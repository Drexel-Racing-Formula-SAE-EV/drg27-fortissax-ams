#ifndef AMS_PRIVATE_SUPERVISION_OWNER_H_
#define AMS_PRIVATE_SUPERVISION_OWNER_H_
/* Zephyr composition-root binding, private to owner startup and adapter SIL. */
#include <zephyr/kernel.h>
#include <stdbool.h>
bool ams_z023_bind(k_tid_t current,k_tid_t adbms,k_tid_t supervisor);
#endif
