#ifndef AMS_CORE_ADBMS_MONITOR_H_
#define AMS_CORE_ADBMS_MONITOR_H_

#include <ams_core/ams_adbms_protocol.h>
#include <ams_core/ams_cell_image.h>
#include <ams_core/ams_temp_diagnostics.h>
#include <ams_core/ams_adbms_recovery.h>

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AMS_ADBMS_Z017_REFERENCE_PREWAIT_US 3000U
#define AMS_ADBMS_Z017_REDUNDANT_WAIT_US 17000U
#define AMS_ADBMS_Z017_AUX_WAIT_US 20000U
#define AMS_ADBMS_Z017_RESET_SETTLE_US 300U
#define AMS_ADBMS_Z017_SNAPSHOT_SETTLE_US 10U
#define AMS_ADBMS_Z017_MAX_POST_ATTEMPTS 2U
#define AMS_ADBMS_Z017_MAX_EPOCH_ATTEMPTS 2U
#define AMS_ADBMS_Z017_IIR_FC 3U

typedef struct {
    void *context;
    ams_adbms_result_t (*now_us)(void *context, uint64_t *now_us);
    ams_adbms_result_t (*delay_us)(void *context, uint32_t delay_us);
    ams_adbms_result_t (*wake_b)(void *context, bool cold);
    ams_adbms_result_t (*write_b)(void *context, const uint8_t *tx, uint16_t tx_len);
    ams_adbms_result_t (*write_read_b)(void *context,
                                       const uint8_t *tx,
                                       uint16_t tx_len,
                                       uint8_t *rx,
                                       uint16_t rx_len);
} ams_adbms_monitor_io_t;

typedef enum {
    AMS_ADBMS_MONITOR_UNINITIALIZED = 0,
    AMS_ADBMS_MONITOR_INITIALIZING,
    AMS_ADBMS_MONITOR_READY,
    AMS_ADBMS_MONITOR_FAULTED,
} ams_adbms_monitor_state_t;

typedef enum {
    AMS_ADBMS_POST_IDLE = 0,
    AMS_ADBMS_POST_BASELINE,
    AMS_ADBMS_POST_OSC_FAST,
    AMS_ADBMS_POST_OSC_SLOW,
    AMS_ADBMS_POST_SUPPLY_UV,
    AMS_ADBMS_POST_SUPPLY_OV,
    AMS_ADBMS_POST_THSD,
    AMS_ADBMS_POST_NVM_ED,
    AMS_ADBMS_POST_NVM_MED,
    AMS_ADBMS_POST_TMOD,
    AMS_ADBMS_POST_SPIFLT,
    AMS_ADBMS_POST_RESTORE,
    AMS_ADBMS_POST_FINAL_BASELINE,
    AMS_ADBMS_POST_PASS,
    AMS_ADBMS_POST_FAIL,
} ams_adbms_post_stage_t;

typedef struct {
    ams_adbms_monitor_state_t state;
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
    ams_adbms_post_stage_t post_stage;
    uint8_t post_expected_flag_d;
    uint16_t post_failed_stage_mask;
    uint16_t post_unexpected_stage_mask;

    /* Startup-only fail-safe balancing finalization. No UNMUTE/nonzero API exists. */
    uint32_t balance_inhibit_attempt_count;
    uint32_t balance_inhibit_fail_count;
    bool balance_mute_verified;
    bool balance_durable_zero_verified;

    uint32_t scan_attempt_count;
    uint32_t scan_success_count;
    uint32_t scan_fail_count;
    uint32_t coherent_restart_count;
    uint32_t coherent_restart_fail_count;
    uint32_t session_expiry_count;

    uint32_t avg8_read_count;
    uint32_t avg8_fail_count;
    uint32_t filtered_read_count;
    uint32_t filtered_fail_count;
    uint32_t statd_fail_count;

    uint32_t transport_timeout_count;
    uint32_t transport_io_count;
    uint32_t transport_terminal_count;
    uint32_t pec_fail_count;
    uint32_t counter_mismatch_count;
    uint32_t config_mismatch_count;
    uint32_t ccts_fault_count;
    uint32_t diagnostic_fail_count;

    ams_adbms_result_t first_error;
    ams_adbms_result_t cleanup_error;
    ams_adbms_result_t last_result;

    uint32_t attempted_ms;
    uint32_t successful_ms;
    uint8_t epoch_attempts;
    uint16_t raw_fresh_mask;
    uint16_t raw_bad_mask;
    uint16_t avg8_fresh_mask;
    uint16_t avg8_bad_mask;
    uint16_t iir_fresh_mask;
    uint16_t iir_bad_mask;
    uint16_t ccts;
    bool ccts_valid;
    bool statd_valid;
    ams_adbms_statd_t statd;

    int16_t raw_codes[AMS_CELL_IMAGE_REGISTER_COUNT];
    int16_t avg8_codes[AMS_CELL_IMAGE_REGISTER_COUNT];
    int16_t iir_codes[AMS_CELL_IMAGE_REGISTER_COUNT];
    ams_cell_image_t cells;
    ams_temp_monitor_t temperature;
    ams_adbms_recovery_t recovery;

    bool session_active;
    bool snapshot_active;
    /* True from a successful SNAP until a successful UNSNAP. It survives a
     * lost awake session/transport recovery so cleanup cannot be forgotten. */
    bool snapshot_cleanup_required;
    uint64_t session_last_activity_us;
    ams_adbms_counter_tracker_t counter;

    /* Sticky diagnostic history excludes deliberately injected passing POST
     * observations; transport/counter statistics are never rolled back. */
    uint32_t sticky_diag_faults;
} ams_adbms_monitor_t;

typedef struct {
    ams_adbms_monitor_state_t state;
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
    ams_adbms_post_stage_t post_stage;
    uint16_t post_failed_stage_mask;
    uint16_t post_unexpected_stage_mask;
    uint32_t balance_inhibit_attempt_count;
    uint32_t balance_inhibit_fail_count;
    bool balance_mute_verified;
    bool balance_durable_zero_verified;
    uint32_t scan_attempt_count;
    uint32_t scan_success_count;
    uint32_t scan_fail_count;
    uint32_t coherent_restart_count;
    uint32_t coherent_restart_fail_count;
    uint32_t session_expiry_count;
    ams_adbms_result_t first_error;
    ams_adbms_result_t cleanup_error;
    ams_adbms_result_t last_result;
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
    ams_adbms_statd_t statd;
    int16_t raw_codes[AMS_CELL_IMAGE_REGISTER_COUNT];
    int16_t avg8_codes[AMS_CELL_IMAGE_REGISTER_COUNT];
    int16_t iir_codes[AMS_CELL_IMAGE_REGISTER_COUNT];
    ams_cell_image_t cells;
    ams_temp_monitor_t temperature;
    ams_adbms_recovery_t recovery;
    ams_adbms_counter_tracker_t counter;
    uint32_t sticky_diag_faults;
    uint32_t transport_timeout_count;
    uint32_t transport_io_count;
    uint32_t transport_terminal_count;
    uint32_t pec_fail_count;
    uint32_t counter_mismatch_count;
    uint32_t config_mismatch_count;
    uint32_t ccts_fault_count;
    uint32_t diagnostic_fail_count;
} ams_adbms_monitor_snapshot_t;

void ams_adbms_monitor_reset(ams_adbms_monitor_t *monitor);
ams_adbms_result_t ams_adbms_monitor_initialize(ams_adbms_monitor_t *monitor,
                                                const ams_adbms_monitor_io_t *io);
ams_adbms_result_t ams_adbms_monitor_acquire(ams_adbms_monitor_t *monitor,
                                             const ams_adbms_monitor_io_t *io,
                                             uint32_t now_ms);
void ams_adbms_monitor_snapshot(const ams_adbms_monitor_t *monitor,
                                ams_adbms_monitor_snapshot_t *snapshot);

/* Sole-owner lifecycle: interrupt withdraws data; step audits or attempts one recovery. */
void ams_adbms_monitor_interrupt(ams_adbms_monitor_t *, ams_adbms_result_t);
ams_adbms_result_t ams_adbms_monitor_recovery_step(ams_adbms_monitor_t *, const ams_adbms_monitor_io_t *, uint32_t now_ms);
ams_adbms_result_t ams_adbms_monitor_temperature(ams_adbms_monitor_t *, const ams_adbms_monitor_io_t *, uint32_t now_ms);
ams_adbms_result_t ams_adbms_monitor_aux2(ams_adbms_monitor_t *, const ams_adbms_monitor_io_t *, uint8_t sensor);
ams_adbms_result_t ams_adbms_monitor_therm_ow(ams_adbms_monitor_t *, const ams_adbms_monitor_io_t *, uint8_t sensor);
#ifdef __cplusplus
}
#endif

#endif /* AMS_CORE_ADBMS_MONITOR_H_ */
