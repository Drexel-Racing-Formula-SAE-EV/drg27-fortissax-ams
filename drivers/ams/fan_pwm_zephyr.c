#include "fan_pwm_zephyr.h"

#include <errno.h>
#include <math.h>
#include <stddef.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/sys/util.h>

struct fan_pwm_channel {
    const struct device *dev;
    uint32_t channel;
};

static const struct fan_pwm_channel fan_channels[AMS_FAN_ZONE_COUNT] = {
    { DEVICE_DT_GET(DT_NODELABEL(pwm3)), 2U }, /* Fan 1: PA7  / TIM3 CH2 */
    { DEVICE_DT_GET(DT_NODELABEL(pwm3)), 4U }, /* Fan 2: PB1  / TIM3 CH4 */
    { DEVICE_DT_GET(DT_NODELABEL(pwm4)), 3U }, /* Fan 3: PD14 / TIM4 CH3 */
    { DEVICE_DT_GET(DT_NODELABEL(pwm4)), 4U }, /* Fan 4: PD15 / TIM4 CH4 */
    { DEVICE_DT_GET(DT_NODELABEL(pwm5)), 1U }, /* Fan 5: PA0  / TIM5 CH1 */
    { DEVICE_DT_GET(DT_NODELABEL(pwm5)), 2U }, /* Fan 6: PA1  / TIM5 CH2 */
};

static bool platform_ready;
static uint32_t startup_fail_mask;

static uint32_t fan_percent_to_compare(float percent)
{
    if (!isfinite(percent)) {
        percent = 0.0f;
    } else if (percent > 100.0f) {
        percent = 100.0f;
    } else if (percent < 0.0f) {
        percent = 0.0f;
    }

    /* Exact v2.6.27 fans.c mapping. FAN_MAX is 3360 and the 100% path writes
     * CCR=3360 rather than ARR+1. pwm_stm32 subtracts one from the requested
     * up-counter period before programming ARR, so period_cycles=3361 gives
     * the original ARR=3360. */
    if (percent >= 100.0f) {
        return AMS_FAN_PWM_MAX_COMPARE_CYCLES;
    }

    return (uint32_t)(((double)AMS_FAN_PWM_MAX_COMPARE_CYCLES *
                       (double)percent) / 100.0);
}

static int validate_timer(const struct device *dev, uint32_t channel)
{
    uint64_t cycles_per_sec = 0U;
    int ret;

    if ((dev == NULL) || !device_is_ready(dev)) {
        return -ENODEV;
    }

    ret = pwm_get_cycles_per_sec(dev, channel, &cycles_per_sec);
    if (ret != 0) {
        return ret;
    }

    if (cycles_per_sec != AMS_FAN_PWM_EXPECTED_TIMER_CLOCK_HZ) {
        return -ERANGE;
    }

    return 0;
}

int ams_fan_pwm_set_percent(uint8_t zone, float percent)
{
    const struct fan_pwm_channel *fan;
    uint32_t pulse_cycles;

    if (zone >= AMS_FAN_ZONE_COUNT) {
        return -EINVAL;
    }

    if (!platform_ready) {
        return -ENODEV;
    }

    fan = &fan_channels[zone];
    pulse_cycles = fan_percent_to_compare(percent);

    return pwm_set_cycles(fan->dev,
                          fan->channel,
                          AMS_FAN_PWM_PERIOD_CYCLES,
                          pulse_cycles,
                          PWM_POLARITY_NORMAL);
}

uint32_t ams_fan_pwm_force_all_off(void)
{
    uint32_t fail_mask = 0U;

    for (uint8_t zone = 0U; zone < AMS_FAN_ZONE_COUNT; ++zone) {
        if (ams_fan_pwm_set_percent(zone, 0.0f) != 0) {
            fail_mask |= BIT(zone);
        }
    }

    return fail_mask;
}

int ams_fan_pwm_init(void)
{
    int ret;

    platform_ready = false;
    startup_fail_mask = 0U;

    /* Validate each physical timer once. A missing/unclocked timer is a
     * platform-initialization failure, equivalent to the HAL timer init path
     * reaching Error_Handler() before app_create(). */
    ret = validate_timer(fan_channels[0].dev, fan_channels[0].channel);
    if (ret != 0) {
        return ret;
    }

    ret = validate_timer(fan_channels[2].dev, fan_channels[2].channel);
    if (ret != 0) {
        return ret;
    }

    ret = validate_timer(fan_channels[4].dev, fan_channels[4].channel);
    if (ret != 0) {
        return ret;
    }

    platform_ready = true;

    /* Match board_init/fan_init semantics: attempt every fan independently.
     * A PWM-start/write failure is a process/output fault, not an RTOS
     * integrity failure. Preserve it for the fan task and retry later. */
    startup_fail_mask = ams_fan_pwm_force_all_off();

    return 0;
}

uint32_t ams_fan_pwm_startup_fail_mask(void)
{
    return startup_fail_mask;
}

bool ams_fan_pwm_platform_ready(void)
{
    return platform_ready;
}
