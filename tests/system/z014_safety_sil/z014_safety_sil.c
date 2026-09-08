#include <ams_core/ams_stack_health.h>
#include <ams_core/ams_watchdog_heartbeat.h>
#include <ams_core/ams_watchdog_policy.h>

#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SYSTEM_FUZZ_STEPS 250000U
#define SYSTEM_FUZZ_SEEDS 5U

#define CHECK(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        return false; \
    } \
} while (0)

typedef enum {
    MODEL_PLATFORM_READY = 0,
    MODEL_PLATFORM_STARTED,
    MODEL_PLATFORM_PREPARE_FAILED,
    MODEL_PLATFORM_START_TERMINAL,
    MODEL_PLATFORM_FEED_TERMINAL
} model_platform_state_t;

typedef struct {
    uint32_t boot_ms;
    uint32_t now_ms;
    ams_watchdog_heartbeat_monitor_t heartbeat;
    ams_watchdog_policy_state_t policy;

    model_platform_state_t platform;
    bool runtime_enabled;
    bool panic_latched;
    bool stop_feed_test;
    bool rtos_integrity_fault;
    uint16_t stack_critical_mask;

    /* Process faults deliberately do not enter the watchdog policy. They are
     * tracked here to prove that a live actor can fail its process function,
     * keep BMS_OK fail-low, and still prove software liveness. */
    bool fan_process_fault;
    bool imd_process_fault;

    /* Z-014 has no positive authority path. These can only ever remain false. */
    bool bms_ok_asserted;
    bool balance_authority;

    bool inject_start_ambiguous;
    bool inject_feed_failure;
    uint32_t start_attempts;
    uint32_t platform_feed_attempts;
    uint32_t platform_feed_successes;
    uint32_t fan_work_completions;
    uint32_t imd_work_completions;
} z014_model_t;

static uint16_t hb_bit(ams_watchdog_heartbeat_id_t id)
{
    return AMS_WATCHDOG_HEARTBEAT_BIT(id);
}

static uint16_t oracle_mask(void)
{
    return (uint16_t)(hb_bit(AMS_WATCHDOG_HEARTBEAT_ADBMS) |
                      hb_bit(AMS_WATCHDOG_HEARTBEAT_CURRENT) |
                      hb_bit(AMS_WATCHDOG_HEARTBEAT_TEMP) |
                      hb_bit(AMS_WATCHDOG_HEARTBEAT_CAN) |
                      hb_bit(AMS_WATCHDOG_HEARTBEAT_IMD) |
                      hb_bit(AMS_WATCHDOG_HEARTBEAT_FAN));
}

static uint16_t evidence_mask(void)
{
    /* The exact present Z-014 safety-evidence boundary. Deferred actor loops
     * are not permitted to fabricate liveness evidence. */
    return (uint16_t)(hb_bit(AMS_WATCHDOG_HEARTBEAT_FAN) |
                      hb_bit(AMS_WATCHDOG_HEARTBEAT_IMD));
}

static void model_init(z014_model_t *m, uint32_t boot_ms)
{
    memset(m, 0, sizeof(*m));
    m->boot_ms = boot_ms;
    m->now_ms = boot_ms;
    m->platform = MODEL_PLATFORM_READY;
    m->runtime_enabled = true;
    ams_watchdog_policy_init(&m->policy);
    ams_watchdog_heartbeat_init(&m->heartbeat, boot_ms);
}

static void model_advance(z014_model_t *m, uint32_t delta_ms)
{
    m->now_ms += delta_ms;
}

static bool model_actor_complete(z014_model_t *m,
                                 ams_watchdog_heartbeat_id_t id)
{
    if (id == AMS_WATCHDOG_HEARTBEAT_FAN) {
        m->fan_work_completions++;
    } else if (id == AMS_WATCHDOG_HEARTBEAT_IMD) {
        m->imd_work_completions++;
    } else {
        return false;
    }

    return ams_watchdog_heartbeat_kick(&m->heartbeat, id, m->now_ms);
}

static ams_watchdog_policy_input_t model_input(const z014_model_t *m,
                                               uint16_t stale_mask)
{
    ams_watchdog_policy_input_t in;
    memset(&in, 0, sizeof(in));

    in.now_ms = m->now_ms;
    in.boot_ms = m->boot_ms;
    in.runtime_enabled = m->runtime_enabled;
    in.platform_ready_to_start = m->platform == MODEL_PLATFORM_READY;
    in.platform_started = m->platform == MODEL_PLATFORM_STARTED;
    in.platform_prepare_failed = m->platform == MODEL_PLATFORM_PREPARE_FAILED;
    in.platform_start_terminal_fault = m->platform == MODEL_PLATFORM_START_TERMINAL;
    in.platform_feed_terminal_fault = m->platform == MODEL_PLATFORM_FEED_TERMINAL;
    in.panic_latched = m->panic_latched;
    in.stop_feed_test = m->stop_feed_test;
    in.oracle_required_mask = oracle_mask();
    in.migration_evidence_mask = evidence_mask();
    in.stale_mask = stale_mask;
    in.rtos_integrity_fault = m->rtos_integrity_fault;
    in.stack_critical_mask = m->stack_critical_mask;
    return in;
}

/* Model one production supervisor iteration, including the same-cycle
 * start/re-evaluate boundary and terminal feed-failure conversion. */
static ams_watchdog_policy_action_t model_supervisor_step(z014_model_t *m)
{
    uint16_t stale = ams_watchdog_heartbeat_update(&m->heartbeat,
                                                   m->now_ms,
                                                   oracle_mask());
    ams_watchdog_policy_input_t in = model_input(m, stale);
    ams_watchdog_policy_action_t action;

    ams_watchdog_policy_evaluate(&m->policy, &in, &action);

    if (action.request_start) {
        m->start_attempts++;
        if (m->inject_start_ambiguous) {
            m->platform = MODEL_PLATFORM_START_TERMINAL;
            m->inject_start_ambiguous = false;
            m->bms_ok_asserted = false;
        } else {
            m->platform = MODEL_PLATFORM_STARTED;
        }
        in = model_input(m, stale);
        ams_watchdog_policy_evaluate(&m->policy, &in, &action);
    }

    if (action.feed_permitted) {
        m->platform_feed_attempts++;
        if (m->inject_feed_failure) {
            m->inject_feed_failure = false;
            m->platform = MODEL_PLATFORM_FEED_TERMINAL;
            m->bms_ok_asserted = false;
            in = model_input(m, stale);
            ams_watchdog_policy_evaluate(&m->policy, &in, &action);
            if (action.block_reason != AMS_WATCHDOG_BLOCK_RTOS_INTEGRITY) {
                action.block_reason = AMS_WATCHDOG_BLOCK_RTOS_INTEGRITY;
                action.feed_permitted = false;
                action.health_good = false;
                action.count_block = true;
            }
            ams_watchdog_policy_record_block(&m->policy, action.block_reason);
        } else {
            m->platform_feed_successes++;
            ams_watchdog_policy_record_feed(&m->policy, m->now_ms,
                                            action.block_reason);
        }
    } else if (!action.request_start) {
        ams_watchdog_policy_record_block(&m->policy, action.block_reason);
    }

    return action;
}

static bool invariant_common(const z014_model_t *m,
                             const ams_watchdog_policy_action_t *action)
{
    uint32_t age = (uint32_t)(m->now_ms - m->boot_ms);

    CHECK(!m->bms_ok_asserted);
    CHECK(!m->balance_authority);
    CHECK(!action->coverage_complete);
    CHECK(action->effective_required_mask == evidence_mask());
    CHECK((action->effective_stale_mask & (uint16_t)~evidence_mask()) == 0U);
    CHECK(m->platform_feed_successes <= m->platform_feed_attempts);
    CHECK(m->policy.feed_count == m->platform_feed_successes);

    if (action->feed_permitted) {
        CHECK(m->platform == MODEL_PLATFORM_STARTED);
        CHECK(!m->panic_latched);
        CHECK(!m->stop_feed_test);
        CHECK(m->platform != MODEL_PLATFORM_START_TERMINAL);
        CHECK(m->platform != MODEL_PLATFORM_FEED_TERMINAL);
        /* The inherited v2.6.27 ordering deliberately lets startup grace mask
         * heartbeat/RTOS-integrity/stack diagnostics. Outside grace, no such
         * condition may be fed. */
        if (age >= AMS_WATCHDOG_STARTUP_GRACE_MS) {
            CHECK(action->effective_stale_mask == 0U);
            CHECK(!m->rtos_integrity_fault);
            CHECK(m->stack_critical_mask == 0U);
        }
    }

    return true;
}

static bool test_startup_and_exact_stale_boundaries(void)
{
    z014_model_t m;
    ams_watchdog_policy_action_t a;

    model_init(&m, 0U);
    m.platform = MODEL_PLATFORM_STARTED;

    m.now_ms = 2999U;
    a = model_supervisor_step(&m);
    CHECK(a.feed_permitted);
    CHECK(a.block_reason == AMS_WATCHDOG_BLOCK_STARTUP_GRACE);
    CHECK(m.policy.feed_count == 1U);

    m.now_ms = 3000U;
    a = model_supervisor_step(&m);
    CHECK(!a.feed_permitted);
    CHECK(a.block_reason == AMS_WATCHDOG_BLOCK_HEARTBEAT);
    CHECK(a.effective_stale_mask == evidence_mask());

    CHECK(model_actor_complete(&m, AMS_WATCHDOG_HEARTBEAT_FAN));
    CHECK(model_actor_complete(&m, AMS_WATCHDOG_HEARTBEAT_IMD));
    a = model_supervisor_step(&m);
    CHECK(a.feed_permitted);

    m.now_ms = 3500U; /* IMD age == 500 is fresh. */
    CHECK(model_actor_complete(&m, AMS_WATCHDOG_HEARTBEAT_FAN));
    a = model_supervisor_step(&m);
    CHECK(a.feed_permitted);

    m.now_ms = 3501U; /* IMD age == 501 is stale. */
    a = model_supervisor_step(&m);
    CHECK(!a.feed_permitted);
    CHECK(a.effective_stale_mask == hb_bit(AMS_WATCHDOG_HEARTBEAT_IMD));

    CHECK(model_actor_complete(&m, AMS_WATCHDOG_HEARTBEAT_IMD));
    a = model_supervisor_step(&m);
    CHECK(a.feed_permitted);

    m.now_ms = 4501U; /* FAN last completed at 3500: age 1001. */
    CHECK(model_actor_complete(&m, AMS_WATCHDOG_HEARTBEAT_IMD));
    a = model_supervisor_step(&m);
    CHECK(!a.feed_permitted);
    CHECK(a.effective_stale_mask == hb_bit(AMS_WATCHDOG_HEARTBEAT_FAN));

    return true;
}

static bool test_placeholder_evidence_cannot_authorize(void)
{
    z014_model_t m;
    ams_watchdog_policy_action_t a;

    model_init(&m, 0U);
    m.platform = MODEL_PLATFORM_STARTED;
    m.now_ms = 4000U;

    /* Deliberately publish all deferred heartbeat IDs. They may exist in the
     * portable monitor but cannot become Z-014 feed evidence. */
    CHECK(ams_watchdog_heartbeat_kick(&m.heartbeat, AMS_WATCHDOG_HEARTBEAT_ADBMS, m.now_ms));
    CHECK(ams_watchdog_heartbeat_kick(&m.heartbeat, AMS_WATCHDOG_HEARTBEAT_CURRENT, m.now_ms));
    CHECK(ams_watchdog_heartbeat_kick(&m.heartbeat, AMS_WATCHDOG_HEARTBEAT_TEMP, m.now_ms));
    CHECK(ams_watchdog_heartbeat_kick(&m.heartbeat, AMS_WATCHDOG_HEARTBEAT_CAN, m.now_ms));
    CHECK(ams_watchdog_heartbeat_kick(&m.heartbeat, AMS_WATCHDOG_HEARTBEAT_ESTIMATOR, m.now_ms));

    a = model_supervisor_step(&m);
    CHECK(!a.feed_permitted);
    CHECK(a.block_reason == AMS_WATCHDOG_BLOCK_HEARTBEAT);
    CHECK(a.effective_stale_mask == evidence_mask());
    CHECK(!a.coverage_complete);

    CHECK(model_actor_complete(&m, AMS_WATCHDOG_HEARTBEAT_FAN));
    CHECK(model_actor_complete(&m, AMS_WATCHDOG_HEARTBEAT_IMD));
    a = model_supervisor_step(&m);
    CHECK(a.feed_permitted);
    CHECK(!a.coverage_complete);

    return true;
}

static bool test_process_faults_are_not_software_death(void)
{
    z014_model_t m;
    ams_watchdog_policy_action_t a;

    model_init(&m, 0U);
    m.platform = MODEL_PLATFORM_STARTED;
    m.now_ms = 4000U;
    m.fan_process_fault = true;
    m.imd_process_fault = true;

    /* Production FAN/IMD code kicks after completing the real work/fail-low
     * handling even when its process result is faulted. */
    CHECK(model_actor_complete(&m, AMS_WATCHDOG_HEARTBEAT_FAN));
    CHECK(model_actor_complete(&m, AMS_WATCHDOG_HEARTBEAT_IMD));
    a = model_supervisor_step(&m);
    CHECK(a.feed_permitted);
    CHECK(a.block_reason == AMS_WATCHDOG_BLOCK_NONE);
    CHECK(!m.bms_ok_asserted);

    /* Software death is different: stop completing IMD work. */
    m.now_ms = 4501U;
    CHECK(model_actor_complete(&m, AMS_WATCHDOG_HEARTBEAT_FAN));
    a = model_supervisor_step(&m);
    CHECK(!a.feed_permitted);
    CHECK(a.block_reason == AMS_WATCHDOG_BLOCK_HEARTBEAT);

    return true;
}

static bool test_stack_and_integrity_fail_closed(void)
{
    z014_model_t m;
    ams_watchdog_policy_action_t a;
    ams_stack_health_t h;

    model_init(&m, 0U);
    m.platform = MODEL_PLATFORM_STARTED;
    m.now_ms = 4000U;
    CHECK(model_actor_complete(&m, AMS_WATCHDOG_HEARTBEAT_FAN));
    CHECK(model_actor_complete(&m, AMS_WATCHDOG_HEARTBEAT_IMD));

    h = ams_stack_health_evaluate(1536U, 300U, true, true);
    CHECK(h.warning);
    CHECK(!h.critical);
    /* Warning is proactive evidence, not a reset trigger. */
    a = model_supervisor_step(&m);
    CHECK(a.feed_permitted);

    h = ams_stack_health_evaluate(1536U, 255U, true, true);
    CHECK(h.warning);
    CHECK(h.critical);
    m.stack_critical_mask = 1U;
    a = model_supervisor_step(&m);
    CHECK(!a.feed_permitted);
    CHECK(a.block_reason == AMS_WATCHDOG_BLOCK_RTOS_INTEGRITY);

    h = ams_stack_health_evaluate(1536U, 1536U, false, true);
    CHECK(!h.query_valid);
    CHECK(h.warning);
    CHECK(h.critical);
    m.stack_critical_mask = 0U;
    m.rtos_integrity_fault = true; /* query/resource failure maps fail-closed */
    a = model_supervisor_step(&m);
    CHECK(!a.feed_permitted);
    CHECK(a.block_reason == AMS_WATCHDOG_BLOCK_RTOS_INTEGRITY);

    m.rtos_integrity_fault = false;
    m.panic_latched = true;
    a = model_supervisor_step(&m);
    CHECK(!a.feed_permitted);
    CHECK(a.block_reason == AMS_WATCHDOG_BLOCK_PANIC);

    m.panic_latched = false;
    m.stop_feed_test = true;
    a = model_supervisor_step(&m);
    CHECK(!a.feed_permitted);
    CHECK(a.block_reason == AMS_WATCHDOG_BLOCK_STOP_FEED_TEST);

    return true;
}

static bool test_platform_terminal_faults(void)
{
    z014_model_t m;
    ams_watchdog_policy_action_t a;

    model_init(&m, 0U);
    m.now_ms = 1000U;
    CHECK(model_actor_complete(&m, AMS_WATCHDOG_HEARTBEAT_FAN));
    CHECK(model_actor_complete(&m, AMS_WATCHDOG_HEARTBEAT_IMD));

    /* Healthy prepare/start crosses the irreversible boundary exactly once and
     * can feed in the same supervisor iteration. */
    a = model_supervisor_step(&m);
    CHECK(a.feed_permitted);
    CHECK(m.platform == MODEL_PLATFORM_STARTED);
    CHECK(m.start_attempts == 1U);
    CHECK(m.platform_feed_successes == 1U);

    model_init(&m, 0U);
    m.now_ms = 1000U;
    CHECK(model_actor_complete(&m, AMS_WATCHDOG_HEARTBEAT_FAN));
    CHECK(model_actor_complete(&m, AMS_WATCHDOG_HEARTBEAT_IMD));
    m.inject_start_ambiguous = true;
    a = model_supervisor_step(&m);
    CHECK(!a.feed_permitted);
    CHECK(a.block_reason == AMS_WATCHDOG_BLOCK_START_FAILED);
    CHECK(m.platform == MODEL_PLATFORM_START_TERMINAL);
    CHECK(m.start_attempts == 1U);
    a = model_supervisor_step(&m);
    CHECK(m.start_attempts == 1U); /* terminal means never retry */
    CHECK(!a.feed_permitted);

    model_init(&m, 0U);
    m.platform = MODEL_PLATFORM_STARTED;
    m.now_ms = 1000U;
    CHECK(model_actor_complete(&m, AMS_WATCHDOG_HEARTBEAT_FAN));
    CHECK(model_actor_complete(&m, AMS_WATCHDOG_HEARTBEAT_IMD));
    m.inject_feed_failure = true;
    a = model_supervisor_step(&m);
    CHECK(!a.feed_permitted);
    CHECK(a.block_reason == AMS_WATCHDOG_BLOCK_RTOS_INTEGRITY);
    CHECK(m.platform == MODEL_PLATFORM_FEED_TERMINAL);
    CHECK(m.policy.feed_count == 0U);
    CHECK(m.platform_feed_successes == 0U);
    CHECK(m.platform_feed_attempts == 1U);
    a = model_supervisor_step(&m);
    CHECK(!a.feed_permitted);
    CHECK(m.platform_feed_attempts == 1U); /* terminal means no more feed calls */

    model_init(&m, 0U);
    m.platform = MODEL_PLATFORM_PREPARE_FAILED;
    m.now_ms = 1000U;
    a = model_supervisor_step(&m);
    CHECK(!a.feed_permitted);
    CHECK(!a.request_start);
    CHECK(a.block_reason == AMS_WATCHDOG_BLOCK_START_FAILED);
    CHECK(m.start_attempts == 0U);

    return true;
}

static bool test_wrap_safe_system_timeline(void)
{
    z014_model_t m;
    ams_watchdog_policy_action_t a;
    const uint32_t boot = UINT32_MAX - 1000U;

    model_init(&m, boot);
    m.platform = MODEL_PLATFORM_STARTED;
    model_advance(&m, 100U);
    CHECK(model_actor_complete(&m, AMS_WATCHDOG_HEARTBEAT_FAN));
    CHECK(model_actor_complete(&m, AMS_WATCHDOG_HEARTBEAT_IMD));

    model_advance(&m, 400U); /* crosses wrap only later; both fresh */
    a = model_supervisor_step(&m);
    CHECK(a.feed_permitted);

    model_advance(&m, 2600U); /* total boot age 3100; both now stale */
    a = model_supervisor_step(&m);
    CHECK(!a.feed_permitted);
    CHECK(a.block_reason == AMS_WATCHDOG_BLOCK_HEARTBEAT);

    CHECK(model_actor_complete(&m, AMS_WATCHDOG_HEARTBEAT_FAN));
    CHECK(model_actor_complete(&m, AMS_WATCHDOG_HEARTBEAT_IMD));
    a = model_supervisor_step(&m);
    CHECK(a.feed_permitted);
    return true;
}

static uint32_t rng_next(uint32_t *state)
{
    uint32_t x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

static bool run_seeded_scheduler_abuse(uint32_t seed, uint32_t steps,
                                       uint64_t *supervisor_steps_out,
                                       uint64_t *actor_steps_out)
{
    z014_model_t m;
    uint32_t rng = seed == 0U ? 1U : seed;
    uint64_t supervisor_steps = 0U;
    uint64_t actor_steps = 0U;

    model_init(&m, UINT32_MAX - (rng & 0x1fffU));
    m.platform = MODEL_PLATFORM_STARTED;

    for (uint32_t i = 0U; i < steps; ++i) {
        uint32_t r = rng_next(&rng);
        uint32_t event = r % 23U;

        model_advance(&m, (r >> 8) % 41U);

        switch (event) {
        case 0U:
        case 1U:
        case 2U:
        case 3U:
            CHECK(model_actor_complete(&m, AMS_WATCHDOG_HEARTBEAT_FAN));
            actor_steps++;
            break;
        case 4U:
        case 5U:
        case 6U:
        case 7U:
        case 8U:
            CHECK(model_actor_complete(&m, AMS_WATCHDOG_HEARTBEAT_IMD));
            actor_steps++;
            break;
        case 9U:
            /* Deferred actor noise can never become effective evidence. */
            CHECK(ams_watchdog_heartbeat_kick(&m.heartbeat,
                                               AMS_WATCHDOG_HEARTBEAT_CURRENT,
                                               m.now_ms));
            break;
        case 10U:
            CHECK(ams_watchdog_heartbeat_kick(&m.heartbeat,
                                               AMS_WATCHDOG_HEARTBEAT_ADBMS,
                                               m.now_ms));
            break;
        case 11U:
            CHECK(ams_watchdog_heartbeat_kick(&m.heartbeat,
                                               AMS_WATCHDOG_HEARTBEAT_TEMP,
                                               m.now_ms));
            break;
        case 12U:
            CHECK(ams_watchdog_heartbeat_kick(&m.heartbeat,
                                               AMS_WATCHDOG_HEARTBEAT_CAN,
                                               m.now_ms));
            break;
        case 13U:
            m.fan_process_fault = !m.fan_process_fault;
            break;
        case 14U:
            m.imd_process_fault = !m.imd_process_fault;
            break;
        case 15U:
            /* Exercise a critical stack episode for one decision, then allow
             * recovery in this SIL model. Production policy may separately
             * latch/fail-low the related safety output. */
            m.stack_critical_mask = (uint16_t)(1U << ((r >> 16) & 7U));
            break;
        case 16U:
            m.rtos_integrity_fault = true;
            break;
        case 17U:
            m.stop_feed_test = true;
            break;
        case 18U:
            m.runtime_enabled = false;
            break;
        case 19U:
            /* Reset the non-latched test disturbances; this simulates a new
             * injected episode, not a real recovery policy claim. */
            m.stack_critical_mask = 0U;
            m.rtos_integrity_fault = false;
            m.stop_feed_test = false;
            m.runtime_enabled = true;
            break;
        default: {
            uint32_t feed_before = m.platform_feed_successes;
            ams_watchdog_policy_action_t a = model_supervisor_step(&m);
            supervisor_steps++;
            CHECK(invariant_common(&m, &a));

            /* A feed counter can advance only on an actual successful platform
             * feed event from this same supervisor step. */
            CHECK((m.platform_feed_successes - feed_before) <= 1U);
            if (m.platform_feed_successes != feed_before) {
                CHECK(a.feed_permitted);
            }

            /* One-shot software-integrity injections remain active until an
             * explicit reset event above; actor publications cannot clear them. */
            if ((m.stop_feed_test || m.rtos_integrity_fault ||
                 m.stack_critical_mask != 0U) &&
                (uint32_t)(m.now_ms - m.boot_ms) >= AMS_WATCHDOG_STARTUP_GRACE_MS) {
                CHECK(m.platform_feed_successes == feed_before);
            }
            break;
        }
        }
    }

    *supervisor_steps_out += supervisor_steps;
    *actor_steps_out += actor_steps;
    return true;
}

static bool test_seeded_scheduler_abuse(void)
{
    const uint32_t seeds[SYSTEM_FUZZ_SEEDS] = {
        1U, 0x12345678U, 0x00C0FFEEU, 0xDEADBEEFU, 0x31415926U
    };
    uint64_t supervisor_steps = 0U;
    uint64_t actor_steps = 0U;

    for (size_t i = 0U; i < SYSTEM_FUZZ_SEEDS; ++i) {
        CHECK(run_seeded_scheduler_abuse(seeds[i], SYSTEM_FUZZ_STEPS,
                                         &supervisor_steps, &actor_steps));
    }

    printf("PASS Z-014 integrated seeded scheduler SIL: seeds=%u steps=%u supervisor=%llu actors=%llu\n",
           SYSTEM_FUZZ_SEEDS, SYSTEM_FUZZ_STEPS,
           (unsigned long long)supervisor_steps,
           (unsigned long long)actor_steps);
    return true;
}

int main(void)
{
    unsigned passed = 0U;

#define RUN(test_fn) do { \
    if (!(test_fn())) { \
        return 1; \
    } \
    passed++; \
} while (0)

    RUN(test_startup_and_exact_stale_boundaries);
    RUN(test_placeholder_evidence_cannot_authorize);
    RUN(test_process_faults_are_not_software_death);
    RUN(test_stack_and_integrity_fail_closed);
    RUN(test_platform_terminal_faults);
    RUN(test_wrap_safe_system_timeline);
    RUN(test_seeded_scheduler_abuse);

#undef RUN

    printf("PASS Z-014 integrated safety SIL: %u scenario classes, BMS/balance authority absent\n",
           passed);
    return 0;
}
