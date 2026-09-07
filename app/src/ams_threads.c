#include "ams_threads.h"

#include <errno.h>
#include <stdint.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>


/*
 * --------------------------------------------------------------------------
 * Z-004 runtime design
 * --------------------------------------------------------------------------
 *
 * This file establishes execution topology only.
 *
 * It deliberately DOES NOT:
 * - access current ADCs
 * - access ADBMS SPI
 * - transmit or receive CAN
 * - execute estimator algorithms
 * - command fans
 * - command balancing
 * - assert BMS_OK
 *
 * Those subsystems are migrated independently after the runtime topology has
 * been established and validated.
 */


/*
 * Zephyr uses numerically smaller non-negative priorities for higher-priority
 * preemptible threads.
 *
 * Preserve relative precedence from the migration plan; do not interpret
 * these numbers as FreeRTOS priority values.
 */
#define AMS_PRIO_SAFETY       0
#define AMS_PRIO_CURRENT      2
#define AMS_PRIO_ADBMS        3
#define AMS_PRIO_CAN          4
#define AMS_PRIO_ESTIMATOR    6
#define AMS_PRIO_FAN          8
#define AMS_PRIO_AIR          8
#define AMS_PRIO_IMD          9
#define AMS_PRIO_DIAGNOSTICS 12


/*
 * Initial periods.
 *
 * Diagnostics is event-driven and therefore has no periodic release.
 */
#define AMS_PERIOD_SAFETY_MS       50U
#define AMS_PERIOD_CURRENT_MS      20U
#define AMS_PERIOD_ADBMS_MS       100U
#define AMS_PERIOD_CAN_MS         100U
#define AMS_PERIOD_ESTIMATOR_MS   100U
#define AMS_PERIOD_FAN_MS         200U
#define AMS_PERIOD_AIR_MS         500U
#define AMS_PERIOD_IMD_MS         100U
#define AMS_PERIOD_DIAGNOSTICS_MS 0U


/*
 * Initial migration stack allocations.
 *
 * These are intentionally generous starting values. Actual high-water data
 * will be collected before final stack sizing.
 */
#define AMS_STACK_SAFETY       2048U
#define AMS_STACK_CURRENT      2048U
#define AMS_STACK_ADBMS        8192U
#define AMS_STACK_CAN          8192U
#define AMS_STACK_ESTIMATOR    8192U
#define AMS_STACK_FAN          1536U
#define AMS_STACK_AIR          1536U
#define AMS_STACK_IMD          1536U
#define AMS_STACK_DIAGNOSTICS  4096U


BUILD_ASSERT(CONFIG_NUM_PREEMPT_PRIORITIES > AMS_PRIO_DIAGNOSTICS,
             "Zephyr requires at least 13 preemptible priorities for AMS layout");


struct ams_runtime_stat {
    atomic_t heartbeat_seq;
    atomic_t scheduled_release_ms;
    atomic_t last_start_ms;
    atomic_t last_complete_ms;
    atomic_t release_miss_count;
    atomic_t overrun_count;
    atomic_t stale;
};


struct ams_thread_descriptor {
    enum ams_thread_id id;

    const char *name;

    int priority;
    uint32_t period_ms;

    struct k_thread *thread;
    k_thread_stack_t *stack;
    size_t stack_size;

    struct ams_runtime_stat *stat;
};


/*
 * --------------------------------------------------------------------------
 * Static stacks
 * --------------------------------------------------------------------------
 */

K_THREAD_STACK_DEFINE(safety_stack, AMS_STACK_SAFETY);
K_THREAD_STACK_DEFINE(current_stack, AMS_STACK_CURRENT);
K_THREAD_STACK_DEFINE(adbms_stack, AMS_STACK_ADBMS);
K_THREAD_STACK_DEFINE(can_stack, AMS_STACK_CAN);
K_THREAD_STACK_DEFINE(estimator_stack, AMS_STACK_ESTIMATOR);
K_THREAD_STACK_DEFINE(fan_stack, AMS_STACK_FAN);
K_THREAD_STACK_DEFINE(air_stack, AMS_STACK_AIR);
K_THREAD_STACK_DEFINE(imd_stack, AMS_STACK_IMD);
K_THREAD_STACK_DEFINE(diagnostics_stack, AMS_STACK_DIAGNOSTICS);


/*
 * Static thread control blocks.
 */
static struct k_thread safety_thread;
static struct k_thread current_thread;
static struct k_thread adbms_thread;
static struct k_thread can_thread;
static struct k_thread estimator_thread;
static struct k_thread fan_thread;
static struct k_thread air_thread;
static struct k_thread imd_thread;
static struct k_thread diagnostics_thread;


/*
 * Static runtime statistics.
 */
static struct ams_runtime_stat runtime_stats[AMS_THREAD_COUNT];


/*
 * Diagnostics wakeup.
 *
 * Diagnostics is explicitly event-driven rather than another periodic
 * safety/runtime task.
 */
K_SEM_DEFINE(diagnostics_request, 0, 1);


/*
 * Runtime becomes true only after every thread object has been created.
 */
static atomic_t runtime_started;


/*
 * Thread descriptors make every AMS execution context explicit.
 *
 * AIR is deliberately listed here as a first-class Zephyr thread.
 * The FreeRTOS oracle created AIR through a separate xTaskCreateStatic()
 * path; that architectural exception must not cause AIR to disappear during
 * migration.
 */
static struct ams_thread_descriptor threads[AMS_THREAD_COUNT] = {
    [AMS_THREAD_SAFETY] = {
        .id = AMS_THREAD_SAFETY,
        .name = "ams_safety",
        .priority = AMS_PRIO_SAFETY,
        .period_ms = AMS_PERIOD_SAFETY_MS,
        .thread = &safety_thread,
        .stack = safety_stack,
        .stack_size = K_THREAD_STACK_SIZEOF(safety_stack),
        .stat = &runtime_stats[AMS_THREAD_SAFETY],
    },

    [AMS_THREAD_CURRENT] = {
        .id = AMS_THREAD_CURRENT,
        .name = "ams_current",
        .priority = AMS_PRIO_CURRENT,
        .period_ms = AMS_PERIOD_CURRENT_MS,
        .thread = &current_thread,
        .stack = current_stack,
        .stack_size = K_THREAD_STACK_SIZEOF(current_stack),
        .stat = &runtime_stats[AMS_THREAD_CURRENT],
    },

    [AMS_THREAD_ADBMS] = {
        .id = AMS_THREAD_ADBMS,
        .name = "ams_adbms",
        .priority = AMS_PRIO_ADBMS,
        .period_ms = AMS_PERIOD_ADBMS_MS,
        .thread = &adbms_thread,
        .stack = adbms_stack,
        .stack_size = K_THREAD_STACK_SIZEOF(adbms_stack),
        .stat = &runtime_stats[AMS_THREAD_ADBMS],
    },

    [AMS_THREAD_CAN] = {
        .id = AMS_THREAD_CAN,
        .name = "ams_can",
        .priority = AMS_PRIO_CAN,
        .period_ms = AMS_PERIOD_CAN_MS,
        .thread = &can_thread,
        .stack = can_stack,
        .stack_size = K_THREAD_STACK_SIZEOF(can_stack),
        .stat = &runtime_stats[AMS_THREAD_CAN],
    },

    [AMS_THREAD_ESTIMATOR] = {
        .id = AMS_THREAD_ESTIMATOR,
        .name = "ams_estimator",
        .priority = AMS_PRIO_ESTIMATOR,
        .period_ms = AMS_PERIOD_ESTIMATOR_MS,
        .thread = &estimator_thread,
        .stack = estimator_stack,
        .stack_size = K_THREAD_STACK_SIZEOF(estimator_stack),
        .stat = &runtime_stats[AMS_THREAD_ESTIMATOR],
    },

    [AMS_THREAD_FAN] = {
        .id = AMS_THREAD_FAN,
        .name = "ams_fan",
        .priority = AMS_PRIO_FAN,
        .period_ms = AMS_PERIOD_FAN_MS,
        .thread = &fan_thread,
        .stack = fan_stack,
        .stack_size = K_THREAD_STACK_SIZEOF(fan_stack),
        .stat = &runtime_stats[AMS_THREAD_FAN],
    },

    [AMS_THREAD_AIR] = {
        .id = AMS_THREAD_AIR,
        .name = "ams_air",
        .priority = AMS_PRIO_AIR,
        .period_ms = AMS_PERIOD_AIR_MS,
        .thread = &air_thread,
        .stack = air_stack,
        .stack_size = K_THREAD_STACK_SIZEOF(air_stack),
        .stat = &runtime_stats[AMS_THREAD_AIR],
    },

    [AMS_THREAD_IMD] = {
        .id = AMS_THREAD_IMD,
        .name = "ams_imd",
        .priority = AMS_PRIO_IMD,
        .period_ms = AMS_PERIOD_IMD_MS,
        .thread = &imd_thread,
        .stack = imd_stack,
        .stack_size = K_THREAD_STACK_SIZEOF(imd_stack),
        .stat = &runtime_stats[AMS_THREAD_IMD],
    },

    [AMS_THREAD_DIAGNOSTICS] = {
        .id = AMS_THREAD_DIAGNOSTICS,
        .name = "ams_diag",
        .priority = AMS_PRIO_DIAGNOSTICS,
        .period_ms = AMS_PERIOD_DIAGNOSTICS_MS,
        .thread = &diagnostics_thread,
        .stack = diagnostics_stack,
        .stack_size = K_THREAD_STACK_SIZEOF(diagnostics_stack),
        .stat = &runtime_stats[AMS_THREAD_DIAGNOSTICS],
    },
};


static void runtime_publish_start(struct ams_thread_descriptor *thread,
                                  int64_t scheduled_release,
                                  int64_t actual_start)
{
    int64_t lateness_ms;

    atomic_set(&thread->stat->scheduled_release_ms,
               (atomic_val_t)(uint32_t)scheduled_release);

    atomic_set(&thread->stat->last_start_ms,
               (atomic_val_t)(uint32_t)actual_start);

    lateness_ms = actual_start - scheduled_release;

    /*
     * A release is counted as missed only if execution begins an entire
     * period late. Ordinary scheduler jitter is not treated as a miss.
     */
    if (lateness_ms >= (int64_t)thread->period_ms) {
        atomic_inc(&thread->stat->release_miss_count);
    }
}


static void runtime_publish_complete(struct ams_thread_descriptor *thread,
                                     int64_t completion)
{
    atomic_set(&thread->stat->last_complete_ms,
               (atomic_val_t)(uint32_t)completion);

    atomic_inc(&thread->stat->heartbeat_seq);
}


static void periodic_placeholder_thread(void *p1, void *p2, void *p3)
{
    struct ams_thread_descriptor *thread = p1;
    int64_t release_ms;

    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    release_ms = k_uptime_get();

    for (;;) {
        int64_t start_ms;
        int64_t complete_ms;
        int64_t next_release_ms;

        /*
         * Absolute release timing prevents execution-time drift from being
         * accumulated into the next period.
         */
        k_sleep(K_TIMEOUT_ABS_MS(release_ms));

        start_ms = k_uptime_get();

        runtime_publish_start(thread, release_ms, start_ms);

        /*
         * ------------------------------------------------------------------
         * Z-004 PLACEHOLDER ONLY
         * ------------------------------------------------------------------
         *
         * Real subsystem work is intentionally absent.
         */

        complete_ms = k_uptime_get();

        runtime_publish_complete(thread, complete_ms);

        next_release_ms = release_ms + thread->period_ms;

        if (complete_ms >= next_release_ms) {
            atomic_inc(&thread->stat->overrun_count);

            /*
             * Do not execute a burst of stale catch-up cycles.
             * Advance to the first future absolute release.
             */
            do {
                next_release_ms += thread->period_ms;
            } while (complete_ms >= next_release_ms);
        }

        release_ms = next_release_ms;
    }
}


static void runtime_update_stale_flags(void)
{
    uint32_t now_ms = k_uptime_get_32();

    /*
     * Safety-supervisor liveness is handled later by the watchdog/supervisor
     * integration. Diagnostics is event-driven and therefore not aged here.
     */
    for (size_t i = AMS_THREAD_CURRENT;
         i < AMS_THREAD_DIAGNOSTICS;
         ++i) {
        struct ams_thread_descriptor *thread = &threads[i];
        uint32_t last_complete;
        uint32_t heartbeat;
        uint32_t max_age_ms;
        uint32_t age_ms;

        heartbeat =
            (uint32_t)atomic_get(&thread->stat->heartbeat_seq);

        last_complete =
            (uint32_t)atomic_get(&thread->stat->last_complete_ms);

        /*
         * Give every thread at least three scheduled periods before calling
         * it stale.
         */
        max_age_ms = thread->period_ms * 3U;

        if (heartbeat == 0U) {
            atomic_set(&thread->stat->stale, 1);
            continue;
        }

        /*
         * Unsigned subtraction intentionally gives wrap-safe 32-bit age.
         */
        age_ms = now_ms - last_complete;

        atomic_set(&thread->stat->stale,
                   (age_ms > max_age_ms) ? 1 : 0);
    }
}


static void safety_supervisor_thread(void *p1, void *p2, void *p3)
{
    struct ams_thread_descriptor *thread = p1;
    int64_t release_ms;

    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    release_ms = k_uptime_get();

    for (;;) {
        int64_t start_ms;
        int64_t complete_ms;
        int64_t next_release_ms;

        k_sleep(K_TIMEOUT_ABS_MS(release_ms));

        start_ms = k_uptime_get();

        runtime_publish_start(thread, release_ms, start_ms);

        if (atomic_get(&runtime_started) != 0) {
            runtime_update_stale_flags();
        }

        /*
         * No authority decision is made here in Z-004.
         *
         * The supervisor currently observes runtime liveness only.
         * BMS_OK remains structurally impossible to assert.
         */

        complete_ms = k_uptime_get();

        runtime_publish_complete(thread, complete_ms);

        next_release_ms = release_ms + thread->period_ms;

        if (complete_ms >= next_release_ms) {
            atomic_inc(&thread->stat->overrun_count);

            do {
                next_release_ms += thread->period_ms;
            } while (complete_ms >= next_release_ms);
        }

        release_ms = next_release_ms;
    }
}


static void diagnostics_thread_entry(void *p1, void *p2, void *p3)
{
    struct ams_thread_descriptor *thread = p1;

    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    for (;;) {
        k_sem_take(&diagnostics_request, K_FOREVER);

        atomic_set(&thread->stat->last_start_ms,
                   (atomic_val_t)k_uptime_get_32());

        printk("\nAMS Z-004 runtime\n");
        printk("thread           prio period hb     stale stack-free\n");

        for (size_t i = 0U; i < AMS_THREAD_COUNT; ++i) {
            struct ams_thread_snapshot snapshot;

            if (ams_thread_snapshot_get(
                    (enum ams_thread_id)i,
                    &snapshot) != 0) {
                continue;
            }

            printk("%-16s %4d %6u %6u %5u %5u/%u\n",
                   snapshot.name,
                   snapshot.priority,
                   snapshot.period_ms,
                   snapshot.heartbeat_seq,
                   snapshot.stale ? 1U : 0U,
                   (unsigned int)snapshot.stack_unused,
                   (unsigned int)snapshot.stack_size);
        }

        atomic_set(&thread->stat->last_complete_ms,
                   (atomic_val_t)k_uptime_get_32());

        atomic_inc(&thread->stat->heartbeat_seq);
    }
}


static k_tid_t create_thread(struct ams_thread_descriptor *thread,
                             k_thread_entry_t entry)
{
    k_tid_t tid;

    tid = k_thread_create(
        thread->thread,
        thread->stack,
        thread->stack_size,
        entry,
        thread,
        NULL,
        NULL,
        thread->priority,
        0,
        K_FOREVER);

    if (tid != NULL) {
        (void)k_thread_name_set(tid, thread->name);
    }

    return tid;
}


int ams_threads_start(void)
{
    /*
     * Create every thread suspended first.
     *
     * This prevents the highest-priority supervisor from running while the
     * rest of the runtime topology is only partially constructed.
     */

    if (create_thread(&threads[AMS_THREAD_SAFETY],
                      safety_supervisor_thread) == NULL) {
        return -ENOMEM;
    }

    if (create_thread(&threads[AMS_THREAD_CURRENT],
                      periodic_placeholder_thread) == NULL) {
        return -ENOMEM;
    }

    if (create_thread(&threads[AMS_THREAD_ADBMS],
                      periodic_placeholder_thread) == NULL) {
        return -ENOMEM;
    }

    if (create_thread(&threads[AMS_THREAD_CAN],
                      periodic_placeholder_thread) == NULL) {
        return -ENOMEM;
    }

    if (create_thread(&threads[AMS_THREAD_ESTIMATOR],
                      periodic_placeholder_thread) == NULL) {
        return -ENOMEM;
    }

    if (create_thread(&threads[AMS_THREAD_FAN],
                      periodic_placeholder_thread) == NULL) {
        return -ENOMEM;
    }

    /*
     * AIR is explicitly created here.
     *
     * Do not remove this because it did not share the normal FreeRTOS
     * CMSIS task-creation path in the oracle.
     */
    if (create_thread(&threads[AMS_THREAD_AIR],
                      periodic_placeholder_thread) == NULL) {
        return -ENOMEM;
    }

    if (create_thread(&threads[AMS_THREAD_IMD],
                      periodic_placeholder_thread) == NULL) {
        return -ENOMEM;
    }

    if (create_thread(&threads[AMS_THREAD_DIAGNOSTICS],
                      diagnostics_thread_entry) == NULL) {
        return -ENOMEM;
    }


    /*
     * Start ordinary workers before the supervisor.
     */
    k_thread_start(threads[AMS_THREAD_CURRENT].thread);
    k_thread_start(threads[AMS_THREAD_ADBMS].thread);
    k_thread_start(threads[AMS_THREAD_CAN].thread);
    k_thread_start(threads[AMS_THREAD_ESTIMATOR].thread);
    k_thread_start(threads[AMS_THREAD_FAN].thread);
    k_thread_start(threads[AMS_THREAD_AIR].thread);
    k_thread_start(threads[AMS_THREAD_IMD].thread);
    k_thread_start(threads[AMS_THREAD_DIAGNOSTICS].thread);

    atomic_set(&runtime_started, 1);

    /*
     * Supervisor starts last so its first liveness scan sees a complete
     * runtime topology.
     */
    k_thread_start(threads[AMS_THREAD_SAFETY].thread);

    return 0;
}


int ams_thread_snapshot_get(enum ams_thread_id id,
                            struct ams_thread_snapshot *snapshot)
{
    struct ams_thread_descriptor *thread;
    size_t unused = 0U;
    int ret;

    if ((id < 0) ||
        (id >= AMS_THREAD_COUNT) ||
        (snapshot == NULL)) {
        return -EINVAL;
    }

    thread = &threads[id];

    ret = k_thread_stack_space_get(thread->thread, &unused);

    if (ret != 0) {
        unused = 0U;
    }

    snapshot->name = thread->name;
    snapshot->priority = thread->priority;
    snapshot->period_ms = thread->period_ms;

    snapshot->heartbeat_seq =
        (uint32_t)atomic_get(&thread->stat->heartbeat_seq);

    snapshot->scheduled_release_ms =
        (uint32_t)atomic_get(
            &thread->stat->scheduled_release_ms);

    snapshot->last_start_ms =
        (uint32_t)atomic_get(
            &thread->stat->last_start_ms);

    snapshot->last_complete_ms =
        (uint32_t)atomic_get(
            &thread->stat->last_complete_ms);

    snapshot->release_miss_count =
        (uint32_t)atomic_get(
            &thread->stat->release_miss_count);

    snapshot->overrun_count =
        (uint32_t)atomic_get(
            &thread->stat->overrun_count);

    snapshot->stale =
        atomic_get(&thread->stat->stale) != 0;

    snapshot->stack_size = thread->stack_size;
    snapshot->stack_unused = unused;

    return 0;
}


void ams_threads_request_diagnostics(void)
{
    k_sem_give(&diagnostics_request);
}


size_t ams_threads_count(void)
{
    return ARRAY_SIZE(threads);
}