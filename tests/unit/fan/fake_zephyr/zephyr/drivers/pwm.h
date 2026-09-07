#ifndef ZEPHYR_DRIVERS_PWM_H_
#define ZEPHYR_DRIVERS_PWM_H_
#include <stdint.h>
#include <zephyr/device.h>
typedef uint32_t pwm_flags_t;
#define PWM_POLARITY_NORMAL 0U
int pwm_set_cycles(const struct device *dev, uint32_t channel,
                   uint32_t period_cycles, uint32_t pulse_cycles,
                   pwm_flags_t flags);
int pwm_get_cycles_per_sec(const struct device *dev, uint32_t channel,
                           uint64_t *cycles);
#endif
