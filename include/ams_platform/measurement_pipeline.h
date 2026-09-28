#ifndef AMS_PLATFORM_MEASUREMENT_PIPELINE_H_
#define AMS_PLATFORM_MEASUREMENT_PIPELINE_H_
#include <ams_core/ams_measurement.h>
#include <ams_core/ams_current_fault.h>
typedef struct {
 current_fault_state_t current_fault;
 uint32_t current_completions, current_completed_ms, estimator_epochs;
 bool current_window_accepted, estimator_current_trusted;
 uint8_t estimator_valid_mask;
 float segment_soc[AMS_PHYSICAL_SEGMENT_COUNT];
 uint32_t segment_faults[AMS_PHYSICAL_SEGMENT_COUNT];
} ams_z022_diagnostics_t;
void ams_z022_estimator_step(void);
bool ams_z022_copy_diagnostics(ams_z022_diagnostics_t *out);
bool ams_z022_init(void);
void ams_z022_current_step(void);
void ams_z022_begin_release(void);
void ams_z022_voltage_boundary(void);
void ams_z022_publish_release(void);
bool ams_z022_copy_latest(ams_measurement_snapshot_t *out);
#endif
