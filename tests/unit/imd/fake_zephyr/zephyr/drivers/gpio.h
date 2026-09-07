#ifndef ZEPHYR_DRIVERS_GPIO_H_
#define ZEPHYR_DRIVERS_GPIO_H_
#include <stdbool.h>
#include <stdint.h>
#include <zephyr/device.h>
struct gpio_dt_spec {
    const struct device *port;
    uint32_t pin;
    uint32_t dt_flags;
};
#define GPIO_DT_SPEC_GET(node_id, prop) \
    { .port = &fake_device_gpioc, .pin = 5U, .dt_flags = 0U }
#define GPIO_ACTIVE_HIGH 0U
#define GPIO_INPUT 1U
static inline bool gpio_is_ready_dt(const struct gpio_dt_spec *spec)
{
    return spec != 0 && device_is_ready(spec->port);
}
int gpio_pin_configure_dt(const struct gpio_dt_spec *spec, uint32_t flags);
int gpio_pin_get_dt(const struct gpio_dt_spec *spec);
#endif
