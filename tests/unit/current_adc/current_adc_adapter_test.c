#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "current_adc_zephyr.h"
#include "fake_zephyr/fake_zephyr_adc.h"

static unsigned int checks;
static unsigned int failures;

#define CHECK(cond) do { \
    checks++; \
    if (!(cond)) { \
        failures++; \
        printf("FAIL:%d: %s\n", __LINE__, #cond); \
    } \
} while (0)

static void expect_trace(const fake_adc_event_t *expected, unsigned int n)
{
    unsigned int i;
    CHECK(fake_adc.trace_count == n);
    for (i = 0U; i < n && i < fake_adc.trace_count; ++i) {
        CHECK(fake_adc.trace[i] == expected[i]);
    }
}


static uint32_t prng_state = 0xC0FFEEU;

static uint32_t next_prng(void)
{
    uint32_t x = prng_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    prng_state = x;
    return x;
}

static void run_stress(void)
{
    unsigned int i;

    for (i = 0U; i < 50000U; ++i) {
        unsigned int mode = next_prng() % 9U;
        ams_current_adc_pair_t pair;
        int expected;
        int ret;

        fake_adc_reset();
        fake_adc.high_count = (uint16_t)(next_prng() & 0x0FFFU);
        fake_adc.low_count = (uint16_t)(next_prng() & 0x0FFFU);
        fake_adc.high_wait_ms = next_prng() % AMS_CURRENT_ADC_TIMEOUT_MS;
        fake_adc.low_wait_ms = next_prng() % AMS_CURRENT_ADC_TIMEOUT_MS;
        expected = 0;

        switch (mode) {
        case 0U:
            break;
        case 1U:
            fake_adc.setup_high_status = -EIO;
            expected = -EIO;
            break;
        case 2U:
            fake_adc.sequence_high_status = -EINVAL;
            expected = -EINVAL;
            break;
        case 3U:
            fake_adc.async_high_status = -EBUSY;
            expected = -EBUSY;
            break;
        case 4U:
            fake_adc.completion_high_status = -EFAULT;
            expected = -EFAULT;
            break;
        case 5U:
            fake_adc.setup_low_status = -EIO;
            expected = -EIO;
            break;
        case 6U:
            fake_adc.sequence_low_status = -EINVAL;
            expected = -EINVAL;
            break;
        case 7U:
            fake_adc.async_low_status = -EBUSY;
            expected = -EBUSY;
            break;
        default:
            fake_adc.completion_low_status = -EFAULT;
            expected = -EFAULT;
            break;
        }

        ret = ams_current_adc_read_pair(&pair);
        CHECK(ret == expected);
        CHECK(!pair.adapter_faulted);

        if (expected == 0) {
            CHECK(pair.complete && pair.high_fresh && pair.low_fresh);
            CHECK(pair.high_count == fake_adc.high_count);
            CHECK(pair.low_count == fake_adc.low_count);
            CHECK(fake_adc.trace_count == 6U);
            CHECK(fake_adc.trace[0] == FAKE_EVT_SETUP_HIGH);
            CHECK(fake_adc.trace[1] == FAKE_EVT_ASYNC_HIGH);
            CHECK(fake_adc.trace[2] == FAKE_EVT_POLL_HIGH);
            CHECK(fake_adc.trace[3] == FAKE_EVT_SETUP_LOW);
            CHECK(fake_adc.trace[4] == FAKE_EVT_ASYNC_LOW);
            CHECK(fake_adc.trace[5] == FAKE_EVT_POLL_LOW);
        } else if (mode <= 4U) {
            CHECK(!pair.low_fresh && !pair.complete);
            /* A HIGH completion error has consumed a conversion but still
             * must not publish HIGH freshness or attempt LOW. */
            CHECK(!pair.high_fresh);
            CHECK(fake_adc.trace_count <= 3U);
            if (fake_adc.trace_count > 0U) {
                CHECK(fake_adc.trace[0] == FAKE_EVT_SETUP_HIGH);
            }
            for (unsigned int j = 0U; j < fake_adc.trace_count; ++j) {
                CHECK(fake_adc.trace[j] != FAKE_EVT_SETUP_LOW);
                CHECK(fake_adc.trace[j] != FAKE_EVT_ASYNC_LOW);
                CHECK(fake_adc.trace[j] != FAKE_EVT_POLL_LOW);
            }
        } else {
            CHECK(pair.high_fresh);
            CHECK(!pair.low_fresh && !pair.complete);
            CHECK(fake_adc.trace_count >= 4U);
            CHECK(fake_adc.trace[0] == FAKE_EVT_SETUP_HIGH);
            CHECK(fake_adc.trace[1] == FAKE_EVT_ASYNC_HIGH);
            CHECK(fake_adc.trace[2] == FAKE_EVT_POLL_HIGH);
            CHECK(fake_adc.trace[3] == FAKE_EVT_SETUP_LOW);
        }
    }
}

int main(int argc, char **argv)
{
    ams_current_adc_pair_t pair;
    int ret;

    if ((argc > 1) && (strcmp(argv[1], "ambiguous") == 0)) {
        fake_adc_reset();
        CHECK(ams_current_adc_init() == 0);
        fake_adc.suppress_completion_signal = true;
        ret = ams_current_adc_read_pair(&pair);
        CHECK(ret == -EIO);
        CHECK(pair.adapter_faulted);
        CHECK(ams_current_adc_is_faulted());
        CHECK(!pair.complete && !pair.high_fresh && !pair.low_fresh);
        CHECK(ams_current_adc_init() == -EIO);
        printf("PASS current ADC ambiguous-completion SIL: %u checks, %u failures\n",
               checks, failures);
        return failures == 0U ? 0 : 1;
    }

    if ((argc > 1) && (strcmp(argv[1], "low-timeout") == 0)) {
        fake_adc_reset();
        CHECK(ams_current_adc_init() == 0);
        fake_adc.poll_low_status = -EAGAIN;
        fake_adc.low_wait_ms = AMS_CURRENT_ADC_TIMEOUT_MS;
        ret = ams_current_adc_read_pair(&pair);
        CHECK(ret == -ETIMEDOUT);
        CHECK(pair.high_fresh);
        CHECK(!pair.low_fresh && !pair.complete);
        CHECK(pair.adapter_faulted);
        CHECK(ams_current_adc_is_faulted());
        CHECK(fake_adc.trace_count == 6U);
        CHECK(fake_adc.trace[0] == FAKE_EVT_SETUP_HIGH);
        CHECK(fake_adc.trace[1] == FAKE_EVT_ASYNC_HIGH);
        CHECK(fake_adc.trace[2] == FAKE_EVT_POLL_HIGH);
        CHECK(fake_adc.trace[3] == FAKE_EVT_SETUP_LOW);
        CHECK(fake_adc.trace[4] == FAKE_EVT_ASYNC_LOW);
        CHECK(fake_adc.trace[5] == FAKE_EVT_POLL_LOW);
        CHECK(ams_current_adc_init() == -EIO);
        printf("PASS current ADC low-timeout SIL: %u checks, %u failures\n",
               checks, failures);
        return failures == 0U ? 0 : 1;
    }

    /* Readiness failure must not partially initialize the adapter. */
    fake_adc_reset();
    fake_adc.ready_low = false;
    CHECK(ams_current_adc_init() == -ENODEV);
    fake_adc.ready_low = true;
    CHECK(ams_current_adc_init() == 0);
    CHECK(ams_current_adc_init() == 0);
    CHECK(!ams_current_adc_is_faulted());

    /* Exact nominal ordering: setup/read/wait HIGH, then LOW. */
    fake_adc_trace_clear();
    fake_adc.high_count = 1234U;
    fake_adc.low_count = 2345U;
    fake_adc.high_wait_ms = 2U;
    fake_adc.low_wait_ms = 3U;
    ret = ams_current_adc_read_pair(&pair);
    CHECK(ret == 0);
    CHECK(pair.high_count == 1234U);
    CHECK(pair.low_count == 2345U);
    CHECK(pair.high_fresh && pair.low_fresh && pair.complete);
    CHECK(!pair.adapter_faulted);
    CHECK(pair.high_wait_ms == 2U);
    CHECK(pair.low_wait_ms == 3U);
    {
        const fake_adc_event_t expected[] = {
            FAKE_EVT_SETUP_HIGH, FAKE_EVT_ASYNC_HIGH, FAKE_EVT_POLL_HIGH,
            FAKE_EVT_SETUP_LOW, FAKE_EVT_ASYNC_LOW, FAKE_EVT_POLL_LOW,
        };
        expect_trace(expected, sizeof(expected) / sizeof(expected[0]));
    }

    /* HIGH setup failure suppresses every LOW operation and remains retryable. */
    fake_adc_reset();
    fake_adc.setup_high_status = -EIO;
    ret = ams_current_adc_read_pair(&pair);
    CHECK(ret == -EIO);
    CHECK(!pair.high_fresh && !pair.low_fresh && !pair.complete);
    CHECK(!pair.adapter_faulted);
    {
        const fake_adc_event_t expected[] = { FAKE_EVT_SETUP_HIGH };
        expect_trace(expected, 1U);
    }
    fake_adc.setup_high_status = 0;

    /* HIGH async-start failure also suppresses LOW and remains retryable. */
    fake_adc_trace_clear();
    fake_adc.async_high_status = -EBUSY;
    ret = ams_current_adc_read_pair(&pair);
    CHECK(ret == -EBUSY);
    CHECK(!pair.high_fresh && !pair.low_fresh && !pair.complete);
    CHECK(!pair.adapter_faulted);
    {
        const fake_adc_event_t expected[] = {
            FAKE_EVT_SETUP_HIGH, FAKE_EVT_ASYNC_HIGH,
        };
        expect_trace(expected, 2U);
    }
    fake_adc.async_high_status = 0;

    /* LOW setup failure preserves fresh HIGH but never claims complete. */
    fake_adc_trace_clear();
    fake_adc.setup_low_status = -EINVAL;
    ret = ams_current_adc_read_pair(&pair);
    CHECK(ret == -EINVAL);
    CHECK(pair.high_fresh);
    CHECK(!pair.low_fresh && !pair.complete);
    CHECK(!pair.adapter_faulted);
    {
        const fake_adc_event_t expected[] = {
            FAKE_EVT_SETUP_HIGH, FAKE_EVT_ASYNC_HIGH, FAKE_EVT_POLL_HIGH,
            FAKE_EVT_SETUP_LOW,
        };
        expect_trace(expected, 4U);
    }
    fake_adc.setup_low_status = 0;

    /* Completion status is propagated, but a completed transaction can retry. */
    fake_adc_trace_clear();
    fake_adc.completion_high_status = -EIO;
    ret = ams_current_adc_read_pair(&pair);
    CHECK(ret == -EIO);
    CHECK(!pair.high_fresh && !pair.low_fresh && !pair.complete);
    CHECK(!pair.adapter_faulted);
    fake_adc.completion_high_status = 0;

    ret = ams_current_adc_read_pair(&pair);
    CHECK(ret == 0);
    CHECK(pair.complete);

    /* Stateful SIL stress before the deliberately terminal timeout case. */
    run_stress();

    /* Final test: timeout makes in-flight storage permanently non-reusable. */
    fake_adc_trace_clear();
    fake_adc.poll_high_status = -EAGAIN;
    fake_adc.high_wait_ms = AMS_CURRENT_ADC_TIMEOUT_MS;
    ret = ams_current_adc_read_pair(&pair);
    CHECK(ret == -ETIMEDOUT);
    CHECK(pair.adapter_faulted);
    CHECK(ams_current_adc_is_faulted());
    CHECK(!pair.high_fresh && !pair.low_fresh && !pair.complete);
    {
        const fake_adc_event_t expected[] = {
            FAKE_EVT_SETUP_HIGH, FAKE_EVT_ASYNC_HIGH, FAKE_EVT_POLL_HIGH,
        };
        expect_trace(expected, 3U);
    }

    fake_adc_trace_clear();
    CHECK(ams_current_adc_init() == -EIO);
    CHECK(ams_current_adc_read_pair(&pair) == -EIO);
    CHECK(pair.adapter_faulted);
    CHECK(fake_adc.trace_count == 0U);

    printf("PASS current ADC adapter SIL: %u checks, %u failures\n",
           checks, failures);
    return failures == 0U ? 0 : 1;
}
