#include "ams_safety.h"

#include <ams_platform/fail_low.h>
#include <ams_platform/bms_ok.h>

#include <stdint.h>

#include <zephyr/fatal.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/util.h>



/*
 * Migration authority invariant.
 *
 * Even if somebody accidentally changes prj.conf or menuconfig, the
 * migration image must refuse to compile rather than gain authority.
 */
BUILD_ASSERT(IS_ENABLED(CONFIG_BOARD_DER26_AMS),
             "AMS safety layer requires the DER26 AMS board");

BUILD_ASSERT(IS_ENABLED(CONFIG_SOC_STM32F767XX),
             "AMS safety layer requires STM32F767XX");

BUILD_ASSERT(!IS_ENABLED(CONFIG_AMS_BMS_AUTHORITY),
             "migration stage forbids BMS_OK assertion authority");

BUILD_ASSERT(!IS_ENABLED(CONFIG_AMS_BALANCE_AUTHORITY),
             "migration stage forbids balancing authority");

/*
 * RTOS integrity invariant.
 *
 * v2.6.27 used FreeRTOS configASSERT plus stack-overflow checking.  The
 * migration image keeps assertions enabled and raises the stack-overflow
 * protection level by requiring the Cortex-M MPU hardware guard.  The
 * application heap is also forbidden.  These are compile-time requirements,
 * not merely prj.conf preferences.
 */
BUILD_ASSERT(IS_ENABLED(CONFIG_ASSERT),
             "AMS migration requires kernel assertions");

BUILD_ASSERT(IS_ENABLED(CONFIG_ARM_MPU),
             "AMS migration requires the ARM MPU");

BUILD_ASSERT(IS_ENABLED(CONFIG_HW_STACK_PROTECTION),
             "AMS migration requires hardware stack protection");

BUILD_ASSERT(IS_ENABLED(CONFIG_THREAD_STACK_INFO),
             "AMS migration requires thread stack metadata");

BUILD_ASSERT(IS_ENABLED(CONFIG_INIT_STACKS),
             "AMS migration requires initialized stacks for proactive headroom queries");

BUILD_ASSERT(CONFIG_HEAP_MEM_POOL_SIZE == 0,
             "AMS migration must remain application-heap-free");



static atomic_t ams_panic_latched;

bool ams_safety_panic_latched(void)
{
    return atomic_get(&ams_panic_latched) != 0;
}

/*
 * Establish the normal Zephyr GPIO ownership once device initialization has
 * completed.
 *
 * The direct path is called first and again on every failure path.
 */
int ams_safety_init(void)
{
    /* Hardware ownership is delegated to the platform adapter.  The app safety
     * layer owns policy/fatal sequencing but not GPIO or Devicetree details. */
    return ams_bms_ok_platform_init_low();
}



/*
 * Zephyr fatal-policy override.
 *
 * Zephyr invokes this for fatal conditions such as CPU exceptions,
 * stack-check failures, kernel oopses, and kernel panics.
 *
 * Safety action happens BEFORE any attempt to halt the system.
 *
 * Do not add mutex, heap, workqueue, driver, or logging dependencies before
 * the direct fail-low call.
 */
void k_sys_fatal_error_handler(unsigned int reason,
                               const struct arch_esf *esf)
{
    ARG_UNUSED(esf);

    ams_bms_ok_force_low_direct();
    atomic_set(&ams_panic_latched, 1);

    k_fatal_halt(reason);
}