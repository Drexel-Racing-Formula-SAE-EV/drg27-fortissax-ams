#ifndef AMS_PLATFORM_SUPERVISION_H_
#define AMS_PLATFORM_SUPERVISION_H_
#include <ams_core/ams_supervision.h>
#include <ams_platform/adbms_monitor.h>
bool ams_z023_owner(unsigned actor);
void ams_z023_current_complete(uint32_t faults);
/* Only actual monitor work may stage a result; publication completes it. */
void ams_z023_monitor_result(const ams_adbms_monitor_platform_snapshot_t *m);
void ams_z023_temperature_result(uint32_t generation,uint8_t position,
 uint32_t converted,uint32_t failed,uint32_t suppressed);
void ams_z023_publication_complete(bool published);
bool ams_z023_poll(ams_supervision_t *out);
bool ams_z023_copy(ams_supervision_t *out);
#endif
