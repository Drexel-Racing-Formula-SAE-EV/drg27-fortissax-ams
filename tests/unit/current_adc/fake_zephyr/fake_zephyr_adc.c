#include "fake_zephyr_adc.h"

#include <string.h>

struct fake_adc_platform fake_adc;
struct device fake_adc_clock_dev = { true };
struct device fake_adc_reset_dev = { true };
struct pinctrl_dev_config fake_adc_pinctrl;
ADC_TypeDef fake_adc1_regs;
ADC_TypeDef fake_adc2_regs;
ADC_TypeDef fake_adc3_regs;
ADC_Common_TypeDef fake_adc_common_regs;

void fake_adc_trace(fake_adc_event_t event)
{
    if (fake_adc.trace_count < FAKE_ADC_TRACE_MAX) {
        fake_adc.trace[fake_adc.trace_count++] = event;
    }
}

void fake_adc_reset(void)
{
    memset(&fake_adc, 0, sizeof(fake_adc));
    memset(&fake_adc1_regs, 0, sizeof(fake_adc1_regs));
    memset(&fake_adc2_regs, 0, sizeof(fake_adc2_regs));
    memset(&fake_adc3_regs, 0, sizeof(fake_adc3_regs));
    memset(&fake_adc_common_regs, 0, sizeof(fake_adc_common_regs));
    fake_adc_clock_dev.ready = true;
    fake_adc_reset_dev.ready = true;
    fake_adc.clock_rate_high = 108000000U;
    fake_adc.clock_rate_low = 108000000U;
    fake_adc.high_count = 1111U;
    fake_adc.low_count = 2222U;
    fake_adc.high_complete_after_ms = 1U;
    fake_adc.low_complete_after_ms = 1U;
    fake_adc.now_ms = 100U;
}

void fake_adc_copy_registers_to_platform(void)
{
    fake_adc.adc1 = fake_adc1_regs;
    fake_adc.adc2 = fake_adc2_regs;
    fake_adc.adc3 = fake_adc3_regs;
    fake_adc.common = fake_adc_common_regs;
}
