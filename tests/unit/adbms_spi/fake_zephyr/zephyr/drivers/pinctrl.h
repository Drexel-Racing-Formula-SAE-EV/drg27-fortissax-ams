#ifndef ZEPHYR_DRIVERS_PINCTRL_H_
#define ZEPHYR_DRIVERS_PINCTRL_H_
#include <errno.h>
#include "fake_zephyr_spi.h"
#define PINCTRL_STATE_DEFAULT 0
#define PINCTRL_DT_DEFINE(node) static int fake_pinctrl_anchor __attribute__((unused))
#define PINCTRL_DT_DEV_CONFIG_GET(node) (&fake_pinctrl_cfg)
static inline int pinctrl_apply_state(const struct pinctrl_dev_config *cfg, int state) {
    (void)cfg; (void)state; fake_spi.pinctrl_count++; return fake_spi.pinctrl_fail ? -EIO : 0;
}
#endif
