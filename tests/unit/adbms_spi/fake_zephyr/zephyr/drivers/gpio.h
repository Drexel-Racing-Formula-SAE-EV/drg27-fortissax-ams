#ifndef ZEPHYR_DRIVERS_GPIO_H_
#define ZEPHYR_DRIVERS_GPIO_H_
#include <errno.h>
#include <stdbool.h>
#include "fake_zephyr_spi.h"
#define GPIO_OUTPUT_INACTIVE 0
struct gpio_dt_spec { const struct device *port; unsigned pin; unsigned dt_flags; };
#define GPIO_DT_SPEC_GET(node, prop) GPIO_DT_SPEC_GET_I(prop)
#define GPIO_DT_SPEC_GET_I(prop) GPIO_DT_SPEC_##prop
#define GPIO_DT_SPEC_cs_a_gpios { &fake_gpioe_dev, 2U, 1U }
#define GPIO_DT_SPEC_cs_b_gpios { &fake_gpioe_dev, 4U, 1U }
static inline bool gpio_is_ready_dt(const struct gpio_dt_spec *s) { return s && device_is_ready(s->port); }
static inline int gpio_pin_configure_dt(const struct gpio_dt_spec *s, int flags) {
    (void)flags;
    if (fake_spi.cs_config_fail) return -EIO;
    if (s->pin == 2U) fake_spi.cs_a_active = false;
    else if (s->pin == 4U) fake_spi.cs_b_active = false;
    return 0;
}
static inline int gpio_pin_set_dt(const struct gpio_dt_spec *s, int value) {
    bool active = value != 0;
    if (active && fake_spi.cs_assert_fail_once) { fake_spi.cs_assert_fail_once = false; return -EIO; }
    if (s->pin == 2U) fake_spi.cs_a_active = active;
    else if (s->pin == 4U) fake_spi.cs_b_active = active;
    else return -EINVAL;
    if (active) fake_spi.cs_assert_count++; else fake_spi.cs_deassert_count++;
    return 0;
}
#endif
