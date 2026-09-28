#ifndef AMS_PLATFORM_ADBMS_MONITOR_H_
#define AMS_PLATFORM_ADBMS_MONITOR_H_

#include <ams_core/ams_cell_image.h>
#include <ams_core/ams_balance_shadow.h>
#include <ams_core/ams_temp_diagnostics.h>
#include <ams_core/ams_adbms_recovery.h>

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Copied diagnostic view only. No raw opcode, direction, CS, SPI buffer, or
 * mutable owner-state API is exported from the platform boundary. */
typedef enum {
    AMS_ADBMS_MONITOR_PLATFORM_UNINITIALIZED = 0,
    AMS_ADBMS_MONITOR_PLATFORM_INITIALIZING,
    AMS_ADBMS_MONITOR_PLATFORM_READY,
    AMS_ADBMS_MONITOR_PLATFORM_FAULTED,
} ams_adbms_monitor_platform_state_t;

typedef struct {
    bool valid;
    uint16_t cell_uv_mask;
    uint16_t cell_ov_mask;
    uint8_t osc_counter;
} ams_adbms_monitor_statd_snapshot_t;

typedef struct {
    bool known;
    uint8_t expected;
    uint32_t mismatch_count;
    uint32_t unexpected_reset_count;
    uint32_t unknown_count;
} ams_adbms_monitor_counter_snapshot_t;

typedef struct {
    bool owner_bound;
    bool init_attempted;
    ams_adbms_monitor_platform_state_t state;
    bool initialized;
    bool config_verified;
    bool startup_post_passed;
    bool acquisition_live;
    bool physical_validated;

    uint32_t init_attempt_count;
    uint32_t init_fail_count;
    uint32_t post_run_count;
    uint32_t post_fail_count;
    uint8_t post_attempts;
    uint16_t post_failed_stage_mask;
    uint16_t post_unexpected_stage_mask;
    uint32_t balance_inhibit_attempt_count;
    uint32_t balance_inhibit_fail_count;
    bool balance_mute_verified;
    bool balance_durable_zero_verified;
    ams_balance_shadow_t balance_shadow; /* advisory, never applied */

    uint32_t scan_attempt_count;
    uint32_t scan_success_count;
    uint32_t scan_fail_count;
    uint32_t coherent_restart_count;
    uint32_t coherent_restart_fail_count;
    uint32_t session_expiry_count;

    int32_t first_error;
    int32_t cleanup_error;
    int32_t last_result;
    uint32_t attempted_ms;
    uint32_t successful_ms;
    uint8_t epoch_attempts;
    bool snapshot_cleanup_required;

    uint16_t raw_fresh_mask;
    uint16_t raw_bad_mask;
    uint16_t avg8_fresh_mask;
    uint16_t avg8_bad_mask;
    uint16_t iir_fresh_mask;
    uint16_t iir_bad_mask;
    uint16_t ccts;
    bool ccts_valid;
    bool statd_valid;
    ams_adbms_monitor_statd_snapshot_t statd;

    int16_t raw_codes[AMS_CELL_IMAGE_REGISTER_COUNT];
    int16_t avg8_codes[AMS_CELL_IMAGE_REGISTER_COUNT];
    int16_t iir_codes[AMS_CELL_IMAGE_REGISTER_COUNT];
    ams_cell_image_t cells;
    ams_temp_monitor_t temperature;
    ams_adbms_recovery_t recovery;
    ams_adbms_monitor_counter_snapshot_t counter;

    uint32_t sticky_diag_faults;
    uint32_t transport_timeout_count;
    uint32_t transport_io_count;
    uint32_t transport_terminal_count;
    uint32_t pec_fail_count;
    uint32_t counter_mismatch_count;
    uint32_t config_mismatch_count;
    uint32_t ccts_fault_count;
    uint32_t diagnostic_fail_count;
} ams_adbms_monitor_platform_snapshot_t;

/* Owner-thread only lifecycle. Initialization is single-attempt by contract. */
bool ams_adbms_monitor_platform_init_owner(void);
void ams_adbms_monitor_platform_step(uint32_t now_ms);

/* Any thread may copy the last committed diagnostic snapshot. */
bool ams_adbms_monitor_platform_snapshot(ams_adbms_monitor_platform_snapshot_t *out);

#ifdef __cplusplus
}
#endif

#endif /* AMS_PLATFORM_ADBMS_MONITOR_H_ */
