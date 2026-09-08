#ifndef DRG27_AMS_THREADS_H_
#define DRG27_AMS_THREADS_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <ams_core/ams_imd.h>
#include <ams_core/ams_watchdog_policy.h>

#ifdef __cplusplus
extern "C" {
#endif

enum ams_thread_id {
    AMS_THREAD_SAFETY = 0,
    AMS_THREAD_CURRENT,
    AMS_THREAD_ADBMS,
    AMS_THREAD_CAN,
    AMS_THREAD_ESTIMATOR,
    AMS_THREAD_FAN,
    AMS_THREAD_AIR,
    AMS_THREAD_IMD,
    AMS_THREAD_DIAGNOSTICS,
    AMS_THREAD_COUNT
};

struct ams_imd_runtime_snapshot {
    uint32_t sequence;
    bool valid;
    bool ok;
    bool fault;
    ams_imd_status_t status;
    uint32_t duty_centipercent;
    uint32_t frequency_millihz;
    uint32_t last_valid_ms;
    uint32_t last_update_ms;
    bool capture_started;
    int capture_start_error;
    bool capture_callback_fault;
    uint32_t capture_callback_count;
    uint32_t capture_callback_error_count;
};

struct ams_watchdog_runtime_snapshot {
    uint32_t sequence;
    bool runtime_enabled;
    bool health_good;
    bool coverage_complete;
    bool stop_feed_test;

    uint16_t oracle_required_mask;
    uint16_t migration_evidence_mask;
    uint16_t effective_required_mask;
    uint16_t effective_stale_mask;

    uint16_t stack_warning_mask;
    uint16_t stack_critical_mask;
    uint16_t stack_query_error_mask;
    size_t min_stack_unused;

    ams_watchdog_block_reason_t block_reason;
    uint32_t feed_count;
    uint32_t block_count;
    uint32_t last_feed_ms;

    uint32_t platform_state;
    int platform_last_error;
    uint32_t platform_prepare_attempt_count;
    uint32_t platform_start_attempt_count;
    uint32_t platform_feed_success_count;
    uint32_t platform_feed_failure_count;

    bool reset_cause_valid;
    bool reset_was_watchdog;
    int reset_cause_error;
    int reset_cause_clear_error;
};

struct ams_thread_snapshot {
    const char *name;

    int priority;

    uint32_t period_ms;
    uint32_t stale_deadline_ms;
    uint32_t startup_grace_ms;

    bool enabled;
    bool safety_heartbeat_required;
    bool safety_evidence_ready;

    uint32_t heartbeat_seq;
    uint32_t heartbeat_age_ms;

    uint32_t scheduled_release_ms;
    uint32_t last_start_ms;
    uint32_t last_complete_ms;

    uint32_t last_lateness_ms;
    uint32_t max_lateness_ms;

    uint32_t release_miss_count;
    uint32_t overrun_count;

    uint32_t last_exec_us;
    uint32_t wcet_us;

    bool stale;
    bool startup_grace_active;

    size_t stack_size;
    size_t stack_configured_size;
    size_t stack_unused;
    size_t stack_used_high_water;
    size_t stack_warning_threshold;
    size_t stack_critical_threshold;
    bool stack_query_valid;
    bool stack_warning;
    bool stack_critical;
};

int ams_threads_start(void);

int ams_thread_snapshot_get(enum ams_thread_id id,
                            struct ams_thread_snapshot *snapshot);

int ams_imd_runtime_snapshot_get(struct ams_imd_runtime_snapshot *snapshot);
int ams_watchdog_runtime_snapshot_get(struct ams_watchdog_runtime_snapshot *snapshot);

void ams_threads_request_diagnostics(void);
void ams_threads_print_manifest(void);

size_t ams_threads_count(void);
size_t ams_threads_active_count(void);
uint32_t ams_threads_runtime_start_ms(void);

#ifdef __cplusplus
}
#endif

#endif /* DRG27_AMS_THREADS_H_ */
