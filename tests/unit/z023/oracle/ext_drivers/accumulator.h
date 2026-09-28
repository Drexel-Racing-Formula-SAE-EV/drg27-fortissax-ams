/* Host-only input seam for unmodified v2.6.27 policy files. */
#ifndef ORACLE_ACCUMULATOR_H
#define ORACLE_ACCUMULATOR_H
#include <stdbool.h>
#include <stdint.h>
#define NSMBS 1
#define NCELLS 15
#define NTEMPS 24
#define ADBMS6830_MAX_TRACKED_ICS 6
typedef struct {uint16_t status_invalid_ic_mask;} adbms6830_diag_health_t;
typedef struct {
 void *ics;int num_ics;unsigned monitored_cell_count;
 struct {bool statd_valid;uint16_t cell_ov_mask,cell_uv_mask;} diag[1];
 adbms6830_diag_health_t health;
} oracle_smb_t;
static inline const adbms6830_diag_health_t *adbms6830_diag_health_get(const oracle_smb_t *s){return &s->health;}
typedef struct {
 oracle_smb_t smb;
 bool voltage_startup_scan_complete,voltage_full_updated,voltage_full_usable;
 uint16_t usable_voltage_count,updated_voltage_count,stale_voltage_count,pec_fail_cell_count;
 uint16_t max_voltage_mv,min_voltage_mv,usable_voltage_mask[1],cell_voltage_mv[1][15];
 uint8_t max_voltage_seg,max_voltage_cell,min_voltage_seg,min_voltage_cell;
 bool temp_startup_scan_complete,temp_full_usable;
 uint16_t usable_temp_count,updated_temp_count,stale_temp_count,invalid_temp_count;
 uint16_t temp_open_count,temp_short_count,temp_jump_count,temp_rate_rise_count;
 int16_t max_temp_deci_c,min_temp_deci_c,filtered_max_temp_deci_c,filtered_avg_temp_deci_c,temp_max_rate_deci_c_per_s;
 uint8_t max_temp_seg,max_temp_sensor,min_temp_seg,min_temp_sensor,temp_max_rate_seg,temp_max_rate_sensor;
} accumulator_t;
#endif
