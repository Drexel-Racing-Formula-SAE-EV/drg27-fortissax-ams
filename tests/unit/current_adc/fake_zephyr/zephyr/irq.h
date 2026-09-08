#ifndef ZEPHYR_IRQ_H_
#define ZEPHYR_IRQ_H_
#include "fake_zephyr_adc.h"
static inline void irq_disable(unsigned irq) { (void)irq; fake_adc.irq_enabled=false; fake_adc.irq_disable_count++; fake_adc_trace(FAKE_ADC_EVT_IRQ_DISABLE); }
#endif
