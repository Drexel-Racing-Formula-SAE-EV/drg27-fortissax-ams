#ifndef ZEPHYR_KERNEL_H_
#define ZEPHYR_KERNEL_H_
#include <stdint.h>
#include "fake_zephyr_spi.h"
static inline uint32_t k_uptime_get_32(void) { return fake_spi.now_ms++; }
#ifdef CONFIG_AMS_Z016_LINK_PROBE
#include <stdbool.h>
#include <stddef.h>
typedef void *k_tid_t;
extern uintptr_t fake_current_thread;
extern bool fake_isr, fake_clock_stalled;
extern uint64_t fake_cycles;
static inline k_tid_t k_current_get(void) { return (k_tid_t)fake_current_thread; }
static inline bool k_is_in_isr(void) { return fake_isr; }
static inline uint64_t k_cycle_get_64(void) {
 if (!fake_clock_stalled) fake_cycles+=216U;
 return fake_cycles;
}
static inline uint64_t k_cyc_to_us_floor64(uint64_t c) { return c/216U; }
static inline uint64_t k_us_to_cyc_ceil64(uint64_t u) { return u*216U; }
#define K_MSEC(x) (x)
static inline int32_t k_sleep(int32_t ms) { fake_cycles+=(uint64_t)ms*216000U;return 0; }
#endif
#endif
