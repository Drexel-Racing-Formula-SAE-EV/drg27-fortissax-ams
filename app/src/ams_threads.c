#ifdef CONFIG_AMS_Z016_LINK_PROBE
#include <ams_platform/adbms_link_probe.h>
#endif
#include "ams_threads.h"

#include <ams_core/ams_fan_control.h>
#include <ams_core/ams_imd.h>
#include <ams_core/ams_stack_health.h>
#include <ams_core/ams_watchdog_policy.h>
#include <ams_core/ams_watchdog_heartbeat.h>

#include <ams_platform/fan_pwm.h>
#include <ams_platform/imd_capture.h>
#include <ams_platform/watchdog.h>
#include "ams_safety.h"
#include <ams_platform/fail_low.h>

#include <errno.h>
#include <stdint.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>


/*
 * --------------------------------------------------------------------------
 * Z-014 AMS runtime/watchdog contract
 * --------------------------------------------------------------------------
 *
 * Z-015 keeps the Z-014 watchdog/runtime safety boundary unchanged while the
 * private ADBMS SPI6 adapter is prepared at startup. The ADBMS thread is dormant in the base profile.
 * Z016's explicit profile executes its finite String B link probe in that
 * same thread; neither profile emits ADBMS heartbeat/safety evidence. IMD was promoted in Z-013
 * to the real 10 Hz PA5/TIM2 PWM-input + PC5 OK_HS workload. Current ADC
 * hardware remains initialized but the current worker still does not acquire
 * samples until Z-022 proves mutex/publication ordering.
 *
 * No thread below currently:
 * - reads current ADCs
 * - transmits or receives CAN
 * - executes estimator algorithms
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
 * integration and is frozen here even though Z-013 does not yet produce it.
 */
/* Exact watchdog heartbeat timing lives in the portable
 * ams_watchdog_heartbeat core. Generic runtime diagnostics below retain their
 * own per-thread stale metadata but do not authorize IWDG feeding. */

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
 * Z-014 remains compile-time no-authority. AIR auxiliary feedback still does
 * not exist on this hardware revision, so AIR stays disabled. IMD is now a
 * real migrated workload: enabling it here does NOT claim
 * AMS_IMD_TARGET_VALIDATED; it permits no-authority capture/diagnostic parity
 * testing only. Vehicle authority remains blocked until the physical IMD gate
 * is independently validated.
 */
#define AMS_RUNTIME_AIR_ENABLED 0U
#define AMS_RUNTIME_IMD_ENABLED 1U

/*
 * Estimator heartbeat is safety-critical in v2.6.27 only when SoP authority
 * is required. Z-014 has no BMS/SoP authority, so it remains diagnostic only.
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
BUILD_ASSERT(AMS_THREAD_COUNT <= 16U,
             "runtime stack/integrity masks are uint16_t");
BUILD_ASSERT(AMS_WATCHDOG_HEARTBEAT_COUNT <= 16U,
             "watchdog heartbeat masks are uint16_t");

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

/* Hidden migration capability symbols distinguish adapter presence, live actors,
 * valid safety evidence and physical validation. These are migration-state
 * facts, not user knobs. */
BUILD_ASSERT(IS_ENABLED(CONFIG_AMS_CAP_BMS_OK_PLATFORM_ADAPTER_PRESENT),
             "Z-014 requires normal BMS_OK platform adapter presence");
BUILD_ASSERT(IS_ENABLED(CONFIG_AMS_CAP_CURRENT_ADC_ADAPTER_PRESENT),
             "Z-014 requires current ADC adapter presence");
BUILD_ASSERT(!IS_ENABLED(CONFIG_AMS_CAP_CURRENT_ACTOR_LIVE),
             "current actor remains deferred at Z-014");
BUILD_ASSERT(!IS_ENABLED(CONFIG_AMS_CAP_CURRENT_SAFETY_EVIDENCE),
             "placeholder current must not be safety evidence");
BUILD_ASSERT(IS_ENABLED(CONFIG_AMS_CAP_ADBMS_SPI_ADAPTER_PRESENT),
             "Z-015 requires the private ADBMS SPI6 adapter");
BUILD_ASSERT(!IS_ENABLED(CONFIG_AMS_CAP_ADBMS_SPI_PHYSICAL_VALIDATED),
             "Z-015 must not claim physical SPI6 validation");
BUILD_ASSERT(!IS_ENABLED(CONFIG_AMS_CAP_ADBMS_ACTOR_LIVE) &&
             !IS_ENABLED(CONFIG_AMS_CAP_ADBMS_SAFETY_EVIDENCE),
             "ADBMS actor/evidence remain deferred at Z-015");
BUILD_ASSERT(!IS_ENABLED(CONFIG_AMS_CAP_TEMPERATURE_SAFETY_EVIDENCE),
             "temperature safety evidence remains deferred at Z-014");
BUILD_ASSERT(!IS_ENABLED(CONFIG_AMS_CAP_CAN_ADAPTER_PRESENT) &&
             !IS_ENABLED(CONFIG_AMS_CAP_CAN_ACTOR_LIVE) &&
             !IS_ENABLED(CONFIG_AMS_CAP_CAN_SAFETY_EVIDENCE),
             "CAN transport/actor/evidence remain deferred at Z-014");
BUILD_ASSERT(IS_ENABLED(CONFIG_AMS_CAP_FAN_PWM_ADAPTER_PRESENT) &&
             IS_ENABLED(CONFIG_AMS_CAP_FAN_ACTOR_LIVE) &&
             IS_ENABLED(CONFIG_AMS_CAP_FAN_SAFETY_EVIDENCE),
             "fan adapter/actor/evidence must remain live at Z-014");
BUILD_ASSERT(!IS_ENABLED(CONFIG_AMS_CAP_FAN_PHYSICAL_VALIDATED),
             "fan physical validation remains an open hardware gate");
BUILD_ASSERT(IS_ENABLED(CONFIG_AMS_CAP_IMD_CAPTURE_ADAPTER_PRESENT) &&
             IS_ENABLED(CONFIG_AMS_CAP_IMD_ACTOR_LIVE) &&
             IS_ENABLED(CONFIG_AMS_CAP_IMD_SAFETY_EVIDENCE),
             "IMD adapter/actor/evidence must remain live at Z-014");
BUILD_ASSERT(!IS_ENABLED(CONFIG_AMS_CAP_IMD_PHYSICAL_VALIDATED),
             "IMD physical validation remains an open hardware gate");
BUILD_ASSERT(IS_ENABLED(CONFIG_AMS_CAP_WATCHDOG_ADAPTER_PRESENT),
             "Z-014 requires the isolated watchdog platform adapter");
BUILD_ASSERT(IS_ENABLED(CONFIG_AMS_CAP_WATCHDOG_ACTIVE) ==
             IS_ENABLED(CONFIG_AMS_IWDG_VALIDATION_MODE),
             "watchdog active capability must exactly match validation mode");
BUILD_ASSERT(!IS_ENABLED(CONFIG_AMS_CAP_WATCHDOG_PHYSICAL_VALIDATED),
             "watchdog physical validation remains an open hardware gate");
BUILD_ASSERT(!IS_ENABLED(CONFIG_AMS_CAP_WATCHDOG_FULL_ORACLE_COVERAGE),
             "Z-014 is intentionally partial watchdog evidence coverage");
BUILD_ASSERT(!IS_ENABLED(CONFIG_AMS_WATCHDOG_TARGET_VALIDATED),
             "Z-014 candidate must not claim target watchdog validation");
BUILD_ASSERT(AMS_WATCHDOG_PLATFORM_TIMEOUT_MS == AMS_WATCHDOG_TIMEOUT_MS,
             "watchdog policy/platform timeout contract drift");

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

    /* Z-014 proactive stack-integrity scan. Snapshot readers consume the
     * latest safety-supervisor result instead of issuing their own query and
     * losing the k_thread_stack_space_get() return code. */
    atomic_t stack_query_valid;
    atomic_t stack_unused;
    atomic_t stack_warning_threshold;
    atomic_t stack_critical_threshold;
    atomic_t stack_warning;
    atomic_t stack_critical;
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
    size_t configured_stack_bytes;

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

/* Z-012 fan process diagnostics. The fan worker is the sole policy writer;
 * atomics provide race-free diagnostic observation without introducing a
 * mutex into the cooling path. */
static atomic_t fan_fault;
static atomic_t fan_set_fail_count;
static atomic_t fan_command_centipercent;
static atomic_t fan_command_on;
static atomic_t fan_control_reason;
static atomic_t fan_last_update_ms;

/* Z-013 IMD process snapshot. The sequence brackets a coherent publication
 * equivalent to the v2.6.27 critical section in imd_task_update(). */
static ams_imd_t imd_state;
static atomic_t imd_publish_sequence;
static atomic_t imd_valid;
static atomic_t imd_ok;
static atomic_t imd_fault;
static atomic_t imd_status;
static atomic_t imd_duty_centipercent;
static atomic_t imd_frequency_millihz;
static atomic_t imd_last_valid_ms;
static atomic_t imd_last_update_ms;

/* Z-014 software-integrity/watchdog diagnostics. Safety supervisor is the
 * sole writer; atomics/seqlock make diagnostics observation race-free. */
static ams_watchdog_policy_state_t watchdog_policy_state;
/* Safety-liveness heartbeat state is independent from generic runtime stats.
 * v2.6.27 protected kick/update with a short critical section; Zephyr uses the
 * same ownership model through this bounded spinlock. Only real migrated
 * safety actors are permitted to call watchdog_heartbeat_kick(). */
static ams_watchdog_heartbeat_monitor_t watchdog_heartbeat_monitor;
static struct k_spinlock watchdog_heartbeat_lock;
static atomic_t watchdog_publish_sequence;
static atomic_t watchdog_runtime_enabled;
static atomic_t watchdog_health_good;
static atomic_t watchdog_coverage_complete;
static atomic_t watchdog_stop_feed_test;
static atomic_t watchdog_oracle_required_mask;
static atomic_t watchdog_migration_evidence_mask;
static atomic_t watchdog_effective_required_mask;
static atomic_t watchdog_effective_stale_mask;
static atomic_t watchdog_stack_warning_mask;
static atomic_t watchdog_stack_critical_mask;
static atomic_t watchdog_stack_query_error_mask;
static atomic_t watchdog_min_stack_unused;
static atomic_t watchdog_block_reason;
static atomic_t watchdog_feed_count;
static atomic_t watchdog_block_count;
static atomic_t watchdog_last_feed_ms;
static atomic_t watchdog_platform_state;
static atomic_t watchdog_platform_last_error;
static atomic_t watchdog_platform_prepare_attempt_count;
static atomic_t watchdog_platform_start_attempt_count;
static atomic_t watchdog_platform_feed_success_count;
static atomic_t watchdog_platform_feed_failure_count;
static atomic_t watchdog_reset_cause_valid;
static atomic_t watchdog_reset_was_watchdog;
static atomic_t watchdog_reset_cause_error;
static atomic_t watchdog_reset_cause_clear_error;

K_SEM_DEFINE(diagnostics_request, 0, 1);

static atomic_t runtime_started;
static atomic_t runtime_start_ms;

struct watchdog_heartbeat_snapshot {
    uint32_t now_ms;
    uint16_t stale_mask;
};

static void watchdog_heartbeat_kick(ams_watchdog_heartbeat_id_t id,
                                    uint32_t now_ms);
static struct watchdog_heartbeat_snapshot watchdog_heartbeat_snapshot_get(void);


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
        .configured_stack_bytes = AMS_STACK_SAFETY,
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
        .configured_stack_bytes = AMS_STACK_CURRENT,
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
        .configured_stack_bytes = AMS_STACK_ADBMS,
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
        .configured_stack_bytes = AMS_STACK_CAN,
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
        .configured_stack_bytes = AMS_STACK_ESTIMATOR,
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
        .safety_evidence_ready = true,
        .thread = &fan_thread,
        .stack = fan_stack,
        .stack_size = K_THREAD_STACK_SIZEOF(fan_stack),
        .configured_stack_bytes = AMS_STACK_FAN,
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
        .configured_stack_bytes = AMS_STACK_AIR,
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
        .safety_evidence_ready = true,
        .thread = &imd_thread,
        .stack = imd_stack,
        .stack_size = K_THREAD_STACK_SIZEOF(imd_stack),
        .configured_stack_bytes = AMS_STACK_IMD,
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
        .configured_stack_bytes = AMS_STACK_DIAGNOSTICS,
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


static void atomic_increment_saturating_u32(atomic_t *value)
{
    atomic_val_t observed;

    if (value == NULL) {
        return;
    }

    observed = atomic_get(value);

    while ((uint32_t)observed != UINT32_MAX) {
        if (atomic_cas(value,
                       observed,
                       (atomic_val_t)((uint32_t)observed + 1U))) {
            return;
        }

        observed = atomic_get(value);
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

    /* v2.6.27 heartbeat counters saturate instead of wrapping through zero.
     * Zero is reserved for "never completed", so wrapping would fabricate an
     * unseen-task state after a sufficiently long uptime. */
    atomic_increment_saturating_u32(&thread->stat->heartbeat_seq);
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

#ifdef CONFIG_AMS_Z016_LINK_PROBE
        if (thread == &threads[AMS_THREAD_ADBMS]) {
            ams_adbms_link_probe_step();
        }
#endif

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


static void fan_increment_fail_count(void)
{
    atomic_val_t observed = atomic_get(&fan_set_fail_count);

    while ((uint32_t)observed != UINT32_MAX) {
        if (atomic_cas(&fan_set_fail_count,
                       observed,
                       (atomic_val_t)((uint32_t)observed + 1U))) {
            return;
        }

        observed = atomic_get(&fan_set_fail_count);
    }
}

static void fan_thread_entry(void *p1,
                             void *p2,
                             void *p3)
{
    struct ams_thread_descriptor *thread = p1;
    float previous_percent = 0.0f;
    uint8_t previous_reason = AMS_FAN_CONTROL_REASON_OFF_COOL;
    int64_t release_ms = k_uptime_get();

    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    for (;;) {
        ams_fan_control_input_t input = {0};
        uint8_t reason = AMS_FAN_CONTROL_REASON_OFF_COOL;
        float percent;
        bool iteration_fault = false;
        int64_t start_ms;
        int64_t complete_ms;
        int64_t next_release_ms;
        uint32_t start_cycles;
        uint32_t elapsed_cycles;
        uint32_t exec_us;

        k_sleep(K_TIMEOUT_ABS_MS(release_ms));

        start_ms = k_uptime_get();
        start_cycles = k_cycle_get_32();
        runtime_publish_start(thread, release_ms, start_ms);

        /*
         * Temperature acquisition is not integrated yet. Do not invent a
         * nominal value. The exact v2.6.27 fan policy treats missing/untrusted
         * temperature evidence as TEMP_INVALID and commands maximum cooling.
         */
        input.temp_valid = false;
        input.temp_read_fault = true;
        input.temp_usable_sensor_count = 0U;
        input.temp_fault = false;
        input.temp_fan_max = false;
        input.max_temp = 0.0f;
        input.state = 0U;
        input.fan_command_percent = previous_percent;
        input.fan_control_reason = previous_reason;

        percent = ams_fan_percent_from_temp(&input, &reason);

        atomic_set(&fan_fault, 0);
        for (uint8_t zone = 0U; zone < AMS_FAN_ZONE_COUNT; ++zone) {
            if (ams_fan_pwm_set_percent(zone, percent) != 0) {
                iteration_fault = true;
                fan_increment_fail_count();
            }
        }

        if (iteration_fault) {
            atomic_set(&fan_fault, 1);
            reason = AMS_FAN_CONTROL_REASON_DRIVER_FAULT;
        }

        previous_percent = percent;
        previous_reason = reason;

        atomic_set(&fan_command_centipercent,
                   (atomic_val_t)(uint32_t)(percent * 100.0f));
        atomic_set(&fan_command_on, percent > 0.5f ? 1 : 0);
        atomic_set(&fan_control_reason, (atomic_val_t)reason);
        atomic_set(&fan_last_update_ms, (atomic_val_t)(uint32_t)start_ms);

        elapsed_cycles = k_cycle_get_32() - start_cycles;
        exec_us = k_cyc_to_us_floor32(elapsed_cycles);
        complete_ms = k_uptime_get();

        /* Watchdog liveness is explicit and separate from generic thread
         * diagnostics. Kick only after the real six-zone actuation attempt;
         * process/driver faults still kick because they are not software death. */
        watchdog_heartbeat_kick(AMS_WATCHDOG_HEARTBEAT_FAN,
                                (uint32_t)complete_ms);
        runtime_publish_complete(thread, complete_ms, exec_us);

        /*
         * Match v2.6.27 fan_task_fn()/osDelayUntil(entry + 200 ms): the
         * deadline is anchored to this iteration's actual entry time. If work
         * overruns the period, the delay returns immediately and the next
         * iteration reanchors instead of skipping an additional full period.
         */
        next_release_ms = start_ms + thread->period_ms;
        if (complete_ms >= next_release_ms) {
            atomic_inc(&thread->stat->overrun_count);
            release_ms = complete_ms;
        } else {
            release_ms = next_release_ms;
        }
    }
}


static void imd_publish_snapshot(bool valid,
                                 bool ok,
                                 ams_imd_status_t status,
                                 uint32_t now_ms)
{
    uint32_t sequence = (uint32_t)atomic_get(&imd_publish_sequence);
    uint32_t duty_centipercent = 0U;
    uint32_t frequency_millihz = 0U;

    if ((sequence & 1U) != 0U) {
        sequence++;
    }

    if (valid) {
        duty_centipercent =
            (uint32_t)(imd_state.duty_percent * 100.0f);
        frequency_millihz =
            (uint32_t)(imd_state.frequency_hz * 1000.0f);
    }

    /* Zephyr atomic operations provide the barriers needed for this short
     * thread/diagnostic seqlock. This preserves the coherent state publication
     * that v2.6.27 performed inside taskENTER_CRITICAL(). */
    atomic_set(&imd_publish_sequence, (atomic_val_t)(sequence + 1U));
    atomic_set(&imd_valid, valid ? 1 : 0);
    atomic_set(&imd_ok, ok ? 1 : 0);
    atomic_set(&imd_fault, ok ? 0 : 1);
    atomic_set(&imd_status, (atomic_val_t)status);
    atomic_set(&imd_duty_centipercent, (atomic_val_t)duty_centipercent);
    atomic_set(&imd_frequency_millihz, (atomic_val_t)frequency_millihz);
    if (valid) {
        atomic_set(&imd_last_valid_ms, (atomic_val_t)now_ms);
    }
    atomic_set(&imd_last_update_ms, (atomic_val_t)now_ms);
    atomic_set(&imd_publish_sequence, (atomic_val_t)(sequence + 2U));
}


static void imd_thread_entry(void *p1,
                             void *p2,
                             void *p3)
{
    struct ams_thread_descriptor *thread = p1;
    int64_t release_ms = k_uptime_get();

    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    for (;;) {
        bool valid;
        bool ok;
        ams_imd_status_t status;
        int64_t start_ms;
        int64_t complete_ms;
        int64_t next_release_ms;
        uint32_t start_cycles;
        uint32_t elapsed_cycles;
        uint32_t exec_us;

        k_sleep(K_TIMEOUT_ABS_MS(release_ms));

        start_ms = k_uptime_get();
        start_cycles = k_cycle_get_32();
        runtime_publish_start(thread, release_ms, start_ms);

        valid =
            (ams_imd_capture_read_at(&imd_state, (uint32_t)start_ms) == 0);
        status = valid ? imd_state.status : AMS_IMD_UNKNOWN;
        ok = valid && imd_state.ok_hs && (status == AMS_IMD_NORMAL);

        /* Match v2.6.27 imd_task_update(): publish a coherent process snapshot
         * first, then independently force BMS_OK low for any non-normal IMD
         * result. Assertion remains compile-time impossible at Z-013. */
        imd_publish_snapshot(valid, ok, status, (uint32_t)start_ms);

        if (!ok) {
            ams_bms_ok_force_low_direct();
        }

        elapsed_cycles = k_cycle_get_32() - start_cycles;
        exec_us = k_cyc_to_us_floor32(elapsed_cycles);
        complete_ms = k_uptime_get();

        /* v2.6.27 kicks IMD after fail-low handling. A bad IMD value remains
         * a process fault while successful task execution remains watchdog
         * liveness evidence. */
        watchdog_heartbeat_kick(AMS_WATCHDOG_HEARTBEAT_IMD,
                                (uint32_t)complete_ms);
        runtime_publish_complete(thread, complete_ms, exec_us);

        /* Match osDelayUntil(entry + 100 ms): re-anchor from the current
         * iteration entry and retry immediately after an overrun. */
        next_release_ms = start_ms + thread->period_ms;
        if (complete_ms >= next_release_ms) {
            atomic_inc(&thread->stat->overrun_count);
            release_ms = complete_ms;
        } else {
            release_ms = next_release_ms;
        }
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
                (startup_age_ms >= thread->startup_grace_ms) ? 1 : 0);

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


static uint16_t watchdog_oracle_required_mask_value(void)
{
    /* Exact v2.6.27 final safety heartbeat mask for this no-SoP-authority
     * stage. LOGGER is intentionally excluded and ESTIMATOR is not required
     * until SoP authority is restored. */
    return (uint16_t)(
        AMS_WATCHDOG_HEARTBEAT_BIT(AMS_WATCHDOG_HEARTBEAT_ADBMS) |
        AMS_WATCHDOG_HEARTBEAT_BIT(AMS_WATCHDOG_HEARTBEAT_CURRENT) |
        AMS_WATCHDOG_HEARTBEAT_BIT(AMS_WATCHDOG_HEARTBEAT_TEMP) |
        AMS_WATCHDOG_HEARTBEAT_BIT(AMS_WATCHDOG_HEARTBEAT_CAN) |
        AMS_WATCHDOG_HEARTBEAT_BIT(AMS_WATCHDOG_HEARTBEAT_IMD) |
        AMS_WATCHDOG_HEARTBEAT_BIT(AMS_WATCHDOG_HEARTBEAT_FAN));
}

static uint16_t watchdog_migration_evidence_mask_value(void)
{
    uint16_t mask = 0U;

    /* Never infer safety evidence from a placeholder heartbeat. Capabilities
     * are the compile-time migration source of truth. */
    if (IS_ENABLED(CONFIG_AMS_CAP_CURRENT_SAFETY_EVIDENCE)) {
        mask |= AMS_WATCHDOG_HEARTBEAT_BIT(AMS_WATCHDOG_HEARTBEAT_CURRENT);
    }
    if (IS_ENABLED(CONFIG_AMS_CAP_ADBMS_SAFETY_EVIDENCE)) {
        mask |= AMS_WATCHDOG_HEARTBEAT_BIT(AMS_WATCHDOG_HEARTBEAT_ADBMS);
    }
    if (IS_ENABLED(CONFIG_AMS_CAP_TEMPERATURE_SAFETY_EVIDENCE)) {
        mask |= AMS_WATCHDOG_HEARTBEAT_BIT(AMS_WATCHDOG_HEARTBEAT_TEMP);
    }
    if (IS_ENABLED(CONFIG_AMS_CAP_CAN_SAFETY_EVIDENCE)) {
        mask |= AMS_WATCHDOG_HEARTBEAT_BIT(AMS_WATCHDOG_HEARTBEAT_CAN);
    }
    if (IS_ENABLED(CONFIG_AMS_CAP_FAN_SAFETY_EVIDENCE)) {
        mask |= AMS_WATCHDOG_HEARTBEAT_BIT(AMS_WATCHDOG_HEARTBEAT_FAN);
    }
    if (IS_ENABLED(CONFIG_AMS_CAP_IMD_SAFETY_EVIDENCE)) {
        mask |= AMS_WATCHDOG_HEARTBEAT_BIT(AMS_WATCHDOG_HEARTBEAT_IMD);
    }

    return mask;
}

static void watchdog_heartbeat_kick(ams_watchdog_heartbeat_id_t id,
                                    uint32_t now_ms)
{
    k_spinlock_key_t key = k_spin_lock(&watchdog_heartbeat_lock);

    (void)ams_watchdog_heartbeat_kick(&watchdog_heartbeat_monitor, id, now_ms);
    k_spin_unlock(&watchdog_heartbeat_lock, key);
}

static struct watchdog_heartbeat_snapshot watchdog_heartbeat_snapshot_get(void)
{
    struct watchdog_heartbeat_snapshot snapshot;
    k_spinlock_key_t key = k_spin_lock(&watchdog_heartbeat_lock);

    /* Capture time while the heartbeat monitor is locked. A worker may have
     * computed its completion timestamp immediately before waiting for this
     * lock; sampling inside the same serialization boundary guarantees the
     * supervisor never evaluates a committed heartbeat against an older time
     * and turns unsigned wrap arithmetic into a false stale event. This is
     * equivalent on the single-core F767 and remains correct under host/SMP
     * concurrency. */
    snapshot.now_ms = k_uptime_get_32();
    snapshot.stale_mask =
        ams_watchdog_heartbeat_update(&watchdog_heartbeat_monitor,
                                      snapshot.now_ms,
                                      watchdog_oracle_required_mask_value());
    k_spin_unlock(&watchdog_heartbeat_lock, key);
    return snapshot;
}

static void runtime_scan_stack_integrity(uint16_t *warning_mask,
                                         uint16_t *critical_mask,
                                         uint16_t *query_error_mask,
                                         size_t *min_unused)
{
    uint16_t warn = 0U;
    uint16_t critical = 0U;
    uint16_t query_errors = 0U;
    size_t minimum = SIZE_MAX;

    for (size_t i = 0U; i < AMS_THREAD_COUNT; ++i) {
        struct ams_thread_descriptor *desc = &threads[i];
        size_t unused = 0U;
        int query_ret = 0;
        bool query_valid = true;
        ams_stack_health_t health;

        if (desc->enabled) {
            query_ret = k_thread_stack_space_get(desc->thread, &unused);
            query_valid = query_ret == 0;
        }

        /* k_thread_stack_space_get() reports against the usable Zephyr stack
         * object passed to k_thread_create(). Use that same size for the
         * percentage thresholds. The requested source constant remains a
         * compile-time lower-bound contract, but MPU/alignment expansion must
         * not silently weaken the 25%/15% proactive margin policy. */
        health = ams_stack_health_evaluate(desc->stack_size,
                                           unused,
                                           query_valid,
                                           desc->enabled);

        atomic_set(&desc->stat->stack_query_valid,
                   health.query_valid ? 1 : 0);
        atomic_set(&desc->stat->stack_unused,
                   (atomic_val_t)(uint32_t)unused);
        atomic_set(&desc->stat->stack_warning_threshold,
                   (atomic_val_t)(uint32_t)health.warning_threshold_bytes);
        atomic_set(&desc->stat->stack_critical_threshold,
                   (atomic_val_t)(uint32_t)health.critical_threshold_bytes);
        atomic_set(&desc->stat->stack_warning, health.warning ? 1 : 0);
        atomic_set(&desc->stat->stack_critical, health.critical ? 1 : 0);

        if (!desc->enabled) {
            continue;
        }

        if (!query_valid) {
            query_errors |= (uint16_t)(1U << i);
        } else if (unused < minimum) {
            minimum = unused;
        }
        if (health.warning) {
            warn |= (uint16_t)(1U << i);
        }
        if (health.critical) {
            critical |= (uint16_t)(1U << i);
        }
    }

    if (minimum == SIZE_MAX) {
        minimum = 0U;
    }

    if (warning_mask != NULL) {
        *warning_mask = warn;
    }
    if (critical_mask != NULL) {
        *critical_mask = critical;
    }
    if (query_error_mask != NULL) {
        *query_error_mask = query_errors;
    }
    if (min_unused != NULL) {
        *min_unused = minimum;
    }
}

static bool watchdog_stop_feed_test_active(uint32_t now_ms)
{
#if defined(CONFIG_AMS_IWDG_STOP_FEED_TEST_AFTER_MS) && \
    (CONFIG_AMS_IWDG_STOP_FEED_TEST_AFTER_MS > 0)
    uint32_t started_ms = (uint32_t)atomic_get(&runtime_start_ms);
    return IS_ENABLED(CONFIG_AMS_IWDG_VALIDATION_MODE) &&
           ((uint32_t)(now_ms - started_ms) >=
            (uint32_t)CONFIG_AMS_IWDG_STOP_FEED_TEST_AFTER_MS);
#else
    ARG_UNUSED(now_ms);
    return false;
#endif
}

static void watchdog_publish_runtime_snapshot(
    const ams_watchdog_policy_action_t *action,
    const ams_watchdog_platform_status_t *platform,
    uint16_t oracle_required_mask,
    uint16_t migration_evidence_mask,
    uint16_t stack_warning_mask,
    uint16_t stack_critical_mask,
    uint16_t stack_query_error_mask,
    size_t min_stack_unused,
    bool stop_feed_test)
{
    uint32_t sequence = (uint32_t)atomic_get(&watchdog_publish_sequence);

    if ((sequence & 1U) != 0U) {
        sequence++;
    }

    atomic_set(&watchdog_publish_sequence, (atomic_val_t)(sequence + 1U));
    atomic_set(&watchdog_runtime_enabled,
               IS_ENABLED(CONFIG_AMS_IWDG_VALIDATION_MODE) ? 1 : 0);
    atomic_set(&watchdog_health_good, action->health_good ? 1 : 0);
    atomic_set(&watchdog_coverage_complete, action->coverage_complete ? 1 : 0);
    atomic_set(&watchdog_stop_feed_test, stop_feed_test ? 1 : 0);
    atomic_set(&watchdog_oracle_required_mask, (atomic_val_t)oracle_required_mask);
    atomic_set(&watchdog_migration_evidence_mask,
               (atomic_val_t)migration_evidence_mask);
    atomic_set(&watchdog_effective_required_mask,
               (atomic_val_t)action->effective_required_mask);
    atomic_set(&watchdog_effective_stale_mask,
               (atomic_val_t)action->effective_stale_mask);
    atomic_set(&watchdog_stack_warning_mask, (atomic_val_t)stack_warning_mask);
    atomic_set(&watchdog_stack_critical_mask, (atomic_val_t)stack_critical_mask);
    atomic_set(&watchdog_stack_query_error_mask,
               (atomic_val_t)stack_query_error_mask);
    atomic_set(&watchdog_min_stack_unused,
               (atomic_val_t)(uint32_t)min_stack_unused);
    atomic_set(&watchdog_block_reason, (atomic_val_t)action->block_reason);
    atomic_set(&watchdog_feed_count,
               (atomic_val_t)watchdog_policy_state.feed_count);
    atomic_set(&watchdog_block_count,
               (atomic_val_t)watchdog_policy_state.block_count);
    atomic_set(&watchdog_last_feed_ms,
               (atomic_val_t)watchdog_policy_state.last_feed_ms);
    atomic_set(&watchdog_platform_state, (atomic_val_t)platform->state);
    atomic_set(&watchdog_platform_last_error, (atomic_val_t)platform->last_error);
    atomic_set(&watchdog_platform_prepare_attempt_count,
               (atomic_val_t)platform->prepare_attempt_count);
    atomic_set(&watchdog_platform_start_attempt_count,
               (atomic_val_t)platform->start_attempt_count);
    atomic_set(&watchdog_platform_feed_success_count,
               (atomic_val_t)platform->feed_success_count);
    atomic_set(&watchdog_platform_feed_failure_count,
               (atomic_val_t)platform->feed_failure_count);
    atomic_set(&watchdog_reset_cause_valid,
               platform->reset_cause_valid ? 1 : 0);
    atomic_set(&watchdog_reset_was_watchdog,
               platform->reset_was_watchdog ? 1 : 0);
    atomic_set(&watchdog_reset_cause_error,
               (atomic_val_t)platform->reset_cause_error);
    atomic_set(&watchdog_reset_cause_clear_error,
               (atomic_val_t)platform->reset_cause_clear_error);
    atomic_set(&watchdog_publish_sequence, (atomic_val_t)(sequence + 2U));
}

static ams_watchdog_policy_action_t watchdog_evaluate_current(
    uint32_t now_ms,
    uint16_t stale_mask,
    uint16_t stack_critical_mask,
    bool stop_feed_test)
{
    ams_watchdog_platform_status_t platform = ams_watchdog_platform_status();
    ams_watchdog_policy_input_t input = {0};
    ams_watchdog_policy_action_t action = {0};

    input.now_ms = now_ms;
    input.boot_ms = (uint32_t)atomic_get(&runtime_start_ms);
    input.runtime_enabled = IS_ENABLED(CONFIG_AMS_IWDG_VALIDATION_MODE);
    input.platform_ready_to_start =
        platform.state == AMS_WATCHDOG_PLATFORM_READY_NOT_STARTED;
    input.platform_started = platform.state == AMS_WATCHDOG_PLATFORM_STARTED;
    input.platform_prepare_failed =
        platform.state == AMS_WATCHDOG_PLATFORM_PREPARE_FAILED_RETRYABLE;
    input.platform_start_terminal_fault =
        platform.state == AMS_WATCHDOG_PLATFORM_START_AMBIGUOUS_TERMINAL;
    input.platform_feed_terminal_fault =
        platform.state == AMS_WATCHDOG_PLATFORM_FEED_FAILED_TERMINAL;
    input.panic_latched = ams_safety_panic_latched();
    input.stop_feed_test = stop_feed_test;
    input.oracle_required_mask = watchdog_oracle_required_mask_value();
    input.migration_evidence_mask = watchdog_migration_evidence_mask_value();
    input.stale_mask = stale_mask;
    input.rtos_integrity_fault = false;
    input.stack_critical_mask = stack_critical_mask;

    ams_watchdog_policy_evaluate(&watchdog_policy_state, &input, &action);
    return action;
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
        struct watchdog_heartbeat_snapshot heartbeat_snapshot;
        uint32_t watchdog_now_ms;
        uint16_t watchdog_stale_mask = 0U;
        uint16_t stack_warning_mask = 0U;
        uint16_t stack_critical_mask = 0U;
        uint16_t stack_query_error_mask = 0U;
        size_t min_stack_unused = 0U;
        bool stop_feed_test;
        ams_watchdog_policy_action_t action;
        ams_watchdog_platform_status_t platform;

        k_sleep(K_TIMEOUT_ABS_MS(release_ms));

        start_ms = k_uptime_get();
        start_cycles = k_cycle_get_32();
        runtime_publish_start(thread, release_ms, start_ms);

        if (atomic_get(&runtime_started) != 0) {
            runtime_update_stale_flags();
        }

        /* Capture watchdog heartbeat evidence as one coherent monitor
         * transaction. Workers can complete concurrently, but a supervisor
         * decision is based on exactly one stale-mask snapshot. */
        heartbeat_snapshot = watchdog_heartbeat_snapshot_get();
        watchdog_now_ms = heartbeat_snapshot.now_ms;
        watchdog_stale_mask = heartbeat_snapshot.stale_mask;

        /* Proactive stack margin parity runs every 50 ms, before any feed.
         * Query failure is distinguishable from a genuine zero-byte margin and
         * is fail-closed critical for every enabled/created thread. */
        runtime_scan_stack_integrity(&stack_warning_mask,
                                     &stack_critical_mask,
                                     &stack_query_error_mask,
                                     &min_stack_unused);

        if ((stack_critical_mask != 0U) || (stack_query_error_mask != 0U)) {
            ams_bms_ok_force_low_direct();
        }

        stop_feed_test = watchdog_stop_feed_test_active(watchdog_now_ms);

        /* A prepare failure is provably pre-IWDG-start with the pinned Zephyr
         * v4.4 STM32 driver, so the safety owner may retry it. Base builds do
         * not need repeated preparation because the watchdog is not armed. */
        platform = ams_watchdog_platform_status();
        if (IS_ENABLED(CONFIG_AMS_IWDG_VALIDATION_MODE) &&
            ((platform.state == AMS_WATCHDOG_PLATFORM_UNPREPARED) ||
             (platform.state == AMS_WATCHDOG_PLATFORM_PREPARE_FAILED_RETRYABLE))) {
            (void)ams_watchdog_platform_prepare();
        }

        action = watchdog_evaluate_current(watchdog_now_ms,
                                           watchdog_stale_mask,
                                           stack_critical_mask,
                                           stop_feed_test);

        if (action.request_start) {
            ams_watchdog_start_result_t start_result =
                ams_watchdog_platform_start();

            if (start_result == AMS_WATCHDOG_START_RESULT_AMBIGUOUS_TERMINAL) {
                /* setup() may have enabled IWDG before reporting failure. Do
                 * not retry/reconfigure/disable; force safe output and let a
                 * possibly-running watchdog expire. */
                ams_bms_ok_force_low_direct();
            }

            action = watchdog_evaluate_current(watchdog_now_ms,
                                               watchdog_stale_mask,
                                               stack_critical_mask,
                                               stop_feed_test);
        }

        if (action.feed_permitted) {
            int feed_ret = ams_watchdog_platform_feed();
            if (feed_ret == 0) {
                ams_watchdog_policy_record_feed(&watchdog_policy_state,
                                                watchdog_now_ms,
                                                action.block_reason);
            } else {
                /* Never count a failed platform call as a watchdog feed. */
                ams_bms_ok_force_low_direct();
                action = watchdog_evaluate_current(watchdog_now_ms,
                                                   watchdog_stale_mask,
                                                   stack_critical_mask,
                                                   stop_feed_test);
                if (action.block_reason != AMS_WATCHDOG_BLOCK_RTOS_INTEGRITY) {
                    action.block_reason = AMS_WATCHDOG_BLOCK_RTOS_INTEGRITY;
                    action.feed_permitted = false;
                    action.health_good = false;
                    action.count_block = true;
                }
                ams_watchdog_policy_record_block(&watchdog_policy_state,
                                                 action.block_reason);
            }
        } else if (!action.request_start) {
            ams_watchdog_policy_record_block(&watchdog_policy_state,
                                             action.block_reason);
        }

        platform = ams_watchdog_platform_status();
        watchdog_publish_runtime_snapshot(&action,
                                          &platform,
                                          watchdog_oracle_required_mask_value(),
                                          watchdog_migration_evidence_mask_value(),
                                          stack_warning_mask,
                                          stack_critical_mask,
                                          stack_query_error_mask,
                                          min_stack_unused,
                                          stop_feed_test);

        elapsed_cycles = k_cycle_get_32() - start_cycles;
        exec_us = k_cyc_to_us_floor32(elapsed_cycles);
        complete_ms = k_uptime_get();

        /* Feed occurs near the end and before this completion publication, so
         * a feed proves stale/stack/policy checks completed in this cycle. */
        runtime_publish_complete(thread, complete_ms, exec_us);

        /* Match v2.6.27 error_task_fn()/osDelayUntil(entry + 50 ms). */
        next_release_ms = start_ms + thread->period_ms;
        if (complete_ms >= next_release_ms) {
            atomic_inc(&thread->stat->overrun_count);
            release_ms = complete_ms;
        } else {
            release_ms = next_release_ms;
        }
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

        printk("\nAMS Z-014 runtime snapshot\n");
        printk("thread           en sf ev p  per age stale late maxL exec wcet stack-used q w c\n");

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
                "%-16s %2u %2u %2u %2d %4u %4u %5u %4u %4u %4u %4u %5u/%u %u %u %u\n",
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
                (unsigned int)snapshot.stack_size,
                snapshot.stack_query_valid ? 1U : 0U,
                snapshot.stack_warning ? 1U : 0U,
                snapshot.stack_critical ? 1U : 0U);
        }

        printk("fan              fault=%u fails=%u cmd=%.2f%% on=%u reason=%s last=%u\n",
               (unsigned int)atomic_get(&fan_fault),
               (unsigned int)atomic_get(&fan_set_fail_count),
               (double)((uint32_t)atomic_get(&fan_command_centipercent) / 100.0),
               atomic_get(&fan_command_on) != 0 ? 1U : 0U,
               ams_fan_control_reason_str((uint8_t)atomic_get(&fan_control_reason)),
               (unsigned int)atomic_get(&fan_last_update_ms));

        struct ams_imd_runtime_snapshot imd_snapshot;
        if (ams_imd_runtime_snapshot_get(&imd_snapshot) == 0) {
            printk("imd              valid=%u ok=%u fault=%u status=%u duty=%.2f%% freq=%.3fHz cap=%u err=%u last=%u\n",
                   imd_snapshot.valid ? 1U : 0U,
                   imd_snapshot.ok ? 1U : 0U,
                   imd_snapshot.fault ? 1U : 0U,
                   (unsigned int)imd_snapshot.status,
                   (double)imd_snapshot.duty_centipercent / 100.0,
                   (double)imd_snapshot.frequency_millihz / 1000.0,
                   (unsigned int)imd_snapshot.capture_callback_count,
                   (unsigned int)imd_snapshot.capture_callback_error_count,
                   (unsigned int)imd_snapshot.last_valid_ms);
        }

        struct ams_watchdog_runtime_snapshot watchdog_snapshot;
        if (ams_watchdog_runtime_snapshot_get(&watchdog_snapshot) == 0) {
            printk("watchdog         en=%u health=%u coverage=%u reason=%s feed=%u block=%u last=%u\n",
                   watchdog_snapshot.runtime_enabled ? 1U : 0U,
                   watchdog_snapshot.health_good ? 1U : 0U,
                   watchdog_snapshot.coverage_complete ? 1U : 0U,
                   ams_watchdog_block_reason_str(watchdog_snapshot.block_reason),
                   (unsigned int)watchdog_snapshot.feed_count,
                   (unsigned int)watchdog_snapshot.block_count,
                   (unsigned int)watchdog_snapshot.last_feed_ms);
            printk("watchdog masks   oracle=0x%02x evidence=0x%02x effective=0x%02x stale=0x%02x stackW=0x%03x stackC=0x%03x stackQ=0x%03x min=%u\n",
                   (unsigned int)watchdog_snapshot.oracle_required_mask,
                   (unsigned int)watchdog_snapshot.migration_evidence_mask,
                   (unsigned int)watchdog_snapshot.effective_required_mask,
                   (unsigned int)watchdog_snapshot.effective_stale_mask,
                   (unsigned int)watchdog_snapshot.stack_warning_mask,
                   (unsigned int)watchdog_snapshot.stack_critical_mask,
                   (unsigned int)watchdog_snapshot.stack_query_error_mask,
                   (unsigned int)watchdog_snapshot.min_stack_unused);
            printk("watchdog hw      state=%u err=%d prep=%u start=%u feedok=%u feederr=%u reset_valid=%u iwdg_reset=%u reset_err=%d clear_err=%d stoptest=%u\n",
                   (unsigned int)watchdog_snapshot.platform_state,
                   watchdog_snapshot.platform_last_error,
                   (unsigned int)watchdog_snapshot.platform_prepare_attempt_count,
                   (unsigned int)watchdog_snapshot.platform_start_attempt_count,
                   (unsigned int)watchdog_snapshot.platform_feed_success_count,
                   (unsigned int)watchdog_snapshot.platform_feed_failure_count,
                   watchdog_snapshot.reset_cause_valid ? 1U : 0U,
                   watchdog_snapshot.reset_was_watchdog ? 1U : 0U,
                   watchdog_snapshot.reset_cause_error,
                   watchdog_snapshot.reset_cause_clear_error,
                   watchdog_snapshot.stop_feed_test ? 1U : 0U);
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

    /* v2.6.27 board_init()/imd_init() completes before ams_heartbeat_init().
     * Preserve that order: timer/GPIO configuration failures are still startup
     * integrity failures, while capture-enable failure is retained as a soft
     * fail-closed IMD process fault by the adapter. */
    int imd_init_ret = ams_imd_capture_init(&imd_state);
    if (imd_init_ret != 0) {
        return imd_init_ret;
    }

    atomic_set(&imd_publish_sequence, 0);
    atomic_set(&imd_valid, 0);
    atomic_set(&imd_ok, 0);
    atomic_set(&imd_fault, 1);
    atomic_set(&imd_status, AMS_IMD_UNKNOWN);
    atomic_set(&imd_duty_centipercent, 0);
    atomic_set(&imd_frequency_millihz, 0);
    atomic_set(&imd_last_valid_ms, 0);
    atomic_set(&imd_last_update_ms, 0);

    /* Z-014 preparation is deliberately before the heartbeat epoch and is
     * non-irreversible with the pinned Zephyr v4.4 STM32 driver. It installs
     * the timeout and captures reset cause but never calls wdt_setup(). A
     * failure is retained for the safety supervisor to retry in validation
     * mode; it is not promoted to a generic startup panic. */
    ams_watchdog_policy_init(&watchdog_policy_state);
    (void)ams_watchdog_platform_prepare();

    atomic_set(&watchdog_publish_sequence, 0);
    atomic_set(&watchdog_runtime_enabled,
               IS_ENABLED(CONFIG_AMS_IWDG_VALIDATION_MODE) ? 1 : 0);
    atomic_set(&watchdog_health_good, 0);
    atomic_set(&watchdog_coverage_complete, 0);
    atomic_set(&watchdog_stop_feed_test, 0);
    atomic_set(&watchdog_oracle_required_mask,
               (atomic_val_t)watchdog_oracle_required_mask_value());
    atomic_set(&watchdog_migration_evidence_mask,
               (atomic_val_t)watchdog_migration_evidence_mask_value());
    atomic_set(&watchdog_effective_required_mask, 0);
    atomic_set(&watchdog_effective_stale_mask, 0);
    atomic_set(&watchdog_stack_warning_mask, 0);
    atomic_set(&watchdog_stack_critical_mask, 0);
    atomic_set(&watchdog_stack_query_error_mask, 0);
    atomic_set(&watchdog_min_stack_unused, 0);
    atomic_set(&watchdog_block_reason, AMS_WATCHDOG_BLOCK_NOT_ENABLED);
    atomic_set(&watchdog_feed_count, 0);
    atomic_set(&watchdog_block_count, 0);
    atomic_set(&watchdog_last_feed_ms, 0);

    /*
     * Start the heartbeat/startup-grace epoch before creating application
     * threads. v2.6.27 calls ams_heartbeat_init() before its safety-critical
     * mutex/thread creation, so thread-construction time belongs to the same
     * bounded startup grace rather than silently extending it.
     */
    atomic_set(
        &runtime_start_ms,
        (atomic_val_t)k_uptime_get_32());
    {
        k_spinlock_key_t key = k_spin_lock(&watchdog_heartbeat_lock);
        ams_watchdog_heartbeat_init(
            &watchdog_heartbeat_monitor,
            (uint32_t)atomic_get(&runtime_start_ms));
        k_spin_unlock(&watchdog_heartbeat_lock, key);
    }

    /* Preserve v2.6.27 heartbeat_init -> watchdog_boot_arm -> task creation.
     * Base migration builds stop here at safe preparation. Only the explicit
     * validation image crosses wdt_setup()'s irreversible boundary. */
    if (IS_ENABLED(CONFIG_AMS_IWDG_VALIDATION_MODE)) {
        ams_watchdog_platform_status_t platform =
            ams_watchdog_platform_status();

        if (platform.state == AMS_WATCHDOG_PLATFORM_READY_NOT_STARTED) {
            ams_watchdog_start_result_t start_result =
                ams_watchdog_platform_start();
            if (start_result == AMS_WATCHDOG_START_RESULT_AMBIGUOUS_TERMINAL) {
                ams_bms_ok_force_low_direct();
            }
        }
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
            fan_thread_entry) == NULL) {
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
            imd_thread_entry) == NULL) {
        return -ENOMEM;
    }

    if (create_thread(
            &threads[AMS_THREAD_DIAGNOSTICS],
            diagnostics_thread_entry) == NULL) {
        return -ENOMEM;
    }

    /* Preserve board_init() semantics from v2.6.27: channel-start failure
     * is visible as a fan process fault before workers run, but it is not a
     * software-integrity panic and does not increment the runtime set-failure
     * counter until an actual fan-task command fails. */
    atomic_set(&fan_fault,
               ams_fan_pwm_startup_fail_mask() != 0U ? 1 : 0);
    atomic_set(&fan_set_fail_count, 0);
    atomic_set(&fan_command_centipercent, 0);
    atomic_set(&fan_command_on, 0);
    atomic_set(&fan_control_reason,
               ams_fan_pwm_startup_fail_mask() != 0U
                   ? AMS_FAN_CONTROL_REASON_DRIVER_FAULT
                   : AMS_FAN_CONTROL_REASON_OFF_COOL);
    atomic_set(&fan_last_update_ms, 0);

    /*
     * All thread objects now exist. Mark runtime active before releasing the
     * highest-priority supervisor; the startup epoch above already includes
     * thread-construction time.
     */
    atomic_set(&runtime_started, 1);

    /*
     * Match the safety architecture rather than the historical creation order:
     * the supervisor is the highest-priority application thread and is active
     * before lower-priority work begins.  BMS_OK still cannot be asserted in
     * Z-014 because assertion authority is compile-time forbidden.
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

    size_t unused;
    bool stack_query_valid;

    uint32_t now_ms;
    uint32_t runtime_epoch_ms;

    if ((id < 0) ||
        (id >= AMS_THREAD_COUNT) ||
        (snapshot == NULL)) {
        return -EINVAL;
    }

    thread = &threads[id];

    /* Consume the latest safety-supervisor scan. Z-013 used to issue a
     * second query here and collapse query failure into unused=0, making
     * "unknown" indistinguishable from a real exhausted stack. */
    unused = (size_t)(uint32_t)atomic_get(&thread->stat->stack_unused);
    stack_query_valid = atomic_get(&thread->stat->stack_query_valid) != 0;

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

    snapshot->stack_size = thread->stack_size;
    snapshot->stack_configured_size = thread->configured_stack_bytes;
    snapshot->stack_query_valid = stack_query_valid;
    snapshot->stack_unused = unused;
    snapshot->stack_warning_threshold =
        (size_t)(uint32_t)atomic_get(&thread->stat->stack_warning_threshold);
    snapshot->stack_critical_threshold =
        (size_t)(uint32_t)atomic_get(&thread->stat->stack_critical_threshold);
    snapshot->stack_warning = atomic_get(&thread->stat->stack_warning) != 0;
    snapshot->stack_critical = atomic_get(&thread->stat->stack_critical) != 0;

    if (stack_query_valid && (unused <= thread->stack_size)) {
        snapshot->stack_used_high_water = thread->stack_size - unused;
    } else {
        /* Unknown stack query is explicitly marked invalid and represented
         * conservatively rather than fabricated as a genuine measurement. */
        snapshot->stack_used_high_water = thread->stack_size;
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
            snapshot->heartbeat_age_ms <
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


int ams_imd_runtime_snapshot_get(struct ams_imd_runtime_snapshot *snapshot)
{
    if (snapshot == NULL) {
        return -EINVAL;
    }

    for (uint8_t attempt = 0U; attempt < 3U; ++attempt) {
        uint32_t before = (uint32_t)atomic_get(&imd_publish_sequence);

        if ((before & 1U) != 0U) {
            continue;
        }

        snapshot->valid = atomic_get(&imd_valid) != 0;
        snapshot->ok = atomic_get(&imd_ok) != 0;
        snapshot->fault = atomic_get(&imd_fault) != 0;
        snapshot->status = (ams_imd_status_t)atomic_get(&imd_status);
        snapshot->duty_centipercent =
            (uint32_t)atomic_get(&imd_duty_centipercent);
        snapshot->frequency_millihz =
            (uint32_t)atomic_get(&imd_frequency_millihz);
        snapshot->last_valid_ms =
            (uint32_t)atomic_get(&imd_last_valid_ms);
        snapshot->last_update_ms =
            (uint32_t)atomic_get(&imd_last_update_ms);
        snapshot->capture_started = ams_imd_capture_started();
        snapshot->capture_start_error = ams_imd_capture_start_error();
        snapshot->capture_callback_fault =
            ams_imd_capture_callback_faulted();
        snapshot->capture_callback_count =
            ams_imd_capture_callback_count();
        snapshot->capture_callback_error_count =
            ams_imd_capture_callback_error_count();

        uint32_t after = (uint32_t)atomic_get(&imd_publish_sequence);
        if ((before == after) && ((after & 1U) == 0U)) {
            snapshot->sequence = after;
            return 0;
        }
    }

    return -EAGAIN;
}


int ams_watchdog_runtime_snapshot_get(struct ams_watchdog_runtime_snapshot *snapshot)
{
    if (snapshot == NULL) {
        return -EINVAL;
    }

    for (uint8_t attempt = 0U; attempt < 3U; ++attempt) {
        uint32_t before = (uint32_t)atomic_get(&watchdog_publish_sequence);
        uint32_t after;

        if ((before & 1U) != 0U) {
            continue;
        }

        snapshot->runtime_enabled = atomic_get(&watchdog_runtime_enabled) != 0;
        snapshot->health_good = atomic_get(&watchdog_health_good) != 0;
        snapshot->coverage_complete = atomic_get(&watchdog_coverage_complete) != 0;
        snapshot->stop_feed_test = atomic_get(&watchdog_stop_feed_test) != 0;

        snapshot->oracle_required_mask =
            (uint16_t)(uint32_t)atomic_get(&watchdog_oracle_required_mask);
        snapshot->migration_evidence_mask =
            (uint16_t)(uint32_t)atomic_get(&watchdog_migration_evidence_mask);
        snapshot->effective_required_mask =
            (uint16_t)(uint32_t)atomic_get(&watchdog_effective_required_mask);
        snapshot->effective_stale_mask =
            (uint16_t)(uint32_t)atomic_get(&watchdog_effective_stale_mask);

        snapshot->stack_warning_mask =
            (uint16_t)(uint32_t)atomic_get(&watchdog_stack_warning_mask);
        snapshot->stack_critical_mask =
            (uint16_t)(uint32_t)atomic_get(&watchdog_stack_critical_mask);
        snapshot->stack_query_error_mask =
            (uint16_t)(uint32_t)atomic_get(&watchdog_stack_query_error_mask);
        snapshot->min_stack_unused =
            (size_t)(uint32_t)atomic_get(&watchdog_min_stack_unused);

        snapshot->block_reason =
            (ams_watchdog_block_reason_t)atomic_get(&watchdog_block_reason);
        snapshot->feed_count =
            (uint32_t)atomic_get(&watchdog_feed_count);
        snapshot->block_count =
            (uint32_t)atomic_get(&watchdog_block_count);
        snapshot->last_feed_ms =
            (uint32_t)atomic_get(&watchdog_last_feed_ms);

        snapshot->platform_state =
            (uint32_t)atomic_get(&watchdog_platform_state);
        snapshot->platform_last_error =
            (int)atomic_get(&watchdog_platform_last_error);
        snapshot->platform_prepare_attempt_count =
            (uint32_t)atomic_get(&watchdog_platform_prepare_attempt_count);
        snapshot->platform_start_attempt_count =
            (uint32_t)atomic_get(&watchdog_platform_start_attempt_count);
        snapshot->platform_feed_success_count =
            (uint32_t)atomic_get(&watchdog_platform_feed_success_count);
        snapshot->platform_feed_failure_count =
            (uint32_t)atomic_get(&watchdog_platform_feed_failure_count);

        snapshot->reset_cause_valid = atomic_get(&watchdog_reset_cause_valid) != 0;
        snapshot->reset_was_watchdog = atomic_get(&watchdog_reset_was_watchdog) != 0;
        snapshot->reset_cause_error =
            (int)atomic_get(&watchdog_reset_cause_error);
        snapshot->reset_cause_clear_error =
            (int)atomic_get(&watchdog_reset_cause_clear_error);

        after = (uint32_t)atomic_get(&watchdog_publish_sequence);
        if ((before == after) && ((after & 1U) == 0U)) {
            snapshot->sequence = after;
            return 0;
        }
    }

    return -EAGAIN;
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
