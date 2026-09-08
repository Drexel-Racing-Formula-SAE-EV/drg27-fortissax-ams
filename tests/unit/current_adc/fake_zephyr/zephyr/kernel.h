#ifndef ZEPHYR_KERNEL_H_
#define ZEPHYR_KERNEL_H_
#include <stdint.h>
#include "fake_zephyr_adc.h"
static inline uint32_t k_uptime_get_32(void) { return fake_adc.now_ms; }
static inline void k_busy_wait(uint32_t us) { (void)us; }
#endif
