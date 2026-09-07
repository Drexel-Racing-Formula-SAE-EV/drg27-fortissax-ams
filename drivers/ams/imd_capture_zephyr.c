#include "imd_capture_zephyr.h"

#include <errno.h>
#include <stddef.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/util.h>

#define IMD_CAPTURE_NODE DT_NODELABEL(imd_capture)

BUILD_ASSERT(!IS_ENABLED(CONFIG_AMS_IMD_TARGET_VALIDATED),
             "Z-013 must not claim physical IMD target validation");

static const struct pwm_dt_spec imd_pwm =
    PWM_DT_SPEC_GET(IMD_CAPTURE_NODE);
static const struct gpio_dt_spec imd_status =
    GPIO_DT_SPEC_GET(IMD_CAPTURE_NODE, status_gpios);

static ams_imd_t *active_state;
static bool platform_ready;
static bool capture_started;
static int capture_start_error;

static atomic_t callback_fault;
static atomic_t callback_count;
static atomic_t callback_error_count;

static void atomic_saturating_increment(atomic_t *value)
{
    atomic_val_t observed;

    observed = atomic_get(value);
    while ((uint32_t)observed != UINT32_MAX) {
        if (atomic_cas(value,
                       observed,
                       (atomic_val_t)((uint32_t)observed + 1U))) {
            return;
        }
        observed = atomic_get(value);
    }
}

static void imd_pwm_capture_callback(const struct device *dev,
                                     uint32_t channel,
                                     uint32_t period_cycles,
                                     uint32_t pulse_cycles,
                                     int status,
                                     void *user_data)
{
    ams_imd_t *state = (ams_imd_t *)user_data;

    if ((dev != imd_pwm.dev) ||
        (channel != AMS_IMD_PWM_CHANNEL) ||
        (state == NULL) ||
        (state != active_state)) {
        atomic_set(&callback_fault, 1);
        atomic_saturating_increment(&callback_error_count);
        return;
    }

    atomic_saturating_increment(&callback_count);

    if (status != 0) {
        /* Fail closed immediately at the next 10 Hz service cycle. Do not
         * replace the last coherent tuple with an errored capture. A later
         * clean hardware capture clears this transient capture fault. */
        atomic_set(&callback_fault, 1);
        atomic_saturating_increment(&callback_error_count);
        return;
    }

    /* Zephyr STM32 two-channel PWM capture uses the same hardware arrangement
     * as v2.6.27: CH1 direct rising-edge period capture, CH2 indirect falling-
     * edge pulse capture, with slave RESET mode. Publish the callback's period
     * and pulse as one tuple. */
    ams_imd_capture_publish(state,
                            pulse_cycles,
                            period_cycles,
                            k_uptime_get_32());

    atomic_set(&callback_fault, 0);
}

int ams_imd_capture_init(ams_imd_t *state)
{
    uint64_t cycles_per_sec = 0U;
    int ret;

    platform_ready = false;
    capture_started = false;
    capture_start_error = 0;
    active_state = NULL;
    atomic_set(&callback_fault, 0);
    atomic_set(&callback_count, 0);
    atomic_set(&callback_error_count, 0);

    if (state == NULL) {
        return -EINVAL;
    }

    ams_imd_init(state, 0U, false);

    if (!pwm_is_ready_dt(&imd_pwm) || !gpio_is_ready_dt(&imd_status)) {
        return -ENODEV;
    }

    ret = gpio_pin_configure_dt(&imd_status, GPIO_INPUT);
    if (ret != 0) {
        return ret;
    }

    ret = pwm_get_cycles_per_sec(imd_pwm.dev,
                                 AMS_IMD_PWM_CHANNEL,
                                 &cycles_per_sec);
    if (ret != 0) {
        return ret;
    }

    if (cycles_per_sec != AMS_IMD_EXPECTED_TIMER_CLOCK_HZ) {
        return -ERANGE;
    }

    ams_imd_init(state, (uint32_t)cycles_per_sec, false);
    active_state = state;

    /* Exact hardware mode equivalence with v2.6.27 TIM2 PWM-input/reset mode:
     * normal polarity => CH1 rising direct input + CH2 falling indirect input;
     * BOTH returns period and high pulse from one coherent cycle. */
    ret = pwm_configure_capture(imd_pwm.dev,
                                AMS_IMD_PWM_CHANNEL,
                                PWM_POLARITY_NORMAL |
                                    PWM_CAPTURE_TYPE_BOTH |
                                    PWM_CAPTURE_MODE_CONTINUOUS,
                                imd_pwm_capture_callback,
                                state);
    if (ret != 0) {
        active_state = NULL;
        return ret;
    }

    platform_ready = true;

    /* This maps to the v2.6.27 runtime HAL_TIM_IC_Start*() stage. Keep a start
     * failure as a process fault, not a kernel-integrity panic. */
    ret = pwm_enable_capture(imd_pwm.dev, AMS_IMD_PWM_CHANNEL);
    if (ret != 0) {
        capture_start_error = ret;
        ams_imd_set_capture_started(state, false);
        return 0;
    }

    capture_started = true;
    ams_imd_set_capture_started(state, true);
    return 0;
}

int ams_imd_capture_read_at(ams_imd_t *state, uint32_t now_ms)
{
    atomic_val_t fault_before;
    int level;
    int ret;

    if ((state == NULL) ||
        !platform_ready ||
        (state != active_state) ||
        !capture_started) {
        ams_imd_force_fail_closed(state);
        return 1;
    }

    /* A driver-reported capture error invalidates authority immediately rather
     * than allowing the old tuple to remain healthy for the full 250 ms stale
     * window. This is deliberately more conservative than the HAL path. */
    fault_before = atomic_get(&callback_fault);
    if (fault_before != 0) {
        ams_imd_force_fail_closed(state);
        return 1;
    }

    level = gpio_pin_get_dt(&imd_status);
    if (level < 0) {
        ams_imd_force_fail_closed(state);
        return 1;
    }

    ret = ams_imd_read_at(state,
                          true,
                          level != 0,
                          now_ms);

    /* Close the small race where an errored capture callback runs while the
     * thread is evaluating the previously coherent tuple. */
    if (atomic_get(&callback_fault) != 0) {
        ams_imd_force_fail_closed(state);
        return 1;
    }

    return ret;
}

bool ams_imd_capture_platform_ready(void)
{
    return platform_ready;
}

bool ams_imd_capture_started(void)
{
    return capture_started;
}

int ams_imd_capture_start_error(void)
{
    return capture_start_error;
}

uint32_t ams_imd_capture_callback_count(void)
{
    return (uint32_t)atomic_get(&callback_count);
}

uint32_t ams_imd_capture_callback_error_count(void)
{
    return (uint32_t)atomic_get(&callback_error_count);
}

bool ams_imd_capture_callback_faulted(void)
{
    return atomic_get(&callback_fault) != 0;
}
