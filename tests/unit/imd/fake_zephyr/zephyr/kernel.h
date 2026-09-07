#ifndef ZEPHYR_KERNEL_H_
#define ZEPHYR_KERNEL_H_
#include <stdint.h>
extern uint32_t fake_uptime_ms;
static inline uint32_t k_uptime_get_32(void) { return fake_uptime_ms; }
#endif
