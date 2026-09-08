#ifndef ZEPHYR_DRIVERS_RESET_H_
#define ZEPHYR_DRIVERS_RESET_H_
#include <errno.h>
#include <stdint.h>
#include <string.h>
#include "fake_zephyr_adc.h"
struct reset_dt_spec { const struct device *dev; uint32_t id; };
#define RESET_DT_SPEC_GET(node) { &fake_adc_reset_dev, 8U }
static inline int reset_line_toggle_dt(const struct reset_dt_spec *spec)
{
    (void)spec;
    fake_adc.reset_count++;
    fake_adc_trace(fake_adc.reset_count == 1U ? FAKE_ADC_EVT_RESET : FAKE_ADC_EVT_RECOVERY_RESET);
    if (fake_adc.reset_fail) return -EIO;
    memset(&fake_adc1_regs, 0, sizeof(fake_adc1_regs));
    memset(&fake_adc2_regs, 0, sizeof(fake_adc2_regs));
    memset(&fake_adc3_regs, 0, sizeof(fake_adc3_regs));
    memset(&fake_adc_common_regs, 0, sizeof(fake_adc_common_regs));
    if (fake_adc.corrupt_after_reset) {
        fake_adc_common_regs.clock = 0xDEADU;
    }
    return 0;
}
#endif
