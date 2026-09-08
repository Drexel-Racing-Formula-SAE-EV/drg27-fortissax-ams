#ifndef SOC_H_
#define SOC_H_
#include "fake_zephyr_spi.h"
typedef int IRQn_Type;
static inline void NVIC_ClearPendingIRQ(IRQn_Type irq) { (void)irq; fake_spi.irq_pending=false; fake_spi.irq_clear_count++; }
#endif
