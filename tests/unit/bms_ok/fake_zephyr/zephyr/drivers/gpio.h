#ifndef ZEPHYR_DRIVERS_GPIO_H_
#define ZEPHYR_DRIVERS_GPIO_H_
#include <stdbool.h>
#include <stdint.h>
#include <zephyr/device.h>

#define GPIO_ACTIVE_HIGH 0U
#define GPIO_OUTPUT_INACTIVE 0x10U

struct gpio_dt_spec {
    const struct device *port;
    uint32_t pin;
    uint32_t dt_flags;
};

extern struct device fake_gpioe_device;
extern int fake_gpio_configure_status;
extern int fake_gpio_set_status;
extern unsigned int fake_gpio_configure_calls;
extern unsigned int fake_gpio_set_calls;
extern int fake_gpio_last_config_flags;
extern int fake_gpio_last_set_value;

#define GPIO_DT_SPEC_GET(node, prop) { .port = &fake_gpioe_device, .pin = 0U, .dt_flags = GPIO_ACTIVE_HIGH }

static inline bool gpio_is_ready_dt(const struct gpio_dt_spec *spec)
{
    return spec != 0 && spec->port != 0 && spec->port->ready;
}

static inline int gpio_pin_configure_dt(const struct gpio_dt_spec *spec, int flags)
{
    (void)spec;
    fake_gpio_configure_calls++;
    fake_gpio_last_config_flags = flags;
    return fake_gpio_configure_status;
}

static inline int gpio_pin_set_dt(const struct gpio_dt_spec *spec, int value)
{
    (void)spec;
    fake_gpio_set_calls++;
    fake_gpio_last_set_value = value;
    return fake_gpio_set_status;
}
#endif
