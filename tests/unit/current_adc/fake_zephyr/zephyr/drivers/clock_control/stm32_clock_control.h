#ifndef ZEPHYR_STM32_CLOCK_CONTROL_H_
#define ZEPHYR_STM32_CLOCK_CONTROL_H_
#include <zephyr/drivers/clock_control.h>
#define STM32_CLOCK_CONTROL_NODE 99
#define STM32_DT_CLOCKS(node) { { 2U, ((node) == 2 ? 8U : 9U) } }
#endif
