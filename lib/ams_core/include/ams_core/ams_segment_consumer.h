#ifndef AMS_SEGMENT_CONSUMER_H
#define AMS_SEGMENT_CONSUMER_H
#include <ams_core/ams_measurement.h>
#include <ams_core/ams_soc_ekf.h>
typedef struct {
 ams_ekf_instance_t segment[AMS_PHYSICAL_SEGMENT_COUNT];
 uint32_t sequence, voltage_tick, invalid_total, accepted_epochs;
 double charge_total;
 bool total_seen, current_trusted;
} ams_segment_consumer_t;
void ams_segment_consumer_init(ams_segment_consumer_t *c);
/* One estimator-thread owner; copied immutable input. Raw voltage only.
 * Outputs remain advisory. This never grants SoH/SoP or watchdog authority. */
bool ams_segment_consumer_step(ams_segment_consumer_t *c,
 const ams_measurement_snapshot_t *s,uint32_t now_ms);
#endif
