#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "ams_safety.h"


int main(void)
{
    int ret;

    /*
     * The PRE_KERNEL_1 direct path has already forced PE0 low.
     * Establish normal Zephyr GPIO ownership now.
     */
    ret = ams_safety_init();

    if (ret != 0) {
        /*
         * ams_safety_init() already forced the physical line low.
         * Panic rather than continue with an invalid safety GPIO setup.
         */
        printk("AMS safety init failed: %d\n", ret);
        k_panic();
    }

    printk("DRG27 Fortissax AMS - Zephyr Z-003\n");
    printk("BMS_OK: forced LOW\n");
    printk("BMS authority: DISABLED\n");
    printk("Balance authority: DISABLED\n");

    for (;;) {
        k_sleep(K_SECONDS(1));
    }

    return 0;
}