#ifndef AMS_TEMP_DIAGNOSTICS_H_
#define AMS_TEMP_DIAGNOSTICS_H_
#include <ams_core/ams_temp_image.h>
typedef struct {
 uint32_t attempts, failures, restore_failures;
 uint8_t sensor;
 bool valid, suspect;
 int16_t baseline, secondary, pullup, recovery;
 int16_t delta_mv, pullup_delta_mv, recovery_delta_mv;
 int32_t result, restore_result;
} ams_temp_diagnostic_t;
typedef struct {
 ams_temp_image_t image;
 uint32_t attempts, failures;
 uint32_t received_mask, attempted_mask;
 uint8_t next_position, mux_valid_mask, selected[3];
 bool config_cleanup_required;
 int32_t result;
 ams_temp_diagnostic_t aux2, open_wire;
} ams_temp_monitor_t;
#endif
