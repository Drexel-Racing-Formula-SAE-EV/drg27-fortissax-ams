#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "ams_safety.h"
#include "ams_threads.h"


int main(void)
{
    int ret;

    /*
     * Direct PRE_KERNEL safety initialization has already forced PE0 low.
     * Establish normal Zephyr GPIO ownership before any application threads
     * are permitted to run.
     */
    ret = ams_safety_init();

    if (ret != 0) {
        printk("AMS safety init failed: %d\n", ret);
        k_panic();
    }

    printk("DRG27 Fortissax AMS - Zephyr Z-004\n");
    printk("BMS_OK: forced LOW\n");
    printk("BMS authority: DISABLED\n");
    printk("Balance authority: DISABLED\n");

    ret = ams_threads_start();

    if (ret != 0) {
        printk("AMS runtime start failed: %d\n", ret);
        k_panic();
    }

    printk("AMS runtime threads: %u\n",
           (unsigned int)ams_threads_count());

    /*
     * Request one startup runtime snapshot.
     * Diagnostics remains event-driven.
     */
    ams_threads_request_diagnostics();

    /*
     * Main is startup-only.
     *
     * It does not become an additional AMS service loop.
     */
    for (;;) {
        k_sleep(K_FOREVER);
    }

    return 0;
}