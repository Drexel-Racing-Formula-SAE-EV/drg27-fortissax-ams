#ifndef SOC_H_
#define SOC_H_
#include "fake_zephyr_adc.h"
typedef int IRQn_Type;
static inline void NVIC_ClearPendingIRQ(IRQn_Type irq) { (void)irq; fake_adc.irq_pending=false; fake_adc.irq_clear_count++; fake_adc_trace(FAKE_ADC_EVT_IRQ_CLEAR); }
#define ADC123_COMMON (&fake_adc_common_regs)
#endif
