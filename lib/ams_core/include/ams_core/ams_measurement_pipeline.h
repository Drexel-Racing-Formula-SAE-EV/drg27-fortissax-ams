#ifndef AMS_MEASUREMENT_PIPELINE_H_
#define AMS_MEASUREMENT_PIPELINE_H_
#include <ams_core/ams_measurement.h>
#include <stdatomic.h>
#include <ams_core/ams_current_window.h>
#include <ams_core/ams_cell_image.h>
#include <ams_core/ams_temp_image.h>
typedef struct {
 void *context;
 bool (*lock)(void *context);
 void (*unlock)(void *context);
 uint32_t (*now_ms)(void *context);
} ams_pipeline_clock_lock_t;
typedef struct {
 ams_measurement_store_t store;
 ams_current_window_accumulator_t current;
 ams_pipeline_clock_lock_t ops;
 ams_current_window_t completed;
 uint32_t boundary;
 bool initialized, boundary_ready;
 bool balance_zero_verified;
 atomic_bool dropped_current;
} ams_measurement_pipeline_t;
bool ams_pipeline_init(ams_measurement_pipeline_t *p,
 const ams_pipeline_clock_lock_t *ops, const ams_measurement_lock_ops_t *metadata);
/* Single current producer. Time is read only after acquiring the shared lock. */
bool ams_pipeline_current(ams_measurement_pipeline_t *p,float current,float filtered,
 bool valid,bool confident,uint32_t calibration,uint16_t uncertainty,uint8_t range);
/* Sole voltage/publication writer. Call immediately after cell acquisition. */
bool ams_pipeline_boundary(ams_measurement_pipeline_t *p);
/* Publish the selected one-SMB bench topology into slot zero. Remaining physical
 * segments stay absent. Per-segment masks carry bench validity; full-pack voltage
 * and temperature flags remain false. Boundary is consumed even on a store drop. */
ams_sequence_t ams_pipeline_publish_single(ams_measurement_pipeline_t *p,
 const ams_cell_image_t *cells,const ams_temp_image_t *temps,bool coherent,
 uint32_t acquisition_start);
/* Full-pack estimator admission. Never substitute diagnostics or missing SMBs. */
bool ams_pipeline_estimator_eligible(const ams_measurement_snapshot_t *s,uint32_t now);
#endif
