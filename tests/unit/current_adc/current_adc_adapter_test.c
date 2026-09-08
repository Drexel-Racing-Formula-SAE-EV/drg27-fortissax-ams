#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <ams_platform/current_adc.h>
#include "fake_zephyr/fake_zephyr_adc.h"
#include "fake_zephyr/stm32_ll_adc.h"

void ams_current_adc_test_reset_state(void);
uint32_t ams_current_adc_test_recovery_count(void);
uint32_t ams_current_adc_test_recovery_failure_count(void);
uint32_t ams_current_adc_test_integrity_violation_count(void);

static unsigned int checks;
static unsigned int failures;

#define CHECK(cond) do { \
    checks++; \
    if (!(cond)) { \
        failures++; \
        printf("FAIL:%d: %s\n", __LINE__, #cond); \
    } \
} while (0)

static void reset_all(void)
{
    fake_adc_reset();
    ams_current_adc_test_reset_state();
}

static void expect_initialized(void)
{
    CHECK(ams_current_adc_init() == 0);
    CHECK(ams_current_adc_init() == 0);
    CHECK(!ams_current_adc_is_faulted());
    CHECK(fake_adc.reset_count == 1U);
    CHECK(fake_adc.irq_enabled == false);
    CHECK(fake_adc.irq_pending == false);
    CHECK(fake_adc.irq_disable_count == 2U);
    CHECK(fake_adc.irq_clear_count == 2U);
    CHECK(fake_adc.start_high_count == 0U);
    CHECK(fake_adc.start_low_count == 0U);
}

static void test_init_failures(void)
{
    reset_all();
    fake_adc_reset_dev.ready = false;
    CHECK(ams_current_adc_init() == -ENODEV);
    CHECK(ams_current_adc_is_faulted());

    reset_all();
    fake_adc.pinctrl_fail = true;
    CHECK(ams_current_adc_init() == -EIO);
    CHECK(ams_current_adc_is_faulted());

    reset_all();
    fake_adc.clock_on_high_fail = true;
    CHECK(ams_current_adc_init() == -EIO);
    CHECK(ams_current_adc_is_faulted());

    reset_all();
    fake_adc.clock_rate_low = 107000000U;
    CHECK(ams_current_adc_init() == -ERANGE);
    CHECK(ams_current_adc_is_faulted());

    reset_all();
    fake_adc.reset_fail = true;
    CHECK(ams_current_adc_init() == -EIO);
    CHECK(ams_current_adc_is_faulted());
}

static void test_nominal_and_order(void)
{
    ams_current_adc_pair_t pair;
    int ret;

    reset_all();
    expect_initialized();
    fake_adc.high_count = 1234U;
    fake_adc.low_count = 2345U;
    fake_adc.high_complete_after_ms = 1U;
    fake_adc.low_complete_after_ms = 2U;

    ret = ams_current_adc_read_pair(&pair);
    CHECK(ret == 0);
    CHECK(pair.high_fresh && pair.low_fresh && pair.complete);
    CHECK(!pair.adapter_faulted);
    CHECK(pair.high_count == 1234U);
    CHECK(pair.low_count == 2345U);
    CHECK(pair.high_status == 0 && pair.low_status == 0);
    CHECK(pair.high_wait_ms < AMS_CURRENT_ADC_TIMEOUT_MS);
    CHECK(pair.low_wait_ms < AMS_CURRENT_ADC_TIMEOUT_MS);
    CHECK(fake_adc.start_high_count == 1U);
    CHECK(fake_adc.start_low_count == 1U);
    CHECK((fake_adc1_regs.SR & (LL_ADC_FLAG_STRT | LL_ADC_FLAG_EOCS)) == 0U);
    CHECK((fake_adc2_regs.SR & (LL_ADC_FLAG_STRT | LL_ADC_FLAG_EOCS)) == 0U);
    CHECK(ams_current_adc_test_recovery_count() == 0U);
}

static void test_high_timeout_recovers(void)
{
    ams_current_adc_pair_t pair;
    unsigned resets_before;

    reset_all();
    expect_initialized();
    resets_before = fake_adc.reset_count;
    fake_adc.high_stuck = true;

    CHECK(ams_current_adc_read_pair(&pair) == -ETIMEDOUT);
    CHECK(!pair.high_fresh && !pair.low_fresh && !pair.complete);
    CHECK(!pair.adapter_faulted);
    CHECK(!ams_current_adc_is_faulted());
    CHECK(pair.high_status == -ETIMEDOUT);
    CHECK(pair.low_status == -EAGAIN);
    CHECK(pair.high_wait_ms >= AMS_CURRENT_ADC_TIMEOUT_MS);
    CHECK(fake_adc.start_low_count == 0U);
    CHECK(fake_adc.reset_count == resets_before + 1U);
    CHECK(ams_current_adc_test_recovery_count() == 1U);
    CHECK(ams_current_adc_test_recovery_failure_count() == 0U);

    fake_adc.high_stuck = false;
    CHECK(ams_current_adc_read_pair(&pair) == 0);
    CHECK(pair.complete && pair.high_fresh && pair.low_fresh);
}

static void test_hal_timeout_boundary_semantics(void)
{
    ams_current_adc_pair_t pair;

    /* HAL_ADC_PollForConversion(timeout=5) checks EOC first, commits timeout
     * only after elapsed > 5, then rechecks EOC. A conversion completing on
     * that recheck remains valid. */
    reset_all();
    expect_initialized();
    fake_adc.high_complete_after_ms = 6U;
    fake_adc.low_complete_after_ms = 1U;
    CHECK(ams_current_adc_read_pair(&pair) == 0);
    CHECK(pair.complete && pair.high_fresh && pair.low_fresh);
    CHECK(ams_current_adc_test_recovery_count() == 0U);

    /* One millisecond later is genuinely beyond the HAL boundary and must
     * return a recoverable timeout rather than a successful sample. */
    reset_all();
    expect_initialized();
    fake_adc.high_complete_after_ms = 7U;
    CHECK(ams_current_adc_read_pair(&pair) == -ETIMEDOUT);
    CHECK(!pair.high_fresh && !pair.complete);
    CHECK(!pair.adapter_faulted);
    CHECK(ams_current_adc_test_recovery_count() == 1U);
}

static void test_low_timeout_recovers_preserves_high(void)
{
    ams_current_adc_pair_t pair;

    reset_all();
    expect_initialized();
    fake_adc.high_count = 1777U;
    fake_adc.low_stuck = true;

    CHECK(ams_current_adc_read_pair(&pair) == -ETIMEDOUT);
    CHECK(pair.high_fresh);
    CHECK(pair.high_count == 1777U);
    CHECK(!pair.low_fresh && !pair.complete);
    CHECK(!pair.adapter_faulted);
    CHECK(!ams_current_adc_is_faulted());
    CHECK(pair.low_status == -ETIMEDOUT);
    CHECK(ams_current_adc_test_recovery_count() == 1U);

    fake_adc.low_stuck = false;
    CHECK(ams_current_adc_read_pair(&pair) == 0);
    CHECK(pair.complete);
}

static void test_io_faults_recover(void)
{
    ams_current_adc_pair_t pair;

    reset_all();
    expect_initialized();
    fake_adc.high_ovr = true;
    CHECK(ams_current_adc_read_pair(&pair) == -EIO);
    CHECK(!pair.adapter_faulted);
    CHECK(!ams_current_adc_is_faulted());
    CHECK(ams_current_adc_test_recovery_count() == 1U);
    fake_adc.high_ovr = false;
    CHECK(ams_current_adc_read_pair(&pair) == 0);

    fake_adc.low_enable_fail = true;
    CHECK(ams_current_adc_read_pair(&pair) == -EIO);
    CHECK(pair.high_fresh && !pair.low_fresh);
    CHECK(!pair.adapter_faulted);
    CHECK(ams_current_adc_test_recovery_count() == 2U);
    fake_adc.low_enable_fail = false;
    CHECK(ams_current_adc_read_pair(&pair) == 0);
}

static void test_failed_recovery_latches(void)
{
    ams_current_adc_pair_t pair;

    reset_all();
    expect_initialized();
    fake_adc.high_stuck = true;
    fake_adc.reset_fail = true;
    CHECK(ams_current_adc_read_pair(&pair) == -EIO);
    CHECK(pair.adapter_faulted);
    CHECK(ams_current_adc_is_faulted());
    CHECK(ams_current_adc_test_recovery_failure_count() == 1U);
    CHECK(ams_current_adc_read_pair(&pair) == -EIO);
    CHECK(pair.adapter_faulted);

    reset_all();
    expect_initialized();
    fake_adc.high_stuck = true;
    fake_adc.corrupt_after_reset = true;
    CHECK(ams_current_adc_read_pair(&pair) == -EIO);
    CHECK(pair.adapter_faulted);
    CHECK(ams_current_adc_is_faulted());
    CHECK(ams_current_adc_test_recovery_failure_count() == 1U);
}

static int nested_ret;
static ams_current_adc_pair_t nested_pair;
static void nested_read(void)
{
    nested_ret = ams_current_adc_read_pair(&nested_pair);
}

static void test_reentry_is_observable(void)
{
    ams_current_adc_pair_t pair;

    reset_all();
    expect_initialized();
    nested_ret = 0;
    memset(&nested_pair, 0, sizeof(nested_pair));
    fake_adc.reentry_hook = nested_read;

    CHECK(ams_current_adc_read_pair(&pair) == 0);
    CHECK(pair.complete);
    CHECK(nested_ret == -EBUSY);
    CHECK(nested_pair.high_status == -EBUSY);
    CHECK(nested_pair.low_status == -EBUSY);
    CHECK(!nested_pair.adapter_faulted);
    CHECK(ams_current_adc_test_integrity_violation_count() == 1U);

    fake_adc.reentry_hook = NULL;
    CHECK(ams_current_adc_read_pair(&pair) == 0);
    CHECK(pair.complete);
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

static void clear_transient_faults(void)
{
    fake_adc.high_stuck = false;
    fake_adc.low_stuck = false;
    fake_adc.high_ovr = false;
    fake_adc.low_ovr = false;
    fake_adc.high_enable_fail = false;
    fake_adc.low_enable_fail = false;
}

static void run_stress(void)
{
    ams_current_adc_pair_t pair;
    unsigned int i;
    uint32_t recovery_expected = 0U;

    reset_all();
    expect_initialized();

    for (i = 0U; i < 50000U; ++i) {
        uint32_t mode = next_prng() % 7U;
        int expected = 0;
        bool expect_high_fresh = true;

        clear_transient_faults();
        fake_adc.high_count = (uint16_t)(next_prng() & 0x0FFFU);
        fake_adc.low_count = (uint16_t)(next_prng() & 0x0FFFU);
        fake_adc.high_complete_after_ms = next_prng() % 3U;
        fake_adc.low_complete_after_ms = next_prng() % 3U;

        switch (mode) {
        case 0U:
            break;
        case 1U:
            fake_adc.high_stuck = true;
            expected = -ETIMEDOUT;
            expect_high_fresh = false;
            recovery_expected++;
            break;
        case 2U:
            fake_adc.low_stuck = true;
            expected = -ETIMEDOUT;
            recovery_expected++;
            break;
        case 3U:
            fake_adc.high_complete_after_ms = 1U;
            fake_adc.high_ovr = true;
            expected = -EIO;
            expect_high_fresh = false;
            recovery_expected++;
            break;
        case 4U:
            fake_adc.low_complete_after_ms = 1U;
            fake_adc.low_ovr = true;
            expected = -EIO;
            recovery_expected++;
            break;
        case 5U:
            fake_adc.high_enable_fail = true;
            expected = -EIO;
            expect_high_fresh = false;
            recovery_expected++;
            break;
        default:
            fake_adc.low_enable_fail = true;
            expected = -EIO;
            recovery_expected++;
            break;
        }

        CHECK(ams_current_adc_read_pair(&pair) == expected);
        CHECK(!pair.adapter_faulted);
        CHECK(!ams_current_adc_is_faulted());
        CHECK(pair.high_fresh == expect_high_fresh);
        if (expected == 0) {
            CHECK(pair.complete && pair.low_fresh);
            CHECK(pair.high_count == fake_adc.high_count);
            CHECK(pair.low_count == fake_adc.low_count);
        } else {
            CHECK(!pair.complete);
            if (!expect_high_fresh) {
                CHECK(!pair.low_fresh);
            }
        }
    }

    CHECK(ams_current_adc_test_recovery_count() == recovery_expected);
    CHECK(ams_current_adc_test_recovery_failure_count() == 0U);
}

int main(int argc, char **argv)
{
    if (argc > 1 && strcmp(argv[1], "high-timeout") == 0) {
        test_high_timeout_recovers();
    } else if (argc > 1 && strcmp(argv[1], "low-timeout") == 0) {
        test_low_timeout_recovers_preserves_high();
    } else if (argc > 1 && strcmp(argv[1], "timeout-boundary") == 0) {
        test_hal_timeout_boundary_semantics();
    } else if (argc > 1 && strcmp(argv[1], "recovery-fail") == 0) {
        test_failed_recovery_latches();
    } else if (argc > 1 && strcmp(argv[1], "reentry") == 0) {
        test_reentry_is_observable();
    } else if (argc > 1 && strcmp(argv[1], "stress") == 0) {
        run_stress();
    } else {
        test_init_failures();
        test_nominal_and_order();
        test_high_timeout_recovers();
        test_low_timeout_recovers_preserves_high();
        test_hal_timeout_boundary_semantics();
        test_io_faults_recover();
        test_failed_recovery_latches();
        test_reentry_is_observable();
        run_stress();
    }

    printf("PASS current ADC private-backend SIL: %u checks, %u failures\n",
           checks, failures);
    return failures == 0U ? 0 : 1;
}
