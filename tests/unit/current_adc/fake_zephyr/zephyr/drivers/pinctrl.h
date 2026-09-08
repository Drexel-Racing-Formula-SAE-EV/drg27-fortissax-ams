#ifndef ZEPHYR_DRIVERS_PINCTRL_H_
#define ZEPHYR_DRIVERS_PINCTRL_H_
#include <errno.h>
#include "fake_zephyr_adc.h"
#define PINCTRL_STATE_DEFAULT 0
#define PINCTRL_DT_DEFINE(node) static int fake_adc_pinctrl_anchor __attribute__((unused))
#define PINCTRL_DT_DEV_CONFIG_GET(node) (&fake_adc_pinctrl)
static inline int pinctrl_apply_state(const struct pinctrl_dev_config *cfg, int state)
{
    (void)cfg; (void)state;
    fake_adc.pinctrl_count++;
    fake_adc_trace(FAKE_ADC_EVT_PINCTRL);
    return fake_adc.pinctrl_fail ? -EIO : 0;
}
#endif
