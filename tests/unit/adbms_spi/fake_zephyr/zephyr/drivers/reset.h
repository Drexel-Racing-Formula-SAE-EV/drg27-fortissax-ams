#ifndef ZEPHYR_DRIVERS_RESET_H_
#define ZEPHYR_DRIVERS_RESET_H_
#include <errno.h>
#include <stdint.h>
#include <string.h>
#include "fake_zephyr_spi.h"
struct reset_dt_spec { const struct device *dev; uint32_t id; };
#define RESET_DT_SPEC_GET(node) { &fake_reset_dev, 21U }
static inline int reset_line_toggle_dt(const struct reset_dt_spec *spec) {
    (void)spec; fake_spi.reset_count++;
    if (fake_spi.reset_fail) return -EIO;
    memset(&fake_spi6_regs, 0, sizeof(fake_spi6_regs));
    fake_spi.fifo_pending = 0U;
    return 0;
}
#endif
