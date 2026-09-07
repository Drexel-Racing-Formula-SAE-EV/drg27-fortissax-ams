#ifndef AMS_CURRENT_WINDOW_H_
#define AMS_CURRENT_WINDOW_H_

#include <stdbool.h>
#include <stdint.h>

#include <ams_core/ams_measurement.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Frozen v2.6.27 / FW 0.5.30 current-window timing limits.
 *
 * The producer is expected to be externally serialized by one thread-context
 * lock when integrated into Zephyr. The portable core deliberately owns no
 * RTOS mutex and performs no blocking.
 */
#define AMS_CURRENT_WINDOW_MAX_SAMPLE_AGE_MS       100U
#define AMS_CURRENT_WINDOW_MAX_INTEGRATION_GAP_MS  100U

/*
 * Mutable producer state for one timestamped current-integration stream.
 *
 * The fields mirror the frozen oracle because several are semantically
 * distinct:
 * - sample_* is metadata for the sample about to be published;
 * - last_* is metadata attached to the most recent accepted sample;
 * - active_*_initialized flags keep "mixed" range/provenance states sticky
 *   until rotation instead of allowing later samples to make the epoch look
 *   single-source again.
 */
typedef struct
{
    ams_current_window_t active;
    ams_sequence_t next_sequence;
    ams_time_ms_t last_sample_tick;
    ams_time_ms_t integration_tick;
    float last_current_A;
    float last_filtered_A;
    double total_charge_As;
    double total_absolute_charge_As;
    uint32_t total_invalid_sample_count;
    double active_current_squared_A2s;
    uint32_t last_calibration_id;
    ams_current_uncertainty_t last_uncertainty_mA;
    uint8_t last_selected_range;
    ams_current_uncertainty_t sample_uncertainty_mA;
    uint8_t sample_selected_range;
    bool initialized;
    bool last_sample_valid;
    bool last_calibration_record_confident;
    bool active_calibration_provenance_initialized;
    bool active_sensor_metadata_initialized;
} ams_current_window_accumulator_t;

void ams_current_window_init(ams_current_window_accumulator_t *acc,
                             ams_time_ms_t now);

/*
 * Set metadata for the sample that will immediately follow.
 *
 * Call under the same external serialization used for update()/rotate().
 * A range change is latched as selected_range == 0 for the remainder of the
 * active epoch. Worst uncertainty is retained.
 */
void ams_current_window_set_sensor_metadata(
    ams_current_window_accumulator_t *acc,
    ams_current_uncertainty_t uncertainty_mA,
    uint8_t selected_range);

/*
 * Publish one current sample into the active integration window.
 *
 * Out-of-order timestamps are rejected and taint the active epoch rather than
 * rewinding the integration timeline.
 */
void ams_current_window_update(ams_current_window_accumulator_t *acc,
                               ams_time_ms_t now,
                               float current_A,
                               float filtered_A,
                               bool valid,
                               bool calibration_record_confident,
                               uint32_t calibration_id);

/*
 * Close the active window at boundary_tick and open the next one.
 *
 * Returns the completed window validity. A boundary older than the most recent
 * accepted activity is rejected without rotating state; this is the core-side
 * defense against assigning a crossing sample to the wrong voltage epoch.
 */
bool ams_current_window_rotate(ams_current_window_accumulator_t *acc,
                               ams_time_ms_t boundary_tick,
                               ams_current_window_t *completed);

#ifdef __cplusplus
}
#endif

#endif /* AMS_CURRENT_WINDOW_H_ */
