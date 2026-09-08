#ifndef AMS_ADBMS_TIME_INTERNAL_H
#define AMS_ADBMS_TIME_INTERNAL_H
#include <stdbool.h>
#include <stdint.h>
bool ams_adbms_time_now(uint64_t *us);
bool ams_adbms_time_delay(uint32_t us);
#endif
