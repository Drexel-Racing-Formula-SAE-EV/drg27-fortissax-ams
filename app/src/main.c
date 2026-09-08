#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include <ams_core/ams_core_contract.h>

#include "ams_safety.h"
#include "ams_threads.h"
#include <ams_platform/current_adc.h>
#include <ams_platform/fan_pwm.h>
#include <ams_platform/imd_capture.h>


int main(void)
{
    int ret;

    ret = ams_safety_init();

    if (ret != 0) {
        printk("AMS safety init failed: %d\n", ret);
        k_panic();
    }

    ret = ams_core_contract_check();

    if (ret != 0) {
        printk("AMS portable core contract failed: %d\n", ret);
        k_panic();
    }

    printk("AMS portable core: contract PASS\n");

    /* Keep the complete Z-011 adapter API in the target link without starting
     * a conversion. Live acquisition remains deferred to Z-022. */
    typedef int (*ams_current_adc_read_pair_fn_t)(ams_current_adc_pair_t *);
    typedef bool (*ams_current_adc_faulted_fn_t)(void);
    volatile ams_current_adc_read_pair_fn_t current_adc_read_anchor =
        ams_current_adc_read_pair;
    volatile ams_current_adc_faulted_fn_t current_adc_faulted_anchor =
        ams_current_adc_is_faulted;

    if ((current_adc_read_anchor == NULL) ||
        (current_adc_faulted_anchor == NULL)) {
        printk("AMS current ADC adapter link contract failed\n");
        k_panic();
    }

    ret = ams_current_adc_init();
    if (ret != 0) {
        printk("AMS current ADC adapter init failed: %d\n", ret);
        k_panic();
    }

    printk("AMS current ADC adapter: READY (acquisition not scheduled)\n");

    /* Z-012: timer/PWM infrastructure failure is equivalent to the legacy
     * MX_TIMx_Init()/Error_Handler() fatal path. Per-channel startup command
     * failures remain soft fan process faults and are retried by the fan task. */
    ret = ams_fan_pwm_init();
    if (ret != 0) {
        printk("AMS fan PWM platform init failed: %d\n", ret);
        k_panic();
    }

    if (ams_fan_pwm_startup_fail_mask() != 0U) {
        printk("AMS fan PWM startup channel fault mask: 0x%02x\n",
               (unsigned int)ams_fan_pwm_startup_fail_mask());
    } else {
        printk("AMS fan PWM adapter: READY (all zones initialized off)\n");
    }
    printk("DRG27 Fortissax AMS - Zephyr Z-014 watchdog candidate\n");
    printk("BMS_OK: forced LOW\n");
    printk("BMS authority: DISABLED\n");
    printk("Balance authority: DISABLED\n");

    /*
     * Print the intended execution topology before it is started.
     */
    ams_threads_print_manifest();

    ret = ams_threads_start();

    if (ret != 0) {
        printk("AMS runtime start failed: %d\n", ret);
        k_panic();
    }

    if (ams_imd_capture_started()) {
        printk("AMS IMD capture: ACTIVE (physical validation still pending)\n");
    } else {
        printk("AMS IMD capture: FAIL-CLOSED start error=%d\n",
               ams_imd_capture_start_error());
    }

    printk("AMS runtime threads: %u active / %u defined\n",
           (unsigned int)ams_threads_active_count(),
           (unsigned int)ams_threads_count());

    /*
     * Give the periodic workers time to produce useful first-cycle runtime
     * data before printing the initial diagnostics snapshot.
     */
    k_sleep(K_MSEC(250));

    struct ams_watchdog_runtime_snapshot watchdog_snapshot;
    if (ams_watchdog_runtime_snapshot_get(&watchdog_snapshot) == 0) {
        printk("AMS watchdog: runtime=%s platform_state=%u reason=%s coverage=%s\n",
               watchdog_snapshot.runtime_enabled ? "ACTIVE" : "PREPARED-NOT-ARMED",
               (unsigned int)watchdog_snapshot.platform_state,
               ams_watchdog_block_reason_str(watchdog_snapshot.block_reason),
               watchdog_snapshot.coverage_complete ? "FULL" : "PARTIAL");
    }

    ams_threads_request_diagnostics();

    /*
     * Main is startup-only.
     */
    for (;;) {
        k_sleep(K_FOREVER);
    }

    return 0;
}