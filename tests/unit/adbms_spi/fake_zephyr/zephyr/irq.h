#ifndef ZEPHYR_IRQ_H_
#define ZEPHYR_IRQ_H_
#include "fake_zephyr_spi.h"
static inline void irq_disable(unsigned irq) { (void)irq; fake_spi.irq_enabled=false; fake_spi.irq_disable_count++; }
static inline void k_irq_clear_pending(unsigned irq) { (void)irq; fake_spi.irq_pending=false; fake_spi.irq_clear_count++; }
#endif
