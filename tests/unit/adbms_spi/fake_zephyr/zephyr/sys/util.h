#ifndef ZEPHYR_SYS_UTIL_H_
#define ZEPHYR_SYS_UTIL_H_
#define ARG_UNUSED(x) (void)(x)
#define BUILD_ASSERT(cond, msg) _Static_assert((cond), msg)
#define IS_ENABLED(x) (x)
#define CONFIG_AMS_ADBMS_SPI_PRIVATE_BACKEND 1
#define CONFIG_SPI 0
#define CONFIG_USE_STM32_LL_SPI 1
#define CONFIG_RESET 1
#define CONFIG_CORTEX_M_SYSTICK_64BIT_CYCLE_COUNTER 1
#define CONFIG_TIMER_HAS_64BIT_CYCLE_COUNTER 1
#define CONFIG_PM 0
#endif
