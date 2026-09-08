#ifndef ZEPHYR_IRQ_H_
#define ZEPHYR_IRQ_H_
#include "fake_zephyr_adc.h"
static inline void irq_disable(unsigned irq) { (void)irq; fake_adc.irq_enabled=false; fake_adc.irq_disable_count++; fake_adc_trace(FAKE_ADC_EVT_IRQ_DISABLE); }
static inline void k_irq_clear_pending(unsigned irq) { (void)irq; fake_adc.irq_pending=false; fake_adc.irq_clear_count++; fake_adc_trace(FAKE_ADC_EVT_IRQ_CLEAR); }
#endif
