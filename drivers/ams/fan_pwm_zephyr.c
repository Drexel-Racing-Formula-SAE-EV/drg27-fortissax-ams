#include <ams_platform/fan_pwm.h>

#include <errno.h>
#include <math.h>
#include <stddef.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/devicetree/pwms.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/sys/util.h>

#define AMS_FAN_NODE DT_NODELABEL(ams_fans)

BUILD_ASSERT(IS_ENABLED(CONFIG_AMS_CAP_FAN_PWM_ADAPTER_PRESENT),
             "fan PWM adapter capability must remain present at Z-013");
BUILD_ASSERT(IS_ENABLED(CONFIG_AMS_CAP_FAN_ACTOR_LIVE),
             "fan platform adapter requires live fan capability at Z-013");
BUILD_ASSERT(!IS_ENABLED(CONFIG_AMS_CAP_FAN_PHYSICAL_VALIDATED),
             "Z-013 must not claim physical fan PWM validation");

BUILD_ASSERT(DT_PROP_LEN(AMS_FAN_NODE, pwms) == AMS_FAN_ZONE_COUNT,
             "typed AMS fan bank must expose exactly six PWM outputs");
BUILD_ASSERT(DT_SAME_NODE(DT_PWMS_CTLR_BY_NAME(AMS_FAN_NODE, fan1),
                          DT_NODELABEL(pwm3)) &&
             DT_SAME_NODE(DT_PWMS_CTLR_BY_NAME(AMS_FAN_NODE, fan2),
                          DT_NODELABEL(pwm3)),
             "fan1/fan2 must remain on TIM3 PWM controller");
BUILD_ASSERT(DT_SAME_NODE(DT_PWMS_CTLR_BY_NAME(AMS_FAN_NODE, fan3),
                          DT_NODELABEL(pwm4)) &&
             DT_SAME_NODE(DT_PWMS_CTLR_BY_NAME(AMS_FAN_NODE, fan4),
                          DT_NODELABEL(pwm4)),
             "fan3/fan4 must remain on TIM4 PWM controller");
BUILD_ASSERT(DT_SAME_NODE(DT_PWMS_CTLR_BY_NAME(AMS_FAN_NODE, fan5),
                          DT_NODELABEL(pwm5)) &&
             DT_SAME_NODE(DT_PWMS_CTLR_BY_NAME(AMS_FAN_NODE, fan6),
                          DT_NODELABEL(pwm5)),
             "fan5/fan6 must remain on TIM5 PWM controller");
BUILD_ASSERT(DT_PWMS_CHANNEL_BY_NAME(AMS_FAN_NODE, fan1) == 2U,
             "fan1 must remain TIM3 CH2");
BUILD_ASSERT(DT_PWMS_CHANNEL_BY_NAME(AMS_FAN_NODE, fan2) == 4U,
             "fan2 must remain TIM3 CH4");
BUILD_ASSERT(DT_PWMS_CHANNEL_BY_NAME(AMS_FAN_NODE, fan3) == 3U,
             "fan3 must remain TIM4 CH3");
BUILD_ASSERT(DT_PWMS_CHANNEL_BY_NAME(AMS_FAN_NODE, fan4) == 4U,
             "fan4 must remain TIM4 CH4");
BUILD_ASSERT(DT_PWMS_CHANNEL_BY_NAME(AMS_FAN_NODE, fan5) == 1U,
             "fan5 must remain TIM5 CH1");
BUILD_ASSERT(DT_PWMS_CHANNEL_BY_NAME(AMS_FAN_NODE, fan6) == 2U,
             "fan6 must remain TIM5 CH2");
BUILD_ASSERT(DT_PWMS_FLAGS_BY_NAME(AMS_FAN_NODE, fan1) == PWM_POLARITY_NORMAL &&
             DT_PWMS_FLAGS_BY_NAME(AMS_FAN_NODE, fan2) == PWM_POLARITY_NORMAL &&
             DT_PWMS_FLAGS_BY_NAME(AMS_FAN_NODE, fan3) == PWM_POLARITY_NORMAL &&
             DT_PWMS_FLAGS_BY_NAME(AMS_FAN_NODE, fan4) == PWM_POLARITY_NORMAL &&
             DT_PWMS_FLAGS_BY_NAME(AMS_FAN_NODE, fan5) == PWM_POLARITY_NORMAL &&
             DT_PWMS_FLAGS_BY_NAME(AMS_FAN_NODE, fan6) == PWM_POLARITY_NORMAL,
             "all DER26 fan PWMs must remain active-high");

static const struct pwm_dt_spec fan_channels[AMS_FAN_ZONE_COUNT] = {
    PWM_DT_SPEC_GET_BY_NAME(AMS_FAN_NODE, fan1),
    PWM_DT_SPEC_GET_BY_NAME(AMS_FAN_NODE, fan2),
    PWM_DT_SPEC_GET_BY_NAME(AMS_FAN_NODE, fan3),
    PWM_DT_SPEC_GET_BY_NAME(AMS_FAN_NODE, fan4),
    PWM_DT_SPEC_GET_BY_NAME(AMS_FAN_NODE, fan5),
    PWM_DT_SPEC_GET_BY_NAME(AMS_FAN_NODE, fan6),
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

static int validate_timer(const struct pwm_dt_spec *spec)
{
    uint64_t cycles_per_sec = 0U;
    int ret;

    if ((spec == NULL) || !pwm_is_ready_dt(spec)) {
        return -ENODEV;
    }

    ret = pwm_get_cycles_per_sec(spec->dev, spec->channel, &cycles_per_sec);
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
    const struct pwm_dt_spec *fan;
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
                          fan->flags);
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
    ret = validate_timer(&fan_channels[0]);
    if (ret != 0) {
        return ret;
    }

    ret = validate_timer(&fan_channels[2]);
    if (ret != 0) {
        return ret;
    }

    ret = validate_timer(&fan_channels[4]);
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
