#ifndef AMS_SUPERVISION_H_
#define AMS_SUPERVISION_H_
#include <stdbool.h>
#include <stdint.h>
#include <ams_core/ams_segment_fault.h>
enum { AMS_SUP_CURRENT, AMS_SUP_ADBMS, AMS_SUP_TEMP, AMS_SUP_ACTORS };
enum {
 AMS_SUP_INVALID=1U, AMS_SUP_PROCESS=2U, AMS_SUP_PUBLICATION=4U,
 AMS_SUP_CONFIG=8U, AMS_SUP_CLEANUP=16U, AMS_SUP_TERMINAL=32U,
 AMS_SUP_UNQUALIFIED=64U, AMS_SUP_DATA_STALE=128U,
 AMS_SUP_PROGRESS_STALE=256U, AMS_SUP_PROTOCOL=512U
};
typedef struct {
 uint64_t sequence;
 uint32_t completed_ms, faults, history;
} ams_supervision_record_t;
typedef struct {
 ams_supervision_record_t actor[AMS_SUP_ACTORS];
 ams_segment_fault_t voltage,temperature;
 uint32_t voltage_data_ms,temperature_data_ms;
 uint32_t temp_converted,temp_failed,temp_suppressed,temp_generation;
 uint64_t consumed[AMS_SUP_ACTORS];
 uint32_t start_ms, evaluated_ms, active_faults, sticky_faults;
 uint32_t heartbeat_ms[AMS_SUP_ACTORS];
 uint8_t accepted_mask, stale_mask;
 bool inhibit; /* Diagnostic only; never grants output authority. */
} ams_supervision_t;
typedef struct {
 uint32_t generation, converted, failed, suppressed;
 uint8_t positions;
 bool generation_valid;
} ams_supervision_scan_t;
void ams_supervision_init(ams_supervision_t *s,uint32_t now);
bool ams_supervision_commit(ams_supervision_t *s,unsigned actor,
 const ams_supervision_record_t *record,uint32_t now);
void ams_supervision_evaluate(ams_supervision_t *s,uint32_t now);
/* Caller serializes producer and supervisor access. No counter wraps/replays. */
bool ams_supervision_next(ams_supervision_record_t *r,uint32_t now,uint32_t faults);
bool ams_supervision_scan(ams_supervision_scan_t *s,uint32_t generation,
 uint8_t position,uint32_t converted,uint32_t failed,uint32_t suppressed);
#endif
