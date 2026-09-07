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

    uint32_t heartbeat_seq;
    uint32_t scheduled_release_ms;
    uint32_t last_start_ms;
    uint32_t last_complete_ms;

    uint32_t release_miss_count;
    uint32_t overrun_count;

    bool stale;

    size_t stack_size;
    size_t stack_unused;
};

/*
 * Create all AMS application threads using statically allocated stacks and
 * thread control blocks.
 *
 * Threads are created suspended first, then started in a controlled order.
 */
int ams_threads_start(void);

/*
 * Snapshot one thread's runtime state.
 */
int ams_thread_snapshot_get(enum ams_thread_id id,
                            struct ams_thread_snapshot *snapshot);

/*
 * Request one event-driven diagnostics dump.
 */
void ams_threads_request_diagnostics(void);

/*
 * Number of explicit AMS application threads.
 */
size_t ams_threads_count(void);

#ifdef __cplusplus
}
#endif

#endif /* DRG27_AMS_THREADS_H_ */