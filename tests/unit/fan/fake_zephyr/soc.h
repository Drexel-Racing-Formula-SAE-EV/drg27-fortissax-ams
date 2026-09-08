#ifndef SOC_H_
#define SOC_H_
#include <stdint.h>
extern unsigned fake_fan_irq_clear_count;
extern uint32_t fake_fan_last_cleared_irq;
typedef int IRQn_Type;
static inline void NVIC_ClearPendingIRQ(IRQn_Type irq)
{
    fake_fan_irq_clear_count++;
    fake_fan_last_cleared_irq = (uint32_t)irq;
}
#endif
