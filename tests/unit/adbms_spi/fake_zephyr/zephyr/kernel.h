#ifndef ZEPHYR_KERNEL_H_
#define ZEPHYR_KERNEL_H_
#include <stdint.h>
#include "fake_zephyr_spi.h"
static inline uint32_t k_uptime_get_32(void) { return fake_spi.now_ms++; }
#endif
