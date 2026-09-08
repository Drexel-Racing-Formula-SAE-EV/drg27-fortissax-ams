#ifndef AMS_CORE_WATCHDOG_POLICY_H_
#define AMS_CORE_WATCHDOG_POLICY_H_

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AMS_WATCHDOG_TIMEOUT_MS 5000U
#define AMS_WATCHDOG_STARTUP_GRACE_MS 3000U

/* Preserve the exact v2.6.27 heartbeat bit numbering. */
typedef enum {
    AMS_WATCHDOG_HEARTBEAT_ADBMS = 0,
    AMS_WATCHDOG_HEARTBEAT_CURRENT,
    AMS_WATCHDOG_HEARTBEAT_TEMP,
    AMS_WATCHDOG_HEARTBEAT_CAN,
    AMS_WATCHDOG_HEARTBEAT_LOGGER,
    AMS_WATCHDOG_HEARTBEAT_IMD,
    AMS_WATCHDOG_HEARTBEAT_FAN,
    AMS_WATCHDOG_HEARTBEAT_ESTIMATOR,
    AMS_WATCHDOG_HEARTBEAT_COUNT
} ams_watchdog_heartbeat_id_t;

#define AMS_WATCHDOG_HEARTBEAT_BIT(id) \
    ((uint16_t)(1U << (uint16_t)(id)))
#define AMS_WATCHDOG_HEARTBEAT_VALID_MASK \
    ((uint16_t)((1U << (uint16_t)AMS_WATCHDOG_HEARTBEAT_COUNT) - 1U))

/* Stable numeric/log schema inherited from v2.6.27. Do not renumber. */
typedef enum {
    AMS_WATCHDOG_BLOCK_NONE = 0,
    AMS_WATCHDOG_BLOCK_NOT_ENABLED = 1,
    AMS_WATCHDOG_BLOCK_PANIC = 2,
    AMS_WATCHDOG_BLOCK_STARTUP_GRACE = 3,
    AMS_WATCHDOG_BLOCK_HEARTBEAT = 4,
    AMS_WATCHDOG_BLOCK_ADBMS_STALE = 5,
    AMS_WATCHDOG_BLOCK_CURRENT_STALE = 6,
    AMS_WATCHDOG_BLOCK_TEMP_STALE = 7,
    AMS_WATCHDOG_BLOCK_HARD_FAULT = 8,
    AMS_WATCHDOG_BLOCK_STOP_FEED_TEST = 9,
    AMS_WATCHDOG_BLOCK_START_FAILED = 10,
    AMS_WATCHDOG_BLOCK_RTOS_INTEGRITY = 11
} ams_watchdog_block_reason_t;

typedef struct {
    uint32_t feed_count;
    uint32_t block_count;
    uint32_t last_feed_ms;
    ams_watchdog_block_reason_t last_block_reason;
    ams_watchdog_block_reason_t last_logged_block_reason;
} ams_watchdog_policy_state_t;

typedef struct {
    uint32_t now_ms;
    uint32_t boot_ms;

    bool runtime_enabled;
    bool platform_ready_to_start;
    bool platform_started;
    bool platform_prepare_failed;
    bool platform_start_terminal_fault;
    bool platform_feed_terminal_fault;

    bool panic_latched;
    bool stop_feed_test;

    uint16_t oracle_required_mask;
    uint16_t migration_evidence_mask;
    uint16_t stale_mask;

    bool rtos_integrity_fault;
    uint16_t stack_critical_mask;
} ams_watchdog_policy_input_t;

typedef struct {
    ams_watchdog_block_reason_t block_reason;
    uint16_t effective_required_mask;
    uint16_t effective_stale_mask;

    bool startup_grace_active;
    bool request_start;
    bool feed_permitted;
    bool health_good;
    bool coverage_complete;
    bool reason_changed;
    bool count_block;
} ams_watchdog_policy_action_t;

void ams_watchdog_policy_init(ams_watchdog_policy_state_t *state);

void ams_watchdog_policy_evaluate(
    const ams_watchdog_policy_state_t *state,
    const ams_watchdog_policy_input_t *input,
    ams_watchdog_policy_action_t *action);

/* Call only after a real platform feed succeeded. */
void ams_watchdog_policy_record_feed(
    ams_watchdog_policy_state_t *state,
    uint32_t now_ms,
    ams_watchdog_block_reason_t status_reason);

/* Call once for a supervisor cycle in which feeding was actually blocked. */
void ams_watchdog_policy_record_block(
    ams_watchdog_policy_state_t *state,
    ams_watchdog_block_reason_t reason);

const char *ams_watchdog_block_reason_str(ams_watchdog_block_reason_t reason);

#ifdef __cplusplus
}
#endif

#endif /* AMS_CORE_WATCHDOG_POLICY_H_ */
