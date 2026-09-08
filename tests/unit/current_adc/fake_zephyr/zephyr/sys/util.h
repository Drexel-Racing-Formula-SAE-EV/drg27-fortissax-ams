#ifndef ZEPHYR_SYS_UTIL_H_
#define ZEPHYR_SYS_UTIL_H_
#define BUILD_ASSERT(cond, msg) _Static_assert((cond), msg)
#define IS_ENABLED(x) (x)
#define CONFIG_AMS_CAP_CURRENT_ADC_ADAPTER_PRESENT 1
#define CONFIG_AMS_CURRENT_ADC_PRIVATE_BACKEND 1
#define CONFIG_ADC 0
#define CONFIG_USE_STM32_LL_ADC 1
#define CONFIG_RESET 1
#endif
