#include "ams_threads.h"

#include <errno.h>
#include <stdint.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>


/*
 * --------------------------------------------------------------------------
 * Z-010 AMS runtime contract
 * --------------------------------------------------------------------------
 *
 * This remains a PLATFORM/RUNTIME skeleton only.
 *
 * No thread below currently:
 * - reads current ADCs
 * - accesses ADBMS SPI
 * - transmits or receives CAN
 * - executes estimator algorithms
 * - commands fans
 * - commands balancing
 * - asserts BMS_OK
 */


/*
 * --------------------------------------------------------------------------
 * v2.6.27 / FW0.5.30 runtime-policy parity
 * --------------------------------------------------------------------------
 *
 * The numeric priority values differ because Zephyr uses a smaller number for
 * a higher preemptible priority, while FreeRTOS uses a larger number for a
 * higher priority.  The relative safety ordering is intentionally identical:
 *
 *   safety > current > ADBMS > CAN > estimator > fan/AIR > IMD > diagnostics
 *
 * The release periods and heartbeat timeouts below are copied from the frozen
 * v2.6.27 app.h policy.  Do not replace them with generic "N periods" rules:
 * several oracle heartbeat windows are intentionally much wider than their
 * task periods to tolerate bounded diagnostic/acquisition work without
 * fabricating a software-liveness fault.
 */

/* Zephyr preemptible priority ordering. */
#define AMS_PRIO_SAFETY       0
#define AMS_PRIO_CURRENT      2
#define AMS_PRIO_ADBMS        3
#define AMS_PRIO_CAN          4
#define AMS_PRIO_ESTIMATOR    6
#define AMS_PRIO_FAN          8
#define AMS_PRIO_AIR          8
#define AMS_PRIO_IMD          9
#define AMS_PRIO_DIAGNOSTICS 12

/* Exact normal-rate task periods from the v2.6.27 oracle. */
#define AMS_PERIOD_SAFETY_MS       50U
#define AMS_PERIOD_CURRENT_MS      20U
#define AMS_PERIOD_ADBMS_MS       100U
#define AMS_PERIOD_CAN_MS         100U
#define AMS_PERIOD_ESTIMATOR_MS   100U
#define AMS_PERIOD_FAN_MS         200U
#define AMS_PERIOD_AIR_MS         500U
#define AMS_PERIOD_IMD_MS         100U
#define AMS_PERIOD_DIAGNOSTICS_MS   0U

/*
 * Exact v2.6.27 heartbeat policy.
 *
 * AIR has no heartbeat bit in the oracle. Diagnostics here is not the legacy
 * logger task, so neither is allowed to masquerade as safety-liveness proof.
 * The separate temperature heartbeat belongs to the future ADBMS acquisition
 * integration and is frozen here even though Z-010 does not yet produce it.
 */
#define AMS_HEARTBEAT_STARTUP_GRACE_MS       3000U
#define AMS_HEARTBEAT_ADBMS_TIMEOUT_MS       3000U
#define AMS_HEARTBEAT_CURRENT_TIMEOUT_MS      200U
#define AMS_HEARTBEAT_TEMP_TIMEOUT_MS        3000U
#define AMS_HEARTBEAT_CAN_TIMEOUT_MS         2000U
#define AMS_HEARTBEAT_LOGGER_TIMEOUT_MS      2000U
#define AMS_HEARTBEAT_IMD_TIMEOUT_MS          500U
#define AMS_HEARTBEAT_FAN_TIMEOUT_MS         1000U
#define AMS_HEARTBEAT_ESTIMATOR_TIMEOUT_MS    500U

#define AMS_STALE_SAFETY_MS          0U
#define AMS_STALE_CURRENT_MS         200U
#define AMS_STALE_ADBMS_MS          3000U
#define AMS_STALE_CAN_MS            2000U
#define AMS_STALE_ESTIMATOR_MS       500U
#define AMS_STALE_FAN_MS            1000U
#define AMS_STALE_AIR_MS             0U
#define AMS_STALE_IMD_MS             500U
#define AMS_STALE_DIAGNOSTICS_MS     0U

#define AMS_STARTUP_SAFETY_MS        0U
#define AMS_STARTUP_CURRENT_MS      3000U
#define AMS_STARTUP_ADBMS_MS        3000U
#define AMS_STARTUP_CAN_MS          3000U
#define AMS_STARTUP_ESTIMATOR_MS    3000U
#define AMS_STARTUP_FAN_MS          3000U
#define AMS_STARTUP_AIR_MS           0U
#define AMS_STARTUP_IMD_MS          3000U
#define AMS_STARTUP_DIAGNOSTICS_MS   0U

/*
 * Lock wait bounds are not used until the corresponding adapters are wired,
 * but freezing them now prevents a later migration from silently converting
 * a bounded fail-low wait into an unbounded deadlock.
 */
#define AMS_ADBMS_MUTEX_TIMEOUT_MS          500U
#define AMS_CURRENT_WINDOW_MUTEX_TIMEOUT_MS  20U

/*
 * Current Z-010 migration profile is stricter than the FreeRTOS vehicle
 * profile: physical authority is compile-time disabled and the physical IMD
 * and AIR auxiliary adapters do not exist yet.
 *
 * The v2.6.27 default/bench behavior does not start the legacy AIR task when
 * AMS_ENABLE_AIR_AUX_FEEDBACK=0.  Likewise, IMD is not started in the bench
 * profile.  Keep those placeholder objects present for topology/stack review,
 * but do not run them or count their no-op loops as liveness evidence.
 */
#define AMS_RUNTIME_AIR_ENABLED 0U
#define AMS_RUNTIME_IMD_ENABLED 0U

/*
 * Estimator heartbeat is safety-critical in v2.6.27 only when SoP authority
 * is required. Z-010 has no BMS/SoP authority, so it remains diagnostic only.
 */
#define AMS_RUNTIME_ESTIMATOR_SAFETY_REQUIRED 0U

/*
 * Zephyr stacks remain intentionally larger than the FreeRTOS byte counts
 * until target stack-watermark evidence exists.  The oracle sizes are kept as
 * lower-bound contracts, not as targets to shrink toward during migration.
 */
#define AMS_ORACLE_STACK_SAFETY_BYTES       1024U
#define AMS_ORACLE_STACK_CURRENT_BYTES      1024U
#define AMS_ORACLE_STACK_ADBMS_BYTES        6144U
#define AMS_ORACLE_STACK_CAN_BYTES          6144U
#define AMS_ORACLE_STACK_ESTIMATOR_BYTES    6144U
#define AMS_ORACLE_STACK_FAN_BYTES           768U
#define AMS_ORACLE_STACK_AIR_BYTES           768U
#define AMS_ORACLE_STACK_IMD_BYTES           768U
#define AMS_ORACLE_STACK_DIAGNOSTICS_BYTES  2048U

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
             "AMS runtime requires at least 13 preemptible priorities");

/* Preserve the v2.6.27 relative safety-priority policy. */
BUILD_ASSERT(AMS_PRIO_SAFETY < AMS_PRIO_CURRENT,
             "safety supervisor must outrank current");
BUILD_ASSERT(AMS_PRIO_CURRENT < AMS_PRIO_ADBMS,
             "current must outrank ADBMS");
BUILD_ASSERT(AMS_PRIO_ADBMS < AMS_PRIO_CAN,
             "ADBMS must outrank CAN");
BUILD_ASSERT(AMS_PRIO_CAN < AMS_PRIO_ESTIMATOR,
             "CAN must outrank estimator");
BUILD_ASSERT(AMS_PRIO_ESTIMATOR < AMS_PRIO_FAN,
             "estimator must outrank fan");
BUILD_ASSERT(AMS_PRIO_FAN == AMS_PRIO_AIR,
             "fan and AIR must retain equal priority");
BUILD_ASSERT(AMS_PRIO_FAN < AMS_PRIO_IMD,
             "fan/AIR must outrank IMD");
BUILD_ASSERT(AMS_PRIO_IMD < AMS_PRIO_DIAGNOSTICS,
             "IMD must outrank diagnostics");

/* Never shrink below the reviewed FreeRTOS allocations without evidence. */
BUILD_ASSERT(AMS_STACK_SAFETY >= AMS_ORACLE_STACK_SAFETY_BYTES,
             "safety stack below v2.6.27 allocation");
BUILD_ASSERT(AMS_STACK_CURRENT >= AMS_ORACLE_STACK_CURRENT_BYTES,
             "current stack below v2.6.27 allocation");
BUILD_ASSERT(AMS_STACK_ADBMS >= AMS_ORACLE_STACK_ADBMS_BYTES,
             "ADBMS stack below v2.6.27 allocation");
BUILD_ASSERT(AMS_STACK_CAN >= AMS_ORACLE_STACK_CAN_BYTES,
             "CAN stack below v2.6.27 allocation");
BUILD_ASSERT(AMS_STACK_ESTIMATOR >= AMS_ORACLE_STACK_ESTIMATOR_BYTES,
             "estimator stack below v2.6.27 allocation");
BUILD_ASSERT(AMS_STACK_FAN >= AMS_ORACLE_STACK_FAN_BYTES,
             "fan stack below v2.6.27 allocation");
BUILD_ASSERT(AMS_STACK_AIR >= AMS_ORACLE_STACK_AIR_BYTES,
             "AIR stack below v2.6.27 allocation");
BUILD_ASSERT(AMS_STACK_IMD >= AMS_ORACLE_STACK_IMD_BYTES,
             "IMD stack below v2.6.27 allocation");
BUILD_ASSERT(AMS_STACK_DIAGNOSTICS >= AMS_ORACLE_STACK_DIAGNOSTICS_BYTES,
             "diagnostics stack below v2.6.27 CLI allocation");

struct ams_runtime_stat {
    atomic_t heartbeat_seq;

    atomic_t scheduled_release_ms;
    atomic_t last_start_ms;
    atomic_t last_complete_ms;

    atomic_t last_lateness_ms;
    atomic_t max_lateness_ms;

    atomic_t release_miss_count;
    atomic_t overrun_count;

    atomic_t last_exec_us;
    atomic_t wcet_us;

    atomic_t stale;
};


struct ams_thread_descriptor {
    enum ams_thread_id id;

    const char *name;

    int priority;

    uint32_t period_ms;
    uint32_t stale_deadline_ms;
    uint32_t startup_grace_ms;

    bool enabled;
    bool safety_heartbeat_required;
    bool safety_evidence_ready;

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


static struct ams_runtime_stat runtime_stats[AMS_THREAD_COUNT];

K_SEM_DEFINE(diagnostics_request, 0, 1);

static atomic_t runtime_started;
static atomic_t runtime_start_ms;


/*
 * Explicit application execution topology.
 *
 * AIR remains first-class here because the FreeRTOS oracle created AIR
 * through its own direct xTaskCreateStatic() path.
 */
static struct ams_thread_descriptor threads[AMS_THREAD_COUNT] = {
    [AMS_THREAD_SAFETY] = {
        .id = AMS_THREAD_SAFETY,
        .name = "ams_safety",
        .priority = AMS_PRIO_SAFETY,
        .period_ms = AMS_PERIOD_SAFETY_MS,
        .stale_deadline_ms = AMS_STALE_SAFETY_MS,
        .startup_grace_ms = AMS_STARTUP_SAFETY_MS,
        .enabled = true,
        .safety_heartbeat_required = false,
        .safety_evidence_ready = false,
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
        .stale_deadline_ms = AMS_STALE_CURRENT_MS,
        .startup_grace_ms = AMS_STARTUP_CURRENT_MS,
        .enabled = true,
        .safety_heartbeat_required = true,
        .safety_evidence_ready = false,
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
        .stale_deadline_ms = AMS_STALE_ADBMS_MS,
        .startup_grace_ms = AMS_STARTUP_ADBMS_MS,
        .enabled = true,
        .safety_heartbeat_required = true,
        .safety_evidence_ready = false,
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
        .stale_deadline_ms = AMS_STALE_CAN_MS,
        .startup_grace_ms = AMS_STARTUP_CAN_MS,
        .enabled = true,
        .safety_heartbeat_required = true,
        .safety_evidence_ready = false,
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
        .stale_deadline_ms = AMS_STALE_ESTIMATOR_MS,
        .startup_grace_ms = AMS_STARTUP_ESTIMATOR_MS,
        .enabled = true,
        .safety_heartbeat_required = AMS_RUNTIME_ESTIMATOR_SAFETY_REQUIRED != 0U,
        .safety_evidence_ready = false,
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
        .stale_deadline_ms = AMS_STALE_FAN_MS,
        .startup_grace_ms = AMS_STARTUP_FAN_MS,
        .enabled = true,
        .safety_heartbeat_required = true,
        .safety_evidence_ready = false,
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
        .stale_deadline_ms = AMS_STALE_AIR_MS,
        .startup_grace_ms = AMS_STARTUP_AIR_MS,
        .enabled = AMS_RUNTIME_AIR_ENABLED != 0U,
        .safety_heartbeat_required = false,
        .safety_evidence_ready = false,
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
        .stale_deadline_ms = AMS_STALE_IMD_MS,
        .startup_grace_ms = AMS_STARTUP_IMD_MS,
        .enabled = AMS_RUNTIME_IMD_ENABLED != 0U,
        .safety_heartbeat_required = AMS_RUNTIME_IMD_ENABLED != 0U,
        .safety_evidence_ready = false,
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
        .stale_deadline_ms = AMS_STALE_DIAGNOSTICS_MS,
        .startup_grace_ms = AMS_STARTUP_DIAGNOSTICS_MS,
        .enabled = true,
        .safety_heartbeat_required = false,
        .safety_evidence_ready = false,
        .thread = &diagnostics_thread,
        .stack = diagnostics_stack,
        .stack_size = K_THREAD_STACK_SIZEOF(diagnostics_stack),
        .stat = &runtime_stats[AMS_THREAD_DIAGNOSTICS],
    },
};


static void atomic_update_max_u32(atomic_t *target, uint32_t value)
{
    atomic_val_t observed;

    observed = atomic_get(target);

    while (value > (uint32_t)observed) {
        if (atomic_cas(target, observed, (atomic_val_t)value)) {
            break;
        }

        observed = atomic_get(target);
    }
}


static void runtime_publish_start(struct ams_thread_descriptor *thread,
                                  int64_t scheduled_release,
                                  int64_t actual_start)
{
    uint32_t lateness_ms = 0U;

    atomic_set(&thread->stat->scheduled_release_ms,
               (atomic_val_t)(uint32_t)scheduled_release);

    atomic_set(&thread->stat->last_start_ms,
               (atomic_val_t)(uint32_t)actual_start);

    if (actual_start > scheduled_release) {
        int64_t lateness = actual_start - scheduled_release;

        if (lateness > (int64_t)UINT32_MAX) {
            lateness_ms = UINT32_MAX;
        } else {
            lateness_ms = (uint32_t)lateness;
        }
    }

    atomic_set(&thread->stat->last_lateness_ms,
               (atomic_val_t)lateness_ms);

    atomic_update_max_u32(&thread->stat->max_lateness_ms,
                          lateness_ms);

    /*
     * An ordinary small scheduler delay is not a release miss.
     * Missing an entire nominal period is.
     */
    if ((thread->period_ms != 0U) &&
        (lateness_ms >= thread->period_ms)) {
        atomic_inc(&thread->stat->release_miss_count);
    }
}


static void runtime_publish_complete(struct ams_thread_descriptor *thread,
                                     int64_t completion,
                                     uint32_t exec_us)
{
    atomic_set(&thread->stat->last_complete_ms,
               (atomic_val_t)(uint32_t)completion);

    atomic_set(&thread->stat->last_exec_us,
               (atomic_val_t)exec_us);

    atomic_update_max_u32(&thread->stat->wcet_us,
                          exec_us);

    atomic_inc(&thread->stat->heartbeat_seq);
}


static void periodic_placeholder_thread(void *p1,
                                        void *p2,
                                        void *p3)
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

        uint32_t start_cycles;
        uint32_t elapsed_cycles;
        uint32_t exec_us;

        /*
         * Absolute scheduling avoids cumulative period drift.
         */
        k_sleep(K_TIMEOUT_ABS_MS(release_ms));

        start_ms = k_uptime_get();
        start_cycles = k_cycle_get_32();

        runtime_publish_start(thread,
                              release_ms,
                              start_ms);

        /*
         * ------------------------------------------------------------------
         * Z-005 PLACEHOLDER
         * ------------------------------------------------------------------
         *
         * The actual subsystem body will be inserted here during its own
         * migration phase.
         */

        elapsed_cycles =
            k_cycle_get_32() - start_cycles;

        exec_us =
            k_cyc_to_us_floor32(elapsed_cycles);

        complete_ms = k_uptime_get();

        runtime_publish_complete(thread,
                                 complete_ms,
                                 exec_us);

        next_release_ms =
            release_ms + thread->period_ms;

        if (complete_ms >= next_release_ms) {
            atomic_inc(&thread->stat->overrun_count);

            /*
             * Skip missed historical releases instead of producing a burst
             * of catch-up executions.
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
    uint32_t now_ms;
    uint32_t started_ms;

    now_ms = k_uptime_get_32();

    started_ms =
        (uint32_t)atomic_get(&runtime_start_ms);

    /*
     * The safety thread does not diagnose its own death.
     * That responsibility moves to the watchdog/external supervisor layer.
     *
     * Diagnostics is event-driven and therefore has no periodic stale
     * deadline.
     */
    for (size_t i = AMS_THREAD_CURRENT;
         i < AMS_THREAD_DIAGNOSTICS;
         ++i) {
        struct ams_thread_descriptor *thread;
        uint32_t heartbeat;
        uint32_t last_complete;
        uint32_t age_ms;
        uint32_t startup_age_ms;

        thread = &threads[i];

        if (!thread->enabled || (thread->stale_deadline_ms == 0U)) {
            atomic_set(&thread->stat->stale, 0);
            continue;
        }

        heartbeat =
            (uint32_t)atomic_get(
                &thread->stat->heartbeat_seq);

        last_complete =
            (uint32_t)atomic_get(
                &thread->stat->last_complete_ms);

        startup_age_ms =
            now_ms - started_ms;

        if (heartbeat == 0U) {
            /*
             * A newly-created thread is not stale until its explicit startup
             * grace has elapsed.
             */
            atomic_set(
                &thread->stat->stale,
                (startup_age_ms > thread->startup_grace_ms) ? 1 : 0);

            continue;
        }

        /*
         * Unsigned subtraction provides wrap-safe 32-bit elapsed age.
         */
        age_ms =
            now_ms - last_complete;

        atomic_set(
            &thread->stat->stale,
            (age_ms > thread->stale_deadline_ms) ? 1 : 0);
    }
}


static void safety_supervisor_thread(void *p1,
                                     void *p2,
                                     void *p3)
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

        uint32_t start_cycles;
        uint32_t elapsed_cycles;
        uint32_t exec_us;

        k_sleep(K_TIMEOUT_ABS_MS(release_ms));

        start_ms = k_uptime_get();
        start_cycles = k_cycle_get_32();

        runtime_publish_start(thread,
                              release_ms,
                              start_ms);

        if (atomic_get(&runtime_started) != 0) {
            runtime_update_stale_flags();
        }

        /*
         * Z-005 supervisor is observational only.
         *
         * It has no BMS_OK assertion authority.
         */

        elapsed_cycles =
            k_cycle_get_32() - start_cycles;

        exec_us =
            k_cyc_to_us_floor32(elapsed_cycles);

        complete_ms = k_uptime_get();

        runtime_publish_complete(thread,
                                 complete_ms,
                                 exec_us);

        next_release_ms =
            release_ms + thread->period_ms;

        if (complete_ms >= next_release_ms) {
            atomic_inc(&thread->stat->overrun_count);

            do {
                next_release_ms += thread->period_ms;
            } while (complete_ms >= next_release_ms);
        }

        release_ms = next_release_ms;
    }
}


static void diagnostics_thread_entry(void *p1,
                                     void *p2,
                                     void *p3)
{
    struct ams_thread_descriptor *thread = p1;

    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    for (;;) {
        uint32_t start_cycles;
        uint32_t elapsed_cycles;
        uint32_t exec_us;

        k_sem_take(&diagnostics_request,
                   K_FOREVER);

        atomic_set(
            &thread->stat->last_start_ms,
            (atomic_val_t)k_uptime_get_32());

        start_cycles = k_cycle_get_32();

        printk("\nAMS Z-010 runtime snapshot\n");
        printk("thread           en sf ev p  per age stale late maxL exec wcet stack-used\n");

        for (size_t i = 0U;
             i < AMS_THREAD_COUNT;
             ++i) {
            struct ams_thread_snapshot snapshot;

            if (ams_thread_snapshot_get(
                    (enum ams_thread_id)i,
                    &snapshot) != 0) {
                continue;
            }

            printk(
                "%-16s %2u %2u %2u %2d %4u %4u %5u %4u %4u %4u %4u %5u/%u\n",
                snapshot.name,
                snapshot.enabled ? 1U : 0U,
                snapshot.safety_heartbeat_required ? 1U : 0U,
                snapshot.safety_evidence_ready ? 1U : 0U,
                snapshot.priority,
                snapshot.period_ms,
                snapshot.heartbeat_age_ms,
                snapshot.stale ? 1U : 0U,
                snapshot.last_lateness_ms,
                snapshot.max_lateness_ms,
                snapshot.last_exec_us,
                snapshot.wcet_us,
                (unsigned int)snapshot.stack_used_high_water,
                (unsigned int)snapshot.stack_size);
        }

        elapsed_cycles =
            k_cycle_get_32() - start_cycles;

        exec_us =
            k_cyc_to_us_floor32(elapsed_cycles);

        runtime_publish_complete(
            thread,
            k_uptime_get(),
            exec_us);
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
        (void)k_thread_name_set(
            tid,
            thread->name);
    }

    return tid;
}


void ams_threads_print_manifest(void)
{
    printk("\nAMS runtime manifest\n");
    printk("thread           en sf ev prio period stale grace stack\n");

    for (size_t i = 0U;
         i < AMS_THREAD_COUNT;
         ++i) {
        const struct ams_thread_descriptor *thread;

        thread = &threads[i];

        printk("%-16s %2u %2u %2u %4d %6u %5u %5u %5u\n",
               thread->name,
               thread->enabled ? 1U : 0U,
               thread->safety_heartbeat_required ? 1U : 0U,
               thread->safety_evidence_ready ? 1U : 0U,
               thread->priority,
               thread->period_ms,
               thread->stale_deadline_ms,
               thread->startup_grace_ms,
               (unsigned int)thread->stack_size);
    }
}


static void start_thread_if_enabled(enum ams_thread_id id)
{
    if (((unsigned int)id < (unsigned int)AMS_THREAD_COUNT) &&
        threads[id].enabled) {
        k_thread_start(threads[id].thread);
    }
}


int ams_threads_start(void)
{
    if (atomic_get(&runtime_started) != 0) {
        return -EALREADY;
    }

    /*
     * Create all thread objects suspended first.
     */
    if (create_thread(
            &threads[AMS_THREAD_SAFETY],
            safety_supervisor_thread) == NULL) {
        return -ENOMEM;
    }

    if (create_thread(
            &threads[AMS_THREAD_CURRENT],
            periodic_placeholder_thread) == NULL) {
        return -ENOMEM;
    }

    if (create_thread(
            &threads[AMS_THREAD_ADBMS],
            periodic_placeholder_thread) == NULL) {
        return -ENOMEM;
    }

    if (create_thread(
            &threads[AMS_THREAD_CAN],
            periodic_placeholder_thread) == NULL) {
        return -ENOMEM;
    }

    if (create_thread(
            &threads[AMS_THREAD_ESTIMATOR],
            periodic_placeholder_thread) == NULL) {
        return -ENOMEM;
    }

    if (create_thread(
            &threads[AMS_THREAD_FAN],
            periodic_placeholder_thread) == NULL) {
        return -ENOMEM;
    }

    /*
     * Explicit AIR creation is mandatory.
     */
    if (create_thread(
            &threads[AMS_THREAD_AIR],
            periodic_placeholder_thread) == NULL) {
        return -ENOMEM;
    }

    if (create_thread(
            &threads[AMS_THREAD_IMD],
            periodic_placeholder_thread) == NULL) {
        return -ENOMEM;
    }

    if (create_thread(
            &threads[AMS_THREAD_DIAGNOSTICS],
            diagnostics_thread_entry) == NULL) {
        return -ENOMEM;
    }

    /*
     * Record the single runtime epoch before any application thread runs.
     * v2.6.27 applies one 3000 ms startup grace to the monitored heartbeat
     * set, so the highest-priority supervisor can safely start first without
     * fabricating stale-worker faults during deterministic startup.
     */
    atomic_set(
        &runtime_start_ms,
        (atomic_val_t)k_uptime_get_32());

    atomic_set(&runtime_started, 1);

    /*
     * Match the safety architecture rather than the historical creation order:
     * the supervisor is the highest-priority application thread and is active
     * before lower-priority work begins.  BMS_OK still cannot be asserted in
     * Z-010 because assertion authority is compile-time forbidden.
     */
    start_thread_if_enabled(AMS_THREAD_SAFETY);

    start_thread_if_enabled(AMS_THREAD_CURRENT);
    start_thread_if_enabled(AMS_THREAD_ADBMS);
    start_thread_if_enabled(AMS_THREAD_CAN);
    start_thread_if_enabled(AMS_THREAD_ESTIMATOR);
    start_thread_if_enabled(AMS_THREAD_FAN);
    start_thread_if_enabled(AMS_THREAD_AIR);
    start_thread_if_enabled(AMS_THREAD_IMD);
    start_thread_if_enabled(AMS_THREAD_DIAGNOSTICS);

    return 0;
}


int ams_thread_snapshot_get(enum ams_thread_id id,
                            struct ams_thread_snapshot *snapshot)
{
    struct ams_thread_descriptor *thread;

    size_t unused = 0U;

    uint32_t now_ms;
    uint32_t runtime_epoch_ms;

    int ret;

    if ((id < 0) ||
        (id >= AMS_THREAD_COUNT) ||
        (snapshot == NULL)) {
        return -EINVAL;
    }

    thread = &threads[id];

    ret =
        k_thread_stack_space_get(
            thread->thread,
            &unused);

    if (ret != 0) {
        unused = 0U;
    }

    snapshot->name =
        thread->name;

    snapshot->priority =
        thread->priority;

    snapshot->period_ms =
        thread->period_ms;

    snapshot->stale_deadline_ms =
        thread->stale_deadline_ms;

    snapshot->startup_grace_ms =
        thread->startup_grace_ms;

    snapshot->enabled =
        thread->enabled;

    snapshot->safety_heartbeat_required =
        thread->safety_heartbeat_required;

    snapshot->safety_evidence_ready =
        thread->safety_evidence_ready;

    snapshot->heartbeat_seq =
        (uint32_t)atomic_get(
            &thread->stat->heartbeat_seq);

    snapshot->scheduled_release_ms =
        (uint32_t)atomic_get(
            &thread->stat->scheduled_release_ms);

    snapshot->last_start_ms =
        (uint32_t)atomic_get(
            &thread->stat->last_start_ms);

    snapshot->last_complete_ms =
        (uint32_t)atomic_get(
            &thread->stat->last_complete_ms);

    snapshot->last_lateness_ms =
        (uint32_t)atomic_get(
            &thread->stat->last_lateness_ms);

    snapshot->max_lateness_ms =
        (uint32_t)atomic_get(
            &thread->stat->max_lateness_ms);

    snapshot->release_miss_count =
        (uint32_t)atomic_get(
            &thread->stat->release_miss_count);

    snapshot->overrun_count =
        (uint32_t)atomic_get(
            &thread->stat->overrun_count);

    snapshot->last_exec_us =
        (uint32_t)atomic_get(
            &thread->stat->last_exec_us);

    snapshot->wcet_us =
        (uint32_t)atomic_get(
            &thread->stat->wcet_us);

    snapshot->stale =
        atomic_get(
            &thread->stat->stale) != 0;

    snapshot->stack_size =
        thread->stack_size;

    snapshot->stack_unused =
        unused;

    if (unused <= thread->stack_size) {
        snapshot->stack_used_high_water =
            thread->stack_size - unused;
    } else {
        snapshot->stack_used_high_water =
            thread->stack_size;
    }

    now_ms =
        k_uptime_get_32();

    runtime_epoch_ms =
        (uint32_t)atomic_get(
            &runtime_start_ms);

    if (!thread->enabled || (thread->stale_deadline_ms == 0U)) {
        snapshot->heartbeat_age_ms = 0U;
        snapshot->startup_grace_active = false;
    } else if (snapshot->heartbeat_seq == 0U) {
        snapshot->heartbeat_age_ms =
            now_ms - runtime_epoch_ms;

        snapshot->startup_grace_active =
            snapshot->heartbeat_age_ms <=
            thread->startup_grace_ms;
    } else {
        snapshot->heartbeat_age_ms =
            now_ms -
            snapshot->last_complete_ms;

        snapshot->startup_grace_active =
            false;
    }

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


size_t ams_threads_active_count(void)
{
    size_t active = 0U;

    for (size_t i = 0U; i < AMS_THREAD_COUNT; ++i) {
        if (threads[i].enabled) {
            active++;
        }
    }

    return active;
}


uint32_t ams_threads_runtime_start_ms(void)
{
    return (uint32_t)atomic_get(
        &runtime_start_ms);
}
