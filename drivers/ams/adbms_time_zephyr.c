#include "adbms_time_internal.h"
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>
BUILD_ASSERT(IS_ENABLED(CONFIG_CORTEX_M_SYSTICK_64BIT_CYCLE_COUNTER), "64-bit SysTick required");
BUILD_ASSERT(IS_ENABLED(CONFIG_TIMER_HAS_64BIT_CYCLE_COUNTER), "64-bit SysTick required");
BUILD_ASSERT(!IS_ENABLED(CONFIG_PM), "PM is outside the qualified timing contract");
/* Pinned v4.4 SysTick cycle_get_64 takes a short spinlock, reads elapsed
 * registers once, and returns. No peripheral polling or 32-bit extension. */
bool ams_adbms_time_now(uint64_t *us)
{
 if (us == NULL || k_is_in_isr()) return false;
 *us=k_cyc_to_us_floor64(k_cycle_get_64());
 return true;
}
bool ams_adbms_time_delay(uint32_t us)
{
 if (us > 1000U || k_is_in_isr()) return false;
 uint64_t start=k_cycle_get_64();
 uint64_t needed=k_us_to_cyc_ceil64(us);
 /* The oracle finite iteration fallback also bounds a stalled timebase. */
 uint32_t budget=1024U+256U*us;
 for (uint32_t i=0; i<budget; ++i) {
  uint64_t now=k_cycle_get_64();
  if (now < start) return false;
  if (now-start >= needed) return true;
 }
 return false;
}
