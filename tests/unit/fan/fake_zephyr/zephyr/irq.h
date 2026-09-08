#ifndef ZEPHYR_IRQ_H_
#define ZEPHYR_IRQ_H_
#include <stdint.h>
extern unsigned fake_fan_irq_disable_count;
extern unsigned fake_fan_irq_clear_count;
extern uint32_t fake_fan_last_disabled_irq;
extern uint32_t fake_fan_last_cleared_irq;
static inline void irq_disable(unsigned int irq) { fake_fan_irq_disable_count++; fake_fan_last_disabled_irq = irq; }
#endif
