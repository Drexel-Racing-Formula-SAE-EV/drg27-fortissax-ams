#include "fake_zephyr_adc.h"

#include <errno.h>
#include <stddef.h>
#include <string.h>

#include <zephyr/drivers/adc.h>

const struct device fake_adc1_device = { .id = 1 };
const struct device fake_adc2_device = { .id = 2 };
fake_adc_control_t fake_adc;

static const struct adc_dt_spec *pending_spec;
static const struct adc_sequence *pending_sequence;
static struct k_poll_signal *pending_signal;
static uint32_t fake_time_ms;

static bool is_high(const struct adc_dt_spec *spec)
{
    return spec != NULL && spec->dev == &fake_adc1_device;
}

static void trace(fake_adc_event_t event)
{
    if (fake_adc.trace_count < FAKE_ADC_TRACE_MAX) {
        fake_adc.trace[fake_adc.trace_count++] = event;
    }
}

void fake_adc_reset(void)
{
    memset(&fake_adc, 0, sizeof(fake_adc));
    fake_adc.ready_high = true;
    fake_adc.ready_low = true;
    fake_adc.high_count = 1111U;
    fake_adc.low_count = 2222U;
    fake_adc.high_wait_ms = 1U;
    fake_adc.low_wait_ms = 1U;
    pending_spec = NULL;
    pending_sequence = NULL;
    pending_signal = NULL;
    fake_time_ms = 100U;
}

void fake_adc_trace_clear(void)
{
    fake_adc.trace_count = 0U;
}

bool adc_is_ready_dt(const struct adc_dt_spec *spec)
{
    return is_high(spec) ? fake_adc.ready_high : fake_adc.ready_low;
}

int adc_channel_setup_dt(const struct adc_dt_spec *spec)
{
    bool high = is_high(spec);
    trace(high ? FAKE_EVT_SETUP_HIGH : FAKE_EVT_SETUP_LOW);
    return high ? fake_adc.setup_high_status : fake_adc.setup_low_status;
}

int adc_sequence_init_dt(const struct adc_dt_spec *spec,
                         struct adc_sequence *sequence)
{
    (void)sequence;
    return is_high(spec) ? fake_adc.sequence_high_status : fake_adc.sequence_low_status;
}

int adc_read_async_dt(const struct adc_dt_spec *spec,
                      const struct adc_sequence *sequence,
                      struct k_poll_signal *signal)
{
    bool high = is_high(spec);
    int status = high ? fake_adc.async_high_status : fake_adc.async_low_status;
    trace(high ? FAKE_EVT_ASYNC_HIGH : FAKE_EVT_ASYNC_LOW);
    if (status != 0) {
        return status;
    }
    pending_spec = spec;
    pending_sequence = sequence;
    pending_signal = signal;
    return 0;
}

void k_poll_signal_init(struct k_poll_signal *signal)
{
    signal->signaled = 0U;
    signal->result = 0;
}

int k_poll_signal_reset(struct k_poll_signal *signal)
{
    signal->signaled = 0U;
    signal->result = 0;
    return 0;
}

void k_poll_event_init(struct k_poll_event *event, int type, int mode,
                       struct k_poll_signal *signal)
{
    (void)type;
    (void)mode;
    event->signal = signal;
}

int k_poll(struct k_poll_event *events, int num_events, int timeout_ms)
{
    bool high;
    int poll_status;
    int completion_status;
    uint16_t count;
    uint32_t wait_ms;

    (void)events;
    (void)num_events;
    (void)timeout_ms;

    if (pending_spec == NULL || pending_sequence == NULL || pending_signal == NULL) {
        return -EINVAL;
    }

    high = is_high(pending_spec);
    trace(high ? FAKE_EVT_POLL_HIGH : FAKE_EVT_POLL_LOW);
    poll_status = high ? fake_adc.poll_high_status : fake_adc.poll_low_status;
    completion_status = high ? fake_adc.completion_high_status : fake_adc.completion_low_status;
    count = high ? fake_adc.high_count : fake_adc.low_count;
    wait_ms = high ? fake_adc.high_wait_ms : fake_adc.low_wait_ms;
    fake_time_ms += wait_ms;

    if (poll_status == 0) {
        if (pending_sequence->buffer != NULL &&
            pending_sequence->buffer_size >= sizeof(uint16_t)) {
            *(uint16_t *)pending_sequence->buffer = count;
        }
        if (!fake_adc.suppress_completion_signal) {
            pending_signal->signaled = 1U;
            pending_signal->result = completion_status;
        }
    }

    pending_spec = NULL;
    pending_sequence = NULL;
    pending_signal = NULL;
    return poll_status;
}

void k_poll_signal_check(struct k_poll_signal *signal,
                         unsigned int *signaled,
                         int *result)
{
    *signaled = signal->signaled;
    *result = signal->result;
}

uint32_t k_uptime_get_32(void)
{
    return fake_time_ms;
}
