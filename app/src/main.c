#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

int main(void)
{
    printk("DRG27 Fortissax AMS - Zephyr bootstrap\n");

    for (;;) {
        k_sleep(K_SECONDS(1));
    }

    return 0;
}