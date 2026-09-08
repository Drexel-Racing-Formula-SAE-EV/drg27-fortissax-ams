#ifndef ZEPHYR_SYS_UTIL_H_
#define ZEPHYR_SYS_UTIL_H_
#define ARG_UNUSED(x) (void)(x)
#define BUILD_ASSERT(cond, msg) _Static_assert((cond), msg)
#define IS_ENABLED(x) (x)
#define CONFIG_AMS_ADBMS_SPI_PRIVATE_BACKEND 1
#define CONFIG_SPI 0
#define CONFIG_USE_STM32_LL_SPI 1
#define CONFIG_RESET 1
#define CONFIG_ARCH_HAS_IRQ_PENDING_OPS 1
#endif
