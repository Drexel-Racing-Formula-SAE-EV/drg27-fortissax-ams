#ifndef ZEPHYR_SYS_UTIL_H_
#define ZEPHYR_SYS_UTIL_H_
#define CONFIG_AMS_BMS_AUTHORITY 0
#define CONFIG_AMS_CAP_BMS_OK_PLATFORM_ADAPTER_PRESENT 1
#define IS_ENABLED(x) (x)
#define BUILD_ASSERT(cond, msg) _Static_assert(cond, msg)
#endif
