#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include <ams_core/ams_core_contract.h>

#include "ams_safety.h"
#include "ams_threads.h"


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

    printk("DRG27 Fortissax AMS - Zephyr Z-006\n");
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

    printk("AMS runtime threads: %u\n",
           (unsigned int)ams_threads_count());

    /*
     * Give the periodic workers time to produce useful first-cycle runtime
     * data before printing the initial diagnostics snapshot.
     */
    k_sleep(K_MSEC(250));

    ams_threads_request_diagnostics();

    /*
     * Main is startup-only.
     */
    for (;;) {
        k_sleep(K_FOREVER);
    }

    return 0;
}