#include <ams_platform/imd_capture.h>

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/pwm.h>

struct device fake_device_pwm2 = {2, true};
struct device fake_device_gpioc = {3, true};
uint32_t fake_uptime_ms;

static int fake_gpio_config_ret;
static int fake_gpio_level = 1;
static int fake_cycles_ret;
static uint64_t fake_cycles = AMS_IMD_EXPECTED_TIMER_CLOCK_HZ;
static int fake_configure_ret;
static int fake_enable_ret;
static uint32_t configure_count;
static uint32_t enable_count;
static uint32_t gpio_config_count;
static uint32_t gpio_get_count;
static uint32_t cycles_get_count;
static uint32_t configured_channel;
static uint32_t configured_flags;
static pwm_capture_callback_handler_t configured_cb;
static void *configured_user_data;
static bool inject_error_during_gpio_get;

static unsigned long long checks;
static unsigned failures;

#define CHECK(x) do { \
    checks++; \
    if (!(x)) { \
        failures++; \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); \
    } \
} while (0)

static void fake_reset(void)
{
    fake_device_pwm2.ready = true;
    fake_device_gpioc.ready = true;
    fake_uptime_ms = 0U;
    fake_gpio_config_ret = 0;
    fake_gpio_level = 1;
    fake_cycles_ret = 0;
    fake_cycles = AMS_IMD_EXPECTED_TIMER_CLOCK_HZ;
    fake_configure_ret = 0;
    fake_enable_ret = 0;
    configure_count = 0U;
    enable_count = 0U;
    gpio_config_count = 0U;
    gpio_get_count = 0U;
    cycles_get_count = 0U;
    configured_channel = 0U;
    configured_flags = 0U;
    configured_cb = NULL;
    configured_user_data = NULL;
    inject_error_during_gpio_get = false;
}

int gpio_pin_configure_dt(const struct gpio_dt_spec *spec, uint32_t flags)
{
    gpio_config_count++;
    CHECK(spec != NULL);
    CHECK(spec->port == &fake_device_gpioc);
    CHECK(spec->pin == 5U);
    CHECK(flags == GPIO_INPUT);
    return fake_gpio_config_ret;
}

int gpio_pin_get_dt(const struct gpio_dt_spec *spec)
{
    gpio_get_count++;
    CHECK(spec != NULL);
    CHECK(spec->port == &fake_device_gpioc);
    CHECK(spec->pin == 5U);
    if (inject_error_during_gpio_get && configured_cb != NULL) {
        inject_error_during_gpio_get = false;
        configured_cb(&fake_device_pwm2,
                      AMS_IMD_PWM_CHANNEL,
                      10800000U,
                      5400000U,
                      -EIO,
                      configured_user_data);
    }
    return fake_gpio_level;
}

int pwm_get_cycles_per_sec(const struct device *dev, uint32_t channel, uint64_t *cycles)
{
    cycles_get_count++;
    CHECK(dev == &fake_device_pwm2);
    CHECK(channel == AMS_IMD_PWM_CHANNEL);
    if (fake_cycles_ret == 0 && cycles != NULL) {
        *cycles = fake_cycles;
    }
    return fake_cycles_ret;
}

int pwm_configure_capture(const struct device *dev,
                          uint32_t channel,
                          pwm_flags_t flags,
                          pwm_capture_callback_handler_t cb,
                          void *user_data)
{
    configure_count++;
    CHECK(dev == &fake_device_pwm2);
    configured_channel = channel;
    configured_flags = flags;
    configured_cb = cb;
    configured_user_data = user_data;
    return fake_configure_ret;
}

int pwm_enable_capture(const struct device *dev, uint32_t channel)
{
    enable_count++;
    CHECK(dev == &fake_device_pwm2);
    CHECK(channel == AMS_IMD_PWM_CHANNEL);
    return fake_enable_ret;
}

static void emit_capture_dev(const struct device *dev,
                             uint32_t channel,
                             uint32_t period,
                             uint32_t pulse,
                             int status,
                             void *user_data,
                             uint32_t tick)
{
    CHECK(configured_cb != NULL);
    fake_uptime_ms = tick;
    configured_cb(dev, channel, period, pulse, status, user_data);
}

static void emit_capture(uint32_t period, uint32_t pulse, int status, uint32_t tick)
{
    emit_capture_dev(&fake_device_pwm2,
                     AMS_IMD_PWM_CHANNEL,
                     period,
                     pulse,
                     status,
                     configured_user_data,
                     tick);
}

static void test_init_failure_matrix(void)
{
    ams_imd_t state;

    fake_reset();
    CHECK(ams_imd_capture_init(NULL) == -EINVAL);
    CHECK(!ams_imd_capture_platform_ready());
    CHECK(!ams_imd_capture_started());

    fake_reset(); fake_device_pwm2.ready = false;
    CHECK(ams_imd_capture_init(&state) == -ENODEV);
    CHECK(!ams_imd_capture_platform_ready());
    CHECK(!ams_imd_capture_started());
    CHECK(state.ret == 1);

    fake_reset(); fake_device_gpioc.ready = false;
    CHECK(ams_imd_capture_init(&state) == -ENODEV);
    CHECK(!ams_imd_capture_platform_ready());

    fake_reset(); fake_gpio_config_ret = -EIO;
    CHECK(ams_imd_capture_init(&state) == -EIO);
    CHECK(gpio_config_count == 1U);
    CHECK(cycles_get_count == 0U);
    CHECK(!ams_imd_capture_platform_ready());

    fake_reset(); fake_cycles_ret = -EIO;
    CHECK(ams_imd_capture_init(&state) == -EIO);
    CHECK(cycles_get_count == 1U);
    CHECK(configure_count == 0U);

    fake_reset(); fake_cycles = AMS_IMD_EXPECTED_TIMER_CLOCK_HZ - 1U;
    CHECK(ams_imd_capture_init(&state) == -ERANGE);
    CHECK(!ams_imd_capture_platform_ready());
    CHECK(configure_count == 0U);

    fake_reset(); fake_cycles = AMS_IMD_EXPECTED_TIMER_CLOCK_HZ + 1U;
    CHECK(ams_imd_capture_init(&state) == -ERANGE);
    CHECK(!ams_imd_capture_platform_ready());

    fake_reset(); fake_configure_ret = -EINVAL;
    CHECK(ams_imd_capture_init(&state) == -EINVAL);
    CHECK(configure_count == 1U);
    CHECK(enable_count == 0U);
    CHECK(!ams_imd_capture_platform_ready());

    fake_reset(); fake_enable_ret = -EIO;
    CHECK(ams_imd_capture_init(&state) == 0);
    CHECK(ams_imd_capture_platform_ready());
    CHECK(!ams_imd_capture_started());
    CHECK(ams_imd_capture_start_error() == -EIO);
    CHECK(!state.capture_started);
    CHECK(ams_imd_capture_read_at(&state, 0U) != 0);
    CHECK(state.ret == 1 && state.status == AMS_IMD_UNKNOWN && !state.ok_hs);
}

static void test_success_and_fail_closed_semantics(void)
{
    ams_imd_t state;
    ams_imd_t other;

    fake_reset();
    memset(&state, 0xA5, sizeof(state));
    CHECK(ams_imd_capture_init(&state) == 0);
    CHECK(ams_imd_capture_platform_ready());
    CHECK(ams_imd_capture_started());
    CHECK(ams_imd_capture_start_error() == 0);
    CHECK(configure_count == 1U && enable_count == 1U);
    CHECK(configured_channel == AMS_IMD_PWM_CHANNEL);
    CHECK(configured_flags == (PWM_POLARITY_NORMAL | PWM_CAPTURE_TYPE_BOTH |
                               PWM_CAPTURE_MODE_CONTINUOUS));
    CHECK(configured_user_data == &state);
    CHECK(!ams_imd_capture_callback_faulted());
    CHECK(ams_imd_capture_callback_count() == 0U);
    CHECK(ams_imd_capture_callback_error_count() == 0U);

    /* No real capture is never valid. */
    fake_gpio_level = 1;
    CHECK(ams_imd_capture_read_at(&state, 0U) != 0);
    CHECK(state.ret == 1 && state.status == AMS_IMD_UNKNOWN);

    /* 10 Hz / 50% is NORMAL and OK_HS high makes the complete predicate good. */
    emit_capture(10800000U, 5400000U, 0, 1000U);
    CHECK(ams_imd_capture_callback_count() == 1U);
    CHECK(ams_imd_capture_callback_error_count() == 0U);
    CHECK(!ams_imd_capture_callback_faulted());
    CHECK(ams_imd_capture_read_at(&state, 1000U) == 0);
    CHECK(state.status == AMS_IMD_NORMAL);
    CHECK(state.ok_hs);
    CHECK(ams_imd_is_ok(&state));

    /* Independent status low does not invalidate PWM decode; it removes health. */
    fake_gpio_level = 0;
    CHECK(ams_imd_capture_read_at(&state, 1001U) == 0);
    CHECK(state.status == AMS_IMD_NORMAL);
    CHECK(!state.ok_hs);
    CHECK(!ams_imd_is_ok(&state));

    /* Exact 250 ms capture age is valid; 251 ms is stale/fail-closed. */
    fake_gpio_level = 1;
    CHECK(ams_imd_capture_read_at(&state, 1250U) == 0);
    CHECK(ams_imd_capture_read_at(&state, 1251U) != 0);
    CHECK(state.status == AMS_IMD_UNKNOWN && !state.ok_hs);

    emit_capture(10800000U, 5400000U, 0, UINT32_MAX - 100U);
    CHECK(ams_imd_capture_read_at(&state, 100U) == 0); /* age 201 across wrap */
    CHECK(ams_imd_capture_read_at(&state, 151U) != 0); /* age 252 */

    /* Invalid coherent tuples fail closed through the real core. */
    emit_capture(0U, 0U, 0, 2000U);
    CHECK(ams_imd_capture_read_at(&state, 2000U) != 0);
    emit_capture(100U, 101U, 0, 2001U);
    CHECK(ams_imd_capture_read_at(&state, 2001U) != 0);

    /* GPIO driver failure fails closed without fabricating a healthy state. */
    emit_capture(10800000U, 5400000U, 0, 2100U);
    fake_gpio_level = -EIO;
    CHECK(ams_imd_capture_read_at(&state, 2100U) != 0);
    CHECK(state.status == AMS_IMD_UNKNOWN && !state.ok_hs);
    fake_gpio_level = 1;

    /* Wrong state pointer is rejected and the supplied object is failed low. */
    memset(&other, 0xA5, sizeof(other));
    CHECK(ams_imd_capture_read_at(&other, 2100U) != 0);
    CHECK(other.ret == 1 && other.status == AMS_IMD_UNKNOWN && !other.ok_hs);
}

static void test_callback_fault_semantics_and_races(void)
{
    ams_imd_t state;
    struct device wrong_dev = {99, true};
    uint32_t count_before;
    uint32_t errors_before;

    fake_reset();
    CHECK(ams_imd_capture_init(&state) == 0);
    emit_capture(10800000U, 5400000U, 0, 100U);
    CHECK(ams_imd_capture_read_at(&state, 100U) == 0);

    /* Driver-reported error is immediately authority-invalidating. */
    emit_capture(10800000U, 5400000U, -EIO, 110U);
    CHECK(ams_imd_capture_callback_faulted());
    CHECK(ams_imd_capture_callback_count() == 2U);
    CHECK(ams_imd_capture_callback_error_count() == 1U);
    CHECK(ams_imd_capture_read_at(&state, 110U) != 0);
    CHECK(state.status == AMS_IMD_UNKNOWN && !state.ok_hs);

    /* A later clean hardware tuple clears the transient callback fault. */
    emit_capture(10800000U, 5400000U, 0, 120U);
    CHECK(!ams_imd_capture_callback_faulted());
    CHECK(ams_imd_capture_read_at(&state, 120U) == 0);
    CHECK(ams_imd_is_ok(&state));

    /* Malformed callback identity is a diagnostic/platform fault and must not
     * publish a tuple or increment the valid callback counter. */
    count_before = ams_imd_capture_callback_count();
    errors_before = ams_imd_capture_callback_error_count();
    emit_capture_dev(&wrong_dev, AMS_IMD_PWM_CHANNEL, 10800000U, 5400000U,
                     0, configured_user_data, 130U);
    CHECK(ams_imd_capture_callback_faulted());
    CHECK(ams_imd_capture_callback_count() == count_before);
    CHECK(ams_imd_capture_callback_error_count() == errors_before + 1U);
    CHECK(ams_imd_capture_read_at(&state, 130U) != 0);

    /* Clean callback restores only after a fresh coherent tuple. */
    emit_capture(10800000U, 5400000U, 0, 140U);
    CHECK(ams_imd_capture_read_at(&state, 140U) == 0);

    count_before = ams_imd_capture_callback_count();
    errors_before = ams_imd_capture_callback_error_count();
    emit_capture_dev(&fake_device_pwm2, AMS_IMD_PWM_CHANNEL + 1U,
                     10800000U, 5400000U, 0, configured_user_data, 150U);
    CHECK(ams_imd_capture_callback_faulted());
    CHECK(ams_imd_capture_callback_count() == count_before);
    CHECK(ams_imd_capture_callback_error_count() == errors_before + 1U);

    emit_capture(10800000U, 5400000U, 0, 160U);
    CHECK(ams_imd_capture_read_at(&state, 160U) == 0);

    /* Exercise the adapter's second callback-fault check: an error arriving
     * while the task is reading OK_HS cannot be hidden by the old good tuple. */
    inject_error_during_gpio_get = true;
    CHECK(ams_imd_capture_read_at(&state, 160U) != 0);
    CHECK(ams_imd_capture_callback_faulted());
    CHECK(state.status == AMS_IMD_UNKNOWN && !state.ok_hs);

    emit_capture(10800000U, 5400000U, 0, 170U);
    CHECK(ams_imd_capture_read_at(&state, 170U) == 0);
}

static uint32_t rng_state = 0x1D13C0DEU;
static uint32_t rnd(void)
{
    rng_state = rng_state * 1664525U + 1013904223U;
    return rng_state;
}

static void test_randomized_adapter_stress(void)
{
    ams_imd_t state;

    fake_reset();
    CHECK(ams_imd_capture_init(&state) == 0);

    for (uint32_t i = 0U; i < 100000U; ++i) {
        uint32_t tick = i * 3U;
        uint32_t selector = rnd() % 12U;
        uint32_t period;
        uint32_t pulse;
        int cb_status = 0;
        int expected_status = -1;

        fake_gpio_level = ((rnd() & 3U) != 0U) ? 1 : 0;

        switch (selector) {
        case 0U: period = 10800000U; pulse = rnd() % (period + 1U); expected_status = AMS_IMD_NORMAL; break;
        case 1U: period = 5400000U; pulse = rnd() % (period + 1U); expected_status = AMS_IMD_UNDERVOLT; break;
        case 2U: period = 3600000U; pulse = rnd() % (period + 1U); expected_status = AMS_IMD_SPEED_START; break;
        case 3U: period = 2700000U; pulse = rnd() % (period + 1U); expected_status = AMS_IMD_DEVICE_ERROR; break;
        case 4U: period = 2160000U; pulse = rnd() % (period + 1U); expected_status = AMS_IMD_GROUND_FAULT; break;
        case 5U: period = 0U; pulse = 0U; break;
        case 6U: period = 100U; pulse = 101U; break;
        case 7U: period = 10800000U; pulse = 5400000U; cb_status = -EIO; break;
        case 8U: period = 15428571U; pulse = 7000000U; break; /* about 7 Hz -> status 1 after rounding? */
        case 9U: period = 1080000U; pulse = 540000U; break; /* 100 Hz => invalid enum */
        default: period = 10800000U; pulse = rnd() % (period + 1U); expected_status = AMS_IMD_NORMAL; break;
        }

        emit_capture(period, pulse, cb_status, tick);
        if (cb_status != 0) {
            CHECK(ams_imd_capture_callback_faulted());
            CHECK(ams_imd_capture_read_at(&state, tick) != 0);
            CHECK(state.status == AMS_IMD_UNKNOWN);
            continue;
        }

        CHECK(!ams_imd_capture_callback_faulted());
        int ret = ams_imd_capture_read_at(&state, tick);

        if (expected_status >= 0) {
            CHECK(ret == 0);
            CHECK((int)state.status == expected_status);
            CHECK(state.ok_hs == (fake_gpio_level != 0));
            CHECK(ams_imd_is_ok(&state) ==
                  ((expected_status == AMS_IMD_NORMAL) && (fake_gpio_level != 0)));
        } else {
            /* Some deliberately malformed/unsupported-frequency tuples. */
            if (ret != 0) {
                CHECK(state.status == AMS_IMD_UNKNOWN);
                CHECK(!state.ok_hs);
            } else {
                CHECK(state.status >= AMS_IMD_SHORT_TO_CHASSIS_GROUND &&
                      state.status <= AMS_IMD_GROUND_FAULT);
            }
        }
    }
}

int main(void)
{
    test_init_failure_matrix();
    test_success_and_fail_closed_semantics();
    test_callback_fault_semantics_and_races();
    test_randomized_adapter_stress();

    if (failures != 0U) {
        fprintf(stderr, "IMD capture adapter: FAIL (%llu checks, %u failures)\n",
                checks, failures);
        return 1;
    }

    printf("IMD capture adapter: PASS (%llu checks)\n", checks);
    return 0;
}
