#ifndef ZEPHYR_DRIVERS_CLOCK_CONTROL_H_
#define ZEPHYR_DRIVERS_CLOCK_CONTROL_H_
#include <errno.h>
#include <stdint.h>
#include "fake_zephyr_spi.h"
typedef void *clock_control_subsys_t;
static inline int clock_control_on(const struct device *d, clock_control_subsys_t s) {
    (void)d; (void)s; fake_spi.clock_on_count++; return fake_spi.clock_on_fail ? -EIO : 0;
}
static inline int clock_control_get_rate(const struct device *d, clock_control_subsys_t s, uint32_t *rate) {
    (void)d; (void)s; if (fake_spi.clock_rate_fail) return -EIO; *rate = fake_spi.clock_rate; return 0;
}
#endif
