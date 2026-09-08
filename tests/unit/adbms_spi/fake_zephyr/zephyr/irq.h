#ifndef ZEPHYR_IRQ_H_
#define ZEPHYR_IRQ_H_
#include "fake_zephyr_spi.h"
static inline void irq_disable(unsigned irq) { (void)irq; fake_spi.irq_enabled=false; fake_spi.irq_disable_count++; }
#endif
