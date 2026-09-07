#ifndef DRG27_AMS_THREADS_H_
#define DRG27_AMS_THREADS_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

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
    size_t stack_unused;
    size_t stack_used_high_water;
};

int ams_threads_start(void);

int ams_thread_snapshot_get(enum ams_thread_id id,
                            struct ams_thread_snapshot *snapshot);

void ams_threads_request_diagnostics(void);

void ams_threads_print_manifest(void);

size_t ams_threads_count(void);

size_t ams_threads_active_count(void);

uint32_t ams_threads_runtime_start_ms(void);

#ifdef __cplusplus
}
#endif

#endif /* DRG27_AMS_THREADS_H_ */