#ifndef ZEPHYR_STM32_CLOCK_CONTROL_H_
#define ZEPHYR_STM32_CLOCK_CONTROL_H_
struct stm32_pclken { unsigned bus; unsigned enr; };
#define STM32_CLOCK_CONTROL_NODE 99
#define STM32_DT_CLOCKS(node) { { 2U, 21U } }
#endif
