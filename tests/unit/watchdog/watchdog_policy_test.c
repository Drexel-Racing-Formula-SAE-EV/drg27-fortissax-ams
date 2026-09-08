#include <ams_core/ams_stack_health.h>
#include <ams_core/ams_watchdog_policy.h>

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks;
static unsigned failures;

#define CHECK(expr) do { \
    checks++; \
    if (!(expr)) { \
        failures++; \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); \
    } \
} while (0)

static ams_watchdog_policy_input_t base_input(void)
{
    ams_watchdog_policy_input_t in;
    memset(&in, 0, sizeof(in));
    in.now_ms = 10000U;
    in.boot_ms = 0U;
    in.runtime_enabled = true;
    in.platform_ready_to_start = true;
    in.platform_started = true;
    in.oracle_required_mask =
        AMS_WATCHDOG_HEARTBEAT_BIT(AMS_WATCHDOG_HEARTBEAT_ADBMS) |
        AMS_WATCHDOG_HEARTBEAT_BIT(AMS_WATCHDOG_HEARTBEAT_CURRENT) |
        AMS_WATCHDOG_HEARTBEAT_BIT(AMS_WATCHDOG_HEARTBEAT_TEMP) |
        AMS_WATCHDOG_HEARTBEAT_BIT(AMS_WATCHDOG_HEARTBEAT_CAN) |
        AMS_WATCHDOG_HEARTBEAT_BIT(AMS_WATCHDOG_HEARTBEAT_IMD) |
        AMS_WATCHDOG_HEARTBEAT_BIT(AMS_WATCHDOG_HEARTBEAT_FAN);
    in.migration_evidence_mask = in.oracle_required_mask;
    return in;
}

static ams_watchdog_policy_action_t eval(ams_watchdog_policy_state_t *state,
                                         ams_watchdog_policy_input_t in)
{
    ams_watchdog_policy_action_t out;
    memset(&out, 0xA5, sizeof(out));
    ams_watchdog_policy_evaluate(state, &in, &out);
    return out;
}

static void test_block_reason_schema(void)
{
    CHECK(AMS_WATCHDOG_BLOCK_NONE == 0);
    CHECK(AMS_WATCHDOG_BLOCK_NOT_ENABLED == 1);
    CHECK(AMS_WATCHDOG_BLOCK_PANIC == 2);
    CHECK(AMS_WATCHDOG_BLOCK_STARTUP_GRACE == 3);
    CHECK(AMS_WATCHDOG_BLOCK_HEARTBEAT == 4);
    CHECK(AMS_WATCHDOG_BLOCK_ADBMS_STALE == 5);
    CHECK(AMS_WATCHDOG_BLOCK_CURRENT_STALE == 6);
    CHECK(AMS_WATCHDOG_BLOCK_TEMP_STALE == 7);
    CHECK(AMS_WATCHDOG_BLOCK_HARD_FAULT == 8);
    CHECK(AMS_WATCHDOG_BLOCK_STOP_FEED_TEST == 9);
    CHECK(AMS_WATCHDOG_BLOCK_START_FAILED == 10);
    CHECK(AMS_WATCHDOG_BLOCK_RTOS_INTEGRITY == 11);
}

static void test_boundaries(void)
{
    ams_watchdog_policy_state_t st;
    ams_watchdog_policy_input_t in = base_input();
    ams_watchdog_policy_action_t out;
    ams_watchdog_policy_init(&st);

    in.now_ms = 2999U;
    out = eval(&st, in);
    CHECK(out.block_reason == AMS_WATCHDOG_BLOCK_STARTUP_GRACE);
    CHECK(out.feed_permitted);
    CHECK(!out.health_good);
    CHECK(!out.count_block);

    in.now_ms = 3000U;
    out = eval(&st, in);
    CHECK(out.block_reason == AMS_WATCHDOG_BLOCK_NONE);
    CHECK(out.feed_permitted);
    CHECK(out.health_good);

    in.now_ms = 3001U;
    out = eval(&st, in);
    CHECK(out.block_reason == AMS_WATCHDOG_BLOCK_NONE);

    in.boot_ms = UINT32_MAX - 100U;
    in.now_ms = 50U; /* 151 ms after boot across wrap */
    out = eval(&st, in);
    CHECK(out.block_reason == AMS_WATCHDOG_BLOCK_STARTUP_GRACE);
}

static void test_masks_and_process_independence(void)
{
    ams_watchdog_policy_state_t st;
    ams_watchdog_policy_input_t in = base_input();
    ams_watchdog_policy_action_t out;
    uint16_t fan = AMS_WATCHDOG_HEARTBEAT_BIT(AMS_WATCHDOG_HEARTBEAT_FAN);
    uint16_t imd = AMS_WATCHDOG_HEARTBEAT_BIT(AMS_WATCHDOG_HEARTBEAT_IMD);
    uint16_t logger = AMS_WATCHDOG_HEARTBEAT_BIT(AMS_WATCHDOG_HEARTBEAT_LOGGER);
    uint16_t current = AMS_WATCHDOG_HEARTBEAT_BIT(AMS_WATCHDOG_HEARTBEAT_CURRENT);
    ams_watchdog_policy_init(&st);

    in.oracle_required_mask = fan | imd | current;
    in.migration_evidence_mask = fan | imd;
    in.stale_mask = current;
    out = eval(&st, in);
    CHECK(out.block_reason == AMS_WATCHDOG_BLOCK_NONE);
    CHECK(out.effective_required_mask == (fan | imd));
    CHECK(out.effective_stale_mask == 0U);
    CHECK(!out.coverage_complete);

    in.stale_mask = fan;
    out = eval(&st, in);
    CHECK(out.block_reason == AMS_WATCHDOG_BLOCK_HEARTBEAT);
    CHECK(out.effective_stale_mask == fan);

    in.stale_mask = logger;
    out = eval(&st, in);
    CHECK(out.block_reason == AMS_WATCHDOG_BLOCK_NONE);

    /* The API has deliberately no voltage/current/IMD-process/fan-process
     * booleans. Process faults therefore cannot accidentally become reset
     * policy inputs. */
    CHECK(sizeof(in.stale_mask) == sizeof(uint16_t));
}

static void test_precedence_and_start(void)
{
    ams_watchdog_policy_state_t st;
    ams_watchdog_policy_input_t in = base_input();
    ams_watchdog_policy_action_t out;
    ams_watchdog_policy_init(&st);

    in.runtime_enabled = false;
    in.panic_latched = true;
    out = eval(&st, in);
    CHECK(out.block_reason == AMS_WATCHDOG_BLOCK_NOT_ENABLED);

    in.runtime_enabled = true;
    out = eval(&st, in);
    CHECK(out.block_reason == AMS_WATCHDOG_BLOCK_PANIC);

    in.panic_latched = false;
    in.stop_feed_test = true;
    out = eval(&st, in);
    CHECK(out.block_reason == AMS_WATCHDOG_BLOCK_STOP_FEED_TEST);

    in.stop_feed_test = false;
    in.now_ms = 1000U;
    in.platform_started = false;
    out = eval(&st, in);
    CHECK(out.block_reason == AMS_WATCHDOG_BLOCK_STARTUP_GRACE);
    CHECK(out.request_start);
    CHECK(!out.feed_permitted);

    in.platform_ready_to_start = false;
    in.platform_prepare_failed = true;
    out = eval(&st, in);
    CHECK(out.block_reason == AMS_WATCHDOG_BLOCK_START_FAILED);
    CHECK(out.count_block);

    in.platform_prepare_failed = false;
    in.platform_start_terminal_fault = true;
    out = eval(&st, in);
    CHECK(out.block_reason == AMS_WATCHDOG_BLOCK_START_FAILED);

    in.platform_start_terminal_fault = false;
    in.platform_started = true;
    in.platform_feed_terminal_fault = true;
    out = eval(&st, in);
    CHECK(out.block_reason == AMS_WATCHDOG_BLOCK_RTOS_INTEGRITY);
}

static void test_rtos_and_counter_semantics(void)
{
    ams_watchdog_policy_state_t st;
    ams_watchdog_policy_input_t in = base_input();
    ams_watchdog_policy_action_t out;
    ams_watchdog_policy_init(&st);

    in.rtos_integrity_fault = true;
    out = eval(&st, in);
    CHECK(out.block_reason == AMS_WATCHDOG_BLOCK_RTOS_INTEGRITY);
    CHECK(!out.feed_permitted);

    in.rtos_integrity_fault = false;
    in.stack_critical_mask = 0x4U;
    out = eval(&st, in);
    CHECK(out.block_reason == AMS_WATCHDOG_BLOCK_RTOS_INTEGRITY);

    st.feed_count = UINT32_MAX - 1U;
    ams_watchdog_policy_record_feed(&st, 77U, AMS_WATCHDOG_BLOCK_NONE);
    CHECK(st.feed_count == UINT32_MAX);
    CHECK(st.last_feed_ms == 77U);
    ams_watchdog_policy_record_feed(&st, 88U, AMS_WATCHDOG_BLOCK_STARTUP_GRACE);
    CHECK(st.feed_count == UINT32_MAX);
    CHECK(st.last_feed_ms == 88U);

    st.block_count = UINT32_MAX - 1U;
    ams_watchdog_policy_record_block(&st, AMS_WATCHDOG_BLOCK_HEARTBEAT);
    CHECK(st.block_count == UINT32_MAX);
    CHECK(st.last_logged_block_reason == AMS_WATCHDOG_BLOCK_HEARTBEAT);
    ams_watchdog_policy_record_block(&st, AMS_WATCHDOG_BLOCK_HEARTBEAT);
    CHECK(st.block_count == UINT32_MAX);
    ams_watchdog_policy_record_block(&st, AMS_WATCHDOG_BLOCK_STARTUP_GRACE);
    CHECK(st.block_count == UINT32_MAX);
}


static void test_defensive_and_coverage_semantics(void)
{
    ams_watchdog_policy_state_t st;
    ams_watchdog_policy_input_t in = base_input();
    ams_watchdog_policy_action_t out;
    const uint16_t logger =
        AMS_WATCHDOG_HEARTBEAT_BIT(AMS_WATCHDOG_HEARTBEAT_LOGGER);
    const uint16_t final_mask = in.oracle_required_mask;

    ams_watchdog_policy_init(&st);

    memset(&out, 0xA5, sizeof(out));
    ams_watchdog_policy_evaluate(&st, NULL, &out);
    CHECK(out.block_reason == AMS_WATCHDOG_BLOCK_RTOS_INTEGRITY);
    CHECK(!out.feed_permitted);
    CHECK(!out.health_good);
    CHECK(out.count_block);
    CHECK(out.reason_changed);

    memset(&out, 0xA5, sizeof(out));
    ams_watchdog_policy_evaluate(NULL, &in, &out);
    CHECK(out.block_reason == AMS_WATCHDOG_BLOCK_RTOS_INTEGRITY);
    CHECK(!out.feed_permitted);
    CHECK(!out.health_good);
    CHECK(out.count_block);
    CHECK(!out.reason_changed);

    /* A NULL action is permitted only as a no-op API guard and must not
     * mutate policy state. */
    st.feed_count = 7U;
    st.block_count = 9U;
    ams_watchdog_policy_evaluate(&st, &in, NULL);
    CHECK(st.feed_count == 7U);
    CHECK(st.block_count == 9U);

    out = eval(&st, in);
    CHECK(out.coverage_complete);
    CHECK(out.effective_required_mask == final_mask);

    /* Unexpected evidence must never create a false FULL-coverage claim. */
    in.migration_evidence_mask = (uint16_t)(final_mask | logger);
    out = eval(&st, in);
    CHECK(!out.coverage_complete);
    CHECK(out.effective_required_mask == final_mask);
    CHECK(out.feed_permitted);

    /* Partial evidence is intentionally allowed only for current no-authority
     * migration validation. It never claims complete oracle coverage. */
    in.migration_evidence_mask =
        AMS_WATCHDOG_HEARTBEAT_BIT(AMS_WATCHDOG_HEARTBEAT_FAN) |
        AMS_WATCHDOG_HEARTBEAT_BIT(AMS_WATCHDOG_HEARTBEAT_IMD);
    in.stale_mask = 0U;
    out = eval(&st, in);
    CHECK(!out.coverage_complete);
    CHECK(out.feed_permitted);

    /* Bits outside the frozen heartbeat schema are corruption, not evidence. */
    in = base_input();
    in.oracle_required_mask |= UINT16_C(0x8000);
    out = eval(&st, in);
    CHECK(out.block_reason == AMS_WATCHDOG_BLOCK_RTOS_INTEGRITY);
    CHECK(!out.feed_permitted);
    CHECK(out.count_block);

    in = base_input();
    in.migration_evidence_mask |= UINT16_C(0x8000);
    out = eval(&st, in);
    CHECK(out.block_reason == AMS_WATCHDOG_BLOCK_RTOS_INTEGRITY);
    CHECK(!out.feed_permitted);

    /* Stale-mask corruption is also fail-closed even when the corrupted bit
     * is outside the currently effective evidence set. Silently masking it
     * would hide monitor/state corruption at the safety boundary. */
    in = base_input();
    in.stale_mask |= UINT16_C(0x8000);
    out = eval(&st, in);
    CHECK(out.block_reason == AMS_WATCHDOG_BLOCK_RTOS_INTEGRITY);
    CHECK(!out.feed_permitted);
    CHECK(out.count_block);
}

static void test_each_required_stale_and_recovery(void)
{
    ams_watchdog_policy_state_t st;
    ams_watchdog_policy_input_t in = base_input();
    ams_watchdog_policy_action_t out;
    const ams_watchdog_heartbeat_id_t ids[] = {
        AMS_WATCHDOG_HEARTBEAT_ADBMS,
        AMS_WATCHDOG_HEARTBEAT_CURRENT,
        AMS_WATCHDOG_HEARTBEAT_TEMP,
        AMS_WATCHDOG_HEARTBEAT_CAN,
        AMS_WATCHDOG_HEARTBEAT_IMD,
        AMS_WATCHDOG_HEARTBEAT_FAN,
    };

    ams_watchdog_policy_init(&st);
    for (size_t i = 0U; i < sizeof(ids) / sizeof(ids[0]); ++i) {
        in.stale_mask = AMS_WATCHDOG_HEARTBEAT_BIT(ids[i]);
        out = eval(&st, in);
        CHECK(out.block_reason == AMS_WATCHDOG_BLOCK_HEARTBEAT);
        CHECK(!out.feed_permitted);
        CHECK(out.count_block);
        CHECK(out.effective_stale_mask == in.stale_mask);
        CHECK(out.reason_changed == (i == 0U));
        ams_watchdog_policy_record_block(&st, out.block_reason);

        /* The same reason remains a blocked cycle but is no longer a new
         * log-transition reason. */
        out = eval(&st, in);
        CHECK(out.block_reason == AMS_WATCHDOG_BLOCK_HEARTBEAT);
        CHECK(out.count_block);
        CHECK(!out.reason_changed);

        in.stale_mask = 0U;
        out = eval(&st, in);
        CHECK(out.block_reason == AMS_WATCHDOG_BLOCK_NONE);
        CHECK(out.feed_permitted);
        CHECK(out.health_good);
        ams_watchdog_policy_record_feed(&st, in.now_ms, out.block_reason);

        /* Returning to heartbeat starvation is a new transition only after a
         * different block reason has actually been recorded, matching the
         * source's last-logged-reason semantics. A healthy feed does not erase
         * the retained last logged fault reason. */
        in.stale_mask = AMS_WATCHDOG_HEARTBEAT_BIT(ids[i]);
        out = eval(&st, in);
        CHECK(!out.reason_changed);
        in.stale_mask = 0U;
    }
}

static void test_warning_only_and_retryable_start_recovery(void)
{
    ams_watchdog_policy_state_t st;
    ams_watchdog_policy_input_t in = base_input();
    ams_watchdog_policy_action_t out;
    ams_stack_health_t h;

    ams_watchdog_policy_init(&st);

    h = ams_stack_health_evaluate(1536U, 383U, true, true);
    CHECK(h.warning);
    CHECK(!h.critical);
    in.stack_critical_mask = h.critical ? 1U : 0U;
    out = eval(&st, in);
    CHECK(out.block_reason == AMS_WATCHDOG_BLOCK_NONE);
    CHECK(out.feed_permitted);

    in.platform_started = false;
    in.platform_ready_to_start = false;
    in.platform_prepare_failed = true;
    out = eval(&st, in);
    CHECK(out.block_reason == AMS_WATCHDOG_BLOCK_START_FAILED);
    CHECK(!out.request_start);
    CHECK(!out.feed_permitted);
    ams_watchdog_policy_record_block(&st, out.block_reason);

    /* A provably pre-start prepare failure can recover and request the only
     * legitimate start transition on a later healthy supervisor cycle. */
    in.platform_prepare_failed = false;
    in.platform_ready_to_start = true;
    out = eval(&st, in);
    CHECK(out.request_start);
    CHECK(!out.feed_permitted);
    CHECK(out.block_reason == AMS_WATCHDOG_BLOCK_NONE);

    in.platform_started = true;
    in.platform_ready_to_start = false;
    out = eval(&st, in);
    CHECK(!out.request_start);
    CHECK(out.feed_permitted);
    CHECK(out.health_good);
}

static void test_stack_health(void)
{
    ams_stack_health_t h;

    CHECK(ams_stack_warning_threshold_bytes(768U) == 384U);
    CHECK(ams_stack_critical_threshold_bytes(768U) == 256U);
    CHECK(ams_stack_warning_threshold_bytes(8192U) == 2048U);
    CHECK(ams_stack_critical_threshold_bytes(8192U) == 1229U);

    h = ams_stack_health_evaluate(1536U, 384U, true, true);
    CHECK(!h.warning); /* equality safe */
    h = ams_stack_health_evaluate(1536U, 383U, true, true);
    CHECK(h.warning);
    CHECK(!h.critical);
    h = ams_stack_health_evaluate(1536U, 256U, true, true);
    CHECK(!h.critical); /* equality safe */
    h = ams_stack_health_evaluate(1536U, 255U, true, true);
    CHECK(h.critical);

    h = ams_stack_health_evaluate(8192U, 1229U, true, true);
    CHECK(!h.critical);
    h = ams_stack_health_evaluate(8192U, 1228U, true, true);
    CHECK(h.critical);

    h = ams_stack_health_evaluate(2048U, 1000U, false, true);
    CHECK(!h.query_valid);
    CHECK(h.warning);
    CHECK(h.critical);

    h = ams_stack_health_evaluate(2048U, 0U, false, false);
    CHECK(h.query_valid);
    CHECK(!h.warning);
    CHECK(!h.critical);
}

int main(void)
{
    test_block_reason_schema();
    test_boundaries();
    test_masks_and_process_independence();
    test_precedence_and_start();
    test_rtos_and_counter_semantics();
    test_defensive_and_coverage_semantics();
    test_each_required_stale_and_recovery();
    test_warning_only_and_retryable_start_recovery();
    test_stack_health();

    if (failures != 0U) {
        fprintf(stderr, "watchdog/stack directed: %u checks, %u failures\n", checks, failures);
        return 1;
    }

    printf("PASS watchdog/stack directed: %u checks, 0 failures\n", checks);
    return 0;
}
