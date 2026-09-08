#ifndef ZEPHYR_DRIVERS_HWINFO_H_
#define ZEPHYR_DRIVERS_HWINFO_H_
#include <stdint.h>
#define RESET_WATCHDOG (1U << 1)
int hwinfo_get_reset_cause(uint32_t *cause);
int hwinfo_clear_reset_cause(void);
#endif
