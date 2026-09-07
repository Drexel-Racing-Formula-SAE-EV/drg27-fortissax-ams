#ifndef ZEPHYR_DRIVERS_PWM_H_
#define ZEPHYR_DRIVERS_PWM_H_
#include <stdbool.h>
#include <stdint.h>
#include <zephyr/device.h>
#include <zephyr/sys/util.h>
typedef uint32_t pwm_flags_t;
#define PWM_POLARITY_NORMAL         0U
#define PWM_CAPTURE_TYPE_BOTH       BIT(1)
#define PWM_CAPTURE_MODE_CONTINUOUS BIT(2)
struct pwm_dt_spec {
    const struct device *dev;
    uint32_t channel;
    uint32_t period;
    pwm_flags_t flags;
};
#define PWM_DT_SPEC_GET(node_id) \
    { .dev = &fake_device_pwm2, .channel = 1U, .period = 0U, .flags = 0U }
static inline bool pwm_is_ready_dt(const struct pwm_dt_spec *spec)
{
    return spec != 0 && device_is_ready(spec->dev);
}
typedef void (*pwm_capture_callback_handler_t)(const struct device *dev,
                                                uint32_t channel,
                                                uint32_t period_cycles,
                                                uint32_t pulse_cycles,
                                                int status,
                                                void *user_data);
int pwm_get_cycles_per_sec(const struct device *dev, uint32_t channel, uint64_t *cycles);
int pwm_configure_capture(const struct device *dev,
                          uint32_t channel,
                          pwm_flags_t flags,
                          pwm_capture_callback_handler_t cb,
                          void *user_data);
int pwm_enable_capture(const struct device *dev, uint32_t channel);
#endif
