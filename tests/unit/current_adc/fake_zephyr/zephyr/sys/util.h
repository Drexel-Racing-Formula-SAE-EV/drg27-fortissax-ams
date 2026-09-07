#ifndef FAKE_ZEPHYR_UTIL_H_
#define FAKE_ZEPHYR_UTIL_H_
#define BUILD_ASSERT(cond, msg) _Static_assert((cond), msg)
#define IS_ENABLED(x) (x)
#ifndef CONFIG_AMS_CAP_CURRENT_ADC_ADAPTER_PRESENT
#define CONFIG_AMS_CAP_CURRENT_ADC_ADAPTER_PRESENT 1
#endif
#endif
