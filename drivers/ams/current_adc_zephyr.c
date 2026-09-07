#include "current_adc_zephyr.h"

#include <errno.h>
#include <stddef.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/devicetree/io-channels.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>

#define CURRENT_ADC_USER_NODE DT_PATH(zephyr_user)
#define CURRENT_ADC_HIGH_INDEX 0U
#define CURRENT_ADC_LOW_INDEX 1U

BUILD_ASSERT(DT_NODE_HAS_PROP(CURRENT_ADC_USER_NODE, io_channels),
             "DER26 current ADC io-channels property missing");
BUILD_ASSERT(DT_PROP_LEN(CURRENT_ADC_USER_NODE, io_channels) == 2,
             "DER26 current ADC must expose exactly two io-channels");
BUILD_ASSERT(DT_SAME_NODE(DT_IO_CHANNELS_CTLR_BY_IDX(CURRENT_ADC_USER_NODE,
                                                     CURRENT_ADC_HIGH_INDEX),
                          DT_NODELABEL(adc1)),
             "current high range must use ADC1");
BUILD_ASSERT(DT_SAME_NODE(DT_IO_CHANNELS_CTLR_BY_IDX(CURRENT_ADC_USER_NODE,
                                                     CURRENT_ADC_LOW_INDEX),
                          DT_NODELABEL(adc2)),
             "current low range must use ADC2");
BUILD_ASSERT(DT_IO_CHANNELS_INPUT_BY_IDX(CURRENT_ADC_USER_NODE,
                                         CURRENT_ADC_HIGH_INDEX) ==
                 AMS_CURRENT_ADC_HIGH_CHANNEL,
             "current high range must be ADC1_IN3");
BUILD_ASSERT(DT_IO_CHANNELS_INPUT_BY_IDX(CURRENT_ADC_USER_NODE,
                                         CURRENT_ADC_LOW_INDEX) ==
                 AMS_CURRENT_ADC_LOW_CHANNEL,
             "current low range must be ADC2_IN10");
BUILD_ASSERT(DT_PROP(DT_NODELABEL(adc1), st_adc_prescaler) ==
                 AMS_CURRENT_ADC_PRESCALER,
             "ADC1 prescaler must preserve v2.6.27 /6 configuration");
BUILD_ASSERT(DT_PROP(DT_NODELABEL(adc2), st_adc_prescaler) ==
                 AMS_CURRENT_ADC_PRESCALER,
             "ADC2 prescaler must preserve v2.6.27 /6 configuration");

static const struct adc_dt_spec current_adc_high =
    ADC_DT_SPEC_GET_BY_IDX(CURRENT_ADC_USER_NODE, CURRENT_ADC_HIGH_INDEX);
static const struct adc_dt_spec current_adc_low =
    ADC_DT_SPEC_GET_BY_IDX(CURRENT_ADC_USER_NODE, CURRENT_ADC_LOW_INDEX);

typedef struct {
    uint16_t sample;
    struct k_poll_signal signal;
    struct k_poll_event event;
    bool wedged;
} current_adc_channel_context_t;

static current_adc_channel_context_t high_context;
static current_adc_channel_context_t low_context;
static bool adapter_initialized;
static bool adapter_faulted;

static int current_adc_read_one(const struct adc_dt_spec *spec,
                                current_adc_channel_context_t *context,
                                uint16_t *count,
                                uint32_t *wait_ms)
{
    struct adc_sequence sequence;
    unsigned int signaled = 0U;
    uint32_t start_ms;
    int completion_status = 0;
    int ret;

    if ((spec == NULL) || (context == NULL) || (count == NULL) ||
        (wait_ms == NULL)) {
        return -EINVAL;
    }

    if (context->wedged || adapter_faulted) {
        return -EIO;
    }

    /*
     * v2.6.27 reconfigured each ADC channel immediately before every
     * conversion. Preserve that ordering instead of relying on startup-only
     * channel setup.
     */
    ret = adc_channel_setup_dt(spec);
    if (ret != 0) {
        return ret;
    }

    memset(&sequence, 0, sizeof(sequence));
    ret = adc_sequence_init_dt(spec, &sequence);
    if (ret != 0) {
        return ret;
    }

    context->sample = 0U;
    sequence.buffer = &context->sample;
    sequence.buffer_size = sizeof(context->sample);

    k_poll_signal_reset(&context->signal);
    k_poll_event_init(&context->event,
                      K_POLL_TYPE_SIGNAL,
                      K_POLL_MODE_NOTIFY_ONLY,
                      &context->signal);

    start_ms = k_uptime_get_32();
    ret = adc_read_async_dt(spec, &sequence, &context->signal);
    if (ret != 0) {
        *wait_ms = k_uptime_get_32() - start_ms;
        return ret;
    }

    ret = k_poll(&context->event, 1, K_MSEC(AMS_CURRENT_ADC_TIMEOUT_MS));
    *wait_ms = k_uptime_get_32() - start_ms;

    if (ret != 0) {
        /*
         * The STM32 Zephyr ADC context has no public cancel primitive. An
         * overdue async conversion may still complete later and touch its
         * persistent sample/signal objects. Never reuse those objects after
         * timeout: fail closed until reboot rather than create a use-after-
         * timeout race or silently exceed the v2.6.27 5 ms bound.
         */
        context->wedged = true;
        adapter_faulted = true;
        return (ret == -EAGAIN) ? -ETIMEDOUT : ret;
    }

    k_poll_signal_check(&context->signal, &signaled, &completion_status);
    if (signaled == 0U) {
        /* A successful poll without the completion signal is an impossible/
         * ambiguous ownership state. Treat it like a timeout: the async ADC
         * operation may still own the static sample object. */
        context->wedged = true;
        adapter_faulted = true;
        return -EIO;
    }
    if (completion_status != 0) {
        return completion_status;
    }

    *count = context->sample;
    return 0;
}

int ams_current_adc_init(void)
{
    /* A post-timeout re-init could reuse storage still owned by an overdue
     * Zephyr ADC transaction. Recovery is reboot-only until the driver exposes
     * a public cancellation/reset contract. */
    if (adapter_faulted) {
        return -EIO;
    }
    if (adapter_initialized) {
        return 0;
    }

    if (!adc_is_ready_dt(&current_adc_high) ||
        !adc_is_ready_dt(&current_adc_low)) {
        adapter_initialized = false;
        return -ENODEV;
    }

    memset(&high_context, 0, sizeof(high_context));
    memset(&low_context, 0, sizeof(low_context));
    k_poll_signal_init(&high_context.signal);
    k_poll_signal_init(&low_context.signal);

    adapter_faulted = false;
    adapter_initialized = true;
    return 0;
}

int ams_current_adc_read_pair(ams_current_adc_pair_t *pair)
{
    int ret;

    if (pair == NULL) {
        return -EINVAL;
    }

    memset(pair, 0, sizeof(*pair));
    pair->high_status = -EAGAIN;
    pair->low_status = -EAGAIN;

    if (!adapter_initialized) {
        pair->high_status = -EACCES;
        pair->low_status = -EACCES;
        return -EACCES;
    }

    if (adapter_faulted) {
        pair->adapter_faulted = true;
        pair->high_status = -EIO;
        pair->low_status = -EIO;
        return -EIO;
    }

    /* v2.6.27 transaction order is HIGH first, LOW second. */
    ret = current_adc_read_one(&current_adc_high,
                               &high_context,
                               &pair->high_count,
                               &pair->high_wait_ms);
    pair->high_status = ret;
    if (ret != 0) {
        pair->adapter_faulted = adapter_faulted;
        return ret;
    }
    pair->high_fresh = true;

    ret = current_adc_read_one(&current_adc_low,
                               &low_context,
                               &pair->low_count,
                               &pair->low_wait_ms);
    pair->low_status = ret;
    if (ret != 0) {
        pair->adapter_faulted = adapter_faulted;
        return ret;
    }
    pair->low_fresh = true;
    pair->complete = true;
    pair->adapter_faulted = adapter_faulted;
    return 0;
}

bool ams_current_adc_is_faulted(void)
{
    return adapter_faulted;
}
