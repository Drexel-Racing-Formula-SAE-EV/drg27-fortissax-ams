#ifndef ZEPHYR_DRIVERS_PWM_H_
#define ZEPHYR_DRIVERS_PWM_H_
#include <stdbool.h>
#include <stdint.h>
#include <zephyr/device.h>
typedef uint32_t pwm_flags_t;
#define PWM_POLARITY_NORMAL 0U
struct pwm_dt_spec {
    const struct device *dev;
    uint32_t channel;
    uint32_t period;
    pwm_flags_t flags;
};
#define PWM_DT_SPEC_GET_BY_NAME(node, name) PWM_DT_SPEC_GET_BY_NAME_##name
#define PWM_DT_SPEC_GET_BY_NAME_fan1 { &fake_device_pwm3, 2U, 31120U, 0U }
#define PWM_DT_SPEC_GET_BY_NAME_fan2 { &fake_device_pwm3, 4U, 31120U, 0U }
#define PWM_DT_SPEC_GET_BY_NAME_fan3 { &fake_device_pwm4, 3U, 31120U, 0U }
#define PWM_DT_SPEC_GET_BY_NAME_fan4 { &fake_device_pwm4, 4U, 31120U, 0U }
#define PWM_DT_SPEC_GET_BY_NAME_fan5 { &fake_device_pwm5, 1U, 31120U, 0U }
#define PWM_DT_SPEC_GET_BY_NAME_fan6 { &fake_device_pwm5, 2U, 31120U, 0U }
static inline bool pwm_is_ready_dt(const struct pwm_dt_spec *spec)
{
    return spec != 0 && device_is_ready(spec->dev);
}
int pwm_set_cycles(const struct device *dev, uint32_t channel,
                   uint32_t period_cycles, uint32_t pulse_cycles,
                   pwm_flags_t flags);
int pwm_get_cycles_per_sec(const struct device *dev, uint32_t channel,
                           uint64_t *cycles);
#endif
