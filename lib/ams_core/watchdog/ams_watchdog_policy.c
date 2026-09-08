#include <ams_core/ams_watchdog_policy.h>

#include <limits.h>
#include <stddef.h>
#include <string.h>

static uint32_t saturating_increment(uint32_t value)
{
    return (value == UINT32_MAX) ? UINT32_MAX : value + 1U;
}

void ams_watchdog_policy_init(ams_watchdog_policy_state_t *state)
{
    if (state == NULL) {
        return;
    }

    memset(state, 0, sizeof(*state));
    state->last_block_reason = AMS_WATCHDOG_BLOCK_NONE;
    state->last_logged_block_reason = AMS_WATCHDOG_BLOCK_NONE;
}

void ams_watchdog_policy_evaluate(
    const ams_watchdog_policy_state_t *state,
    const ams_watchdog_policy_input_t *input,
    ams_watchdog_policy_action_t *action)
{
    ams_watchdog_block_reason_t reason;
    uint16_t effective_required;
    uint16_t effective_stale;
    bool startup_grace;

    if (action == NULL) {
        return;
    }

    memset(action, 0, sizeof(*action));

    /* A caller that loses the policy input must never inherit stale/uninitialised
     * feed permission from its output object. This is outside the normal
     * v2.6.27 decision domain, so fail closed as software-integrity unknown. */
    if ((state == NULL) || (input == NULL)) {
        action->block_reason = AMS_WATCHDOG_BLOCK_RTOS_INTEGRITY;
        action->count_block = true;
        action->reason_changed =
            (state != NULL) &&
            (state->last_logged_block_reason != AMS_WATCHDOG_BLOCK_RTOS_INTEGRITY);
        return;
    }

    /* Oracle/evidence masks are compile-time-valid in v2.6.27.  Treat an
     * out-of-domain bit as memory/configuration corruption: otherwise a bit
     * that has no heartbeat timeout/producer could appear to be covered and
     * silently authorize a feed. */
    if (((input->oracle_required_mask | input->migration_evidence_mask |
          input->stale_mask) &
         (uint16_t)~AMS_WATCHDOG_HEARTBEAT_VALID_MASK) != 0U) {
        action->block_reason = AMS_WATCHDOG_BLOCK_RTOS_INTEGRITY;
        action->count_block = true;
        action->reason_changed =
            (state != NULL) &&
            (state->last_logged_block_reason != AMS_WATCHDOG_BLOCK_RTOS_INTEGRITY);
        return;
    }

    effective_required =
        (uint16_t)(input->oracle_required_mask & input->migration_evidence_mask);
    effective_stale = (uint16_t)(input->stale_mask & effective_required);
    startup_grace =
        (uint32_t)(input->now_ms - input->boot_ms) < AMS_WATCHDOG_STARTUP_GRACE_MS;

    action->effective_required_mask = effective_required;
    action->effective_stale_mask = effective_stale;
    action->startup_grace_active = startup_grace;
    action->coverage_complete =
        input->migration_evidence_mask == input->oracle_required_mask;

    /* Preserve the v2.6.27 software-liveness decision ordering. Platform
     * start/feed failures are handled below at the mechanism boundary because
     * the legacy direct-register implementation had no fallible feed API. */
    if (!input->runtime_enabled) {
        reason = AMS_WATCHDOG_BLOCK_NOT_ENABLED;
    } else if (input->panic_latched) {
        reason = AMS_WATCHDOG_BLOCK_PANIC;
    } else if (input->stop_feed_test) {
        reason = AMS_WATCHDOG_BLOCK_STOP_FEED_TEST;
    } else if (startup_grace) {
        reason = AMS_WATCHDOG_BLOCK_STARTUP_GRACE;
    } else if (effective_stale != 0U) {
        reason = AMS_WATCHDOG_BLOCK_HEARTBEAT;
    } else if (input->rtos_integrity_fault || (input->stack_critical_mask != 0U)) {
        reason = AMS_WATCHDOG_BLOCK_RTOS_INTEGRITY;
    } else {
        reason = AMS_WATCHDOG_BLOCK_NONE;
    }

    if ((reason == AMS_WATCHDOG_BLOCK_NONE) ||
        (reason == AMS_WATCHDOG_BLOCK_STARTUP_GRACE)) {
        if (input->platform_feed_terminal_fault) {
            reason = AMS_WATCHDOG_BLOCK_RTOS_INTEGRITY;
        } else if (!input->platform_started) {
            if (input->platform_start_terminal_fault ||
                input->platform_prepare_failed ||
                !input->platform_ready_to_start) {
                reason = AMS_WATCHDOG_BLOCK_START_FAILED;
            } else {
                action->request_start = true;
            }
        } else {
            action->feed_permitted = true;
        }
    }

    action->block_reason = reason;
    action->health_good =
        (reason == AMS_WATCHDOG_BLOCK_NONE) && input->platform_started;
    action->count_block =
        (reason != AMS_WATCHDOG_BLOCK_NONE) &&
        (reason != AMS_WATCHDOG_BLOCK_NOT_ENABLED) &&
        (reason != AMS_WATCHDOG_BLOCK_STARTUP_GRACE);
    action->reason_changed =
        action->count_block &&
        (state != NULL) &&
        (state->last_logged_block_reason != reason);
}

void ams_watchdog_policy_record_feed(
    ams_watchdog_policy_state_t *state,
    uint32_t now_ms,
    ams_watchdog_block_reason_t status_reason)
{
    if (state == NULL) {
        return;
    }

    state->feed_count = saturating_increment(state->feed_count);
    state->last_feed_ms = now_ms;
    state->last_block_reason = status_reason;
}

void ams_watchdog_policy_record_block(
    ams_watchdog_policy_state_t *state,
    ams_watchdog_block_reason_t reason)
{
    if (state == NULL) {
        return;
    }

    state->last_block_reason = reason;

    if ((reason == AMS_WATCHDOG_BLOCK_NONE) ||
        (reason == AMS_WATCHDOG_BLOCK_NOT_ENABLED) ||
        (reason == AMS_WATCHDOG_BLOCK_STARTUP_GRACE)) {
        return;
    }

    state->block_count = saturating_increment(state->block_count);
    if (state->last_logged_block_reason != reason) {
        state->last_logged_block_reason = reason;
    }
}

const char *ams_watchdog_block_reason_str(ams_watchdog_block_reason_t reason)
{
    switch (reason) {
    case AMS_WATCHDOG_BLOCK_NONE: return "none";
    case AMS_WATCHDOG_BLOCK_NOT_ENABLED: return "not_enabled";
    case AMS_WATCHDOG_BLOCK_PANIC: return "panic";
    case AMS_WATCHDOG_BLOCK_STARTUP_GRACE: return "startup_grace";
    case AMS_WATCHDOG_BLOCK_HEARTBEAT: return "heartbeat";
    case AMS_WATCHDOG_BLOCK_ADBMS_STALE: return "adbms_stale";
    case AMS_WATCHDOG_BLOCK_CURRENT_STALE: return "current_stale";
    case AMS_WATCHDOG_BLOCK_TEMP_STALE: return "temp_stale";
    case AMS_WATCHDOG_BLOCK_HARD_FAULT: return "hard_fault";
    case AMS_WATCHDOG_BLOCK_STOP_FEED_TEST: return "stop_feed_test";
    case AMS_WATCHDOG_BLOCK_START_FAILED: return "start_failed";
    case AMS_WATCHDOG_BLOCK_RTOS_INTEGRITY: return "rtos_integrity";
    default: return "unknown";
    }
}
