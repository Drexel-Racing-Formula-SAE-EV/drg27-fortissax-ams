#define _POSIX_C_SOURCE 200809L

#include <ams_core/ams_watchdog_heartbeat.h>
#include <ams_core/ams_watchdog_policy.h>

#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define PRODUCER_KICKS 100000U
#define SUPERVISOR_STEPS 150000U

static ams_watchdog_heartbeat_monitor_t monitor_state;
static pthread_mutex_t monitor_lock = PTHREAD_MUTEX_INITIALIZER;
static atomic_uint virtual_now;
static atomic_bool producer_start;
static atomic_uint invariant_failures;
static atomic_uint invariant_failure_kind[8];

static uint16_t bit(ams_watchdog_heartbeat_id_t id)
{
    return AMS_WATCHDOG_HEARTBEAT_BIT(id);
}

static uint16_t oracle_mask(void)
{
    return (uint16_t)(bit(AMS_WATCHDOG_HEARTBEAT_ADBMS) |
                      bit(AMS_WATCHDOG_HEARTBEAT_CURRENT) |
                      bit(AMS_WATCHDOG_HEARTBEAT_TEMP) |
                      bit(AMS_WATCHDOG_HEARTBEAT_CAN) |
                      bit(AMS_WATCHDOG_HEARTBEAT_IMD) |
                      bit(AMS_WATCHDOG_HEARTBEAT_FAN));
}

static uint16_t evidence_mask(void)
{
    return (uint16_t)(bit(AMS_WATCHDOG_HEARTBEAT_FAN) |
                      bit(AMS_WATCHDOG_HEARTBEAT_IMD));
}

static void fail_if(bool condition, unsigned kind)
{
    if (condition) {
        atomic_fetch_add_explicit(&invariant_failures, 1U, memory_order_relaxed);
        if (kind < 8U) {
            atomic_fetch_add_explicit(&invariant_failure_kind[kind], 1U,
                                      memory_order_relaxed);
        }
    }
}

struct producer_arg {
    ams_watchdog_heartbeat_id_t id;
    uint32_t stride;
};

static void *producer(void *opaque)
{
    const struct producer_arg *arg = opaque;
    while (!atomic_load_explicit(&producer_start, memory_order_acquire)) {
        sched_yield();
    }

    for (uint32_t i = 0U; i < PRODUCER_KICKS; ++i) {
        uint32_t now;

        pthread_mutex_lock(&monitor_lock);
        /* Model the production integration boundary: completion timestamps are
         * monotonic wall time associated with the serialized heartbeat commit.
         * The supervisor likewise samples its decision time while holding this
         * same lock. */
        now = atomic_fetch_add_explicit(&virtual_now, arg->stride,
                                        memory_order_relaxed) + arg->stride;
        fail_if(!ams_watchdog_heartbeat_kick(&monitor_state, arg->id, now), 0U);
        pthread_mutex_unlock(&monitor_lock);
        if ((i & 0x3ffU) == 0U) {
            sched_yield();
        }
    }
    return NULL;
}

static void *supervisor(void *opaque)
{
    (void)opaque;
    ams_watchdog_policy_state_t state;
    ams_watchdog_policy_init(&state);

    while (!atomic_load_explicit(&producer_start, memory_order_acquire)) {
        sched_yield();
    }

    for (uint32_t i = 0U; i < SUPERVISOR_STEPS; ++i) {
        uint32_t now;
        uint16_t stale;
        ams_watchdog_policy_input_t in;
        ams_watchdog_policy_action_t out;

        pthread_mutex_lock(&monitor_lock);
        now = atomic_load_explicit(&virtual_now, memory_order_relaxed);
        stale = ams_watchdog_heartbeat_update(&monitor_state, now, oracle_mask());
        pthread_mutex_unlock(&monitor_lock);

        memset(&in, 0, sizeof(in));
        in.now_ms = now;
        in.boot_ms = 0U;
        in.runtime_enabled = true;
        in.platform_ready_to_start = true;
        in.platform_started = true;
        in.oracle_required_mask = oracle_mask();
        in.migration_evidence_mask = evidence_mask();
        in.stale_mask = stale;
        ams_watchdog_policy_evaluate(&state, &in, &out);

        fail_if(out.effective_required_mask != evidence_mask(), 1U);
        fail_if(out.effective_stale_mask != (uint16_t)(stale & evidence_mask()), 2U);
        fail_if(out.coverage_complete, 3U);
        if (out.feed_permitted) {
            /* v2.6.27 deliberately feeds through startup grace even if an
             * already-seen short-timeout heartbeat has gone stale. Outside
             * startup grace, stale effective evidence must never be fed. */
            fail_if((out.effective_stale_mask != 0U) &&
                    !out.startup_grace_active, 4U);
            fail_if(out.block_reason != AMS_WATCHDOG_BLOCK_NONE &&
                    out.block_reason != AMS_WATCHDOG_BLOCK_STARTUP_GRACE, 5U);
            ams_watchdog_policy_record_feed(&state, now, out.block_reason);
        } else {
            ams_watchdog_policy_record_block(&state, out.block_reason);
        }
        if ((i & 0x3ffU) == 0U) {
            sched_yield();
        }
    }
    return NULL;
}

static bool deterministic_feed_boundary_checks(void)
{
    ams_watchdog_policy_state_t state;
    ams_watchdog_policy_input_t in;
    ams_watchdog_policy_action_t out;
    uint16_t stale;

    ams_watchdog_policy_init(&state);
    ams_watchdog_heartbeat_init(&monitor_state, 0U);
    (void)ams_watchdog_heartbeat_kick(&monitor_state, AMS_WATCHDOG_HEARTBEAT_FAN, 4000U);
    (void)ams_watchdog_heartbeat_kick(&monitor_state, AMS_WATCHDOG_HEARTBEAT_IMD, 4000U);

    memset(&in, 0, sizeof(in));
    in.runtime_enabled = true;
    in.platform_ready_to_start = true;
    in.platform_started = true;
    in.oracle_required_mask = oracle_mask();
    in.migration_evidence_mask = evidence_mask();
    in.boot_ms = 0U;

    /* A stack fault discovered before policy/feed blocks this exact cycle. */
    stale = ams_watchdog_heartbeat_update(&monitor_state, 4100U, oracle_mask());
    in.now_ms = 4100U;
    in.stale_mask = stale;
    in.stack_critical_mask = 1U;
    ams_watchdog_policy_evaluate(&state, &in, &out);
    if (out.feed_permitted || out.block_reason != AMS_WATCHDOG_BLOCK_RTOS_INTEGRITY) {
        return false;
    }

    /* Healthy snapshot may feed. A fault that becomes observable after that
     * feed cannot invalidate history; it must block the next supervisor step. */
    in.stack_critical_mask = 0U;
    ams_watchdog_policy_evaluate(&state, &in, &out);
    if (!out.feed_permitted) {
        return false;
    }
    ams_watchdog_policy_record_feed(&state, in.now_ms, out.block_reason);
    if (state.feed_count != 1U) {
        return false;
    }

    stale = ams_watchdog_heartbeat_update(&monitor_state, 5002U, oracle_mask());
    in.now_ms = 5002U;
    in.stale_mask = stale;
    ams_watchdog_policy_evaluate(&state, &in, &out);
    if (out.feed_permitted || out.block_reason != AMS_WATCHDOG_BLOCK_HEARTBEAT ||
        state.feed_count != 1U) {
        return false;
    }

    /* Stop-feed dominates healthy heartbeat publication and is not part of the
     * monitor state, so an unrelated kick cannot clear it. */
    (void)ams_watchdog_heartbeat_kick(&monitor_state, AMS_WATCHDOG_HEARTBEAT_FAN, 5100U);
    (void)ams_watchdog_heartbeat_kick(&monitor_state, AMS_WATCHDOG_HEARTBEAT_IMD, 5100U);
    in.now_ms = 5100U;
    in.stale_mask = ams_watchdog_heartbeat_update(&monitor_state, 5100U, oracle_mask());
    in.stop_feed_test = true;
    ams_watchdog_policy_evaluate(&state, &in, &out);
    if (out.feed_permitted || out.block_reason != AMS_WATCHDOG_BLOCK_STOP_FEED_TEST) {
        return false;
    }

    return true;
}

int main(void)
{
    pthread_t fan_thread;
    pthread_t imd_thread;
    pthread_t supervisor_thread;
    const struct producer_arg fan = { AMS_WATCHDOG_HEARTBEAT_FAN, 1U };
    const struct producer_arg imd = { AMS_WATCHDOG_HEARTBEAT_IMD, 1U };
    unsigned failures;

    ams_watchdog_heartbeat_init(&monitor_state, 0U);
    atomic_init(&virtual_now, 0U);
    atomic_init(&producer_start, false);
    atomic_init(&invariant_failures, 0U);
    for (unsigned i = 0U; i < 8U; ++i) {
        atomic_init(&invariant_failure_kind[i], 0U);
    }

    if (pthread_create(&fan_thread, NULL, producer, (void *)&fan) != 0 ||
        pthread_create(&imd_thread, NULL, producer, (void *)&imd) != 0 ||
        pthread_create(&supervisor_thread, NULL, supervisor, NULL) != 0) {
        fprintf(stderr, "FAIL: pthread_create\n");
        return 2;
    }
    atomic_store_explicit(&producer_start, true, memory_order_release);

    (void)pthread_join(fan_thread, NULL);
    (void)pthread_join(imd_thread, NULL);
    (void)pthread_join(supervisor_thread, NULL);

    failures = atomic_load_explicit(&invariant_failures, memory_order_relaxed);
    pthread_mutex_lock(&monitor_lock);
    if (monitor_state.count[AMS_WATCHDOG_HEARTBEAT_FAN] != PRODUCER_KICKS) {
        failures++;
    }
    if (monitor_state.count[AMS_WATCHDOG_HEARTBEAT_IMD] != PRODUCER_KICKS) {
        failures++;
    }
    if (monitor_state.count[AMS_WATCHDOG_HEARTBEAT_CURRENT] != 0U ||
        (monitor_state.seen_mask & bit(AMS_WATCHDOG_HEARTBEAT_CURRENT)) != 0U) {
        failures++;
    }
    pthread_mutex_unlock(&monitor_lock);

    if (!deterministic_feed_boundary_checks()) {
        failures++;
    }

    if (failures != 0U) {
        fprintf(stderr, "watchdog concurrency SIL: %u failures", failures);
        for (unsigned i = 0U; i < 8U; ++i) {
            unsigned count = atomic_load_explicit(&invariant_failure_kind[i],
                                                  memory_order_relaxed);
            if (count != 0U) {
                fprintf(stderr, " kind%u=%u", i, count);
            }
        }
        fputc('\n', stderr);
        return 1;
    }

    printf("PASS watchdog concurrency SIL: fan=%u imd=%u supervisor=%u, 0 failures\n",
           PRODUCER_KICKS, PRODUCER_KICKS, SUPERVISOR_STEPS);
    return 0;
}
