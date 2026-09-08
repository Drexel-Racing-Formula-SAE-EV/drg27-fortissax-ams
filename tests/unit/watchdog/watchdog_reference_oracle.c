#include "watchdog_reference_oracle.h"

watchdog_reference_output_t watchdog_reference_step(watchdog_reference_input_t in)
{
    watchdog_reference_output_t out = {0};
    ams_watchdog_block_reason_t reason = AMS_WATCHDOG_BLOCK_NONE;
    bool startup = (uint32_t)(in.now_ms - in.boot_ms) < 3000U;

    /* Direct transcription of v2.6.27 ams_safety_watchdog_task_update()/ok()
     * for the pure software-liveness decision after hardware is started. */
    if (!in.runtime_enabled) {
        reason = AMS_WATCHDOG_BLOCK_NOT_ENABLED;
    } else if (in.panic) {
        reason = AMS_WATCHDOG_BLOCK_PANIC;
    } else if (in.stop_feed) {
        reason = AMS_WATCHDOG_BLOCK_STOP_FEED_TEST;
    } else if (startup) {
        reason = AMS_WATCHDOG_BLOCK_STARTUP_GRACE;
    } else if (in.safety_stale_mask != 0U) {
        reason = AMS_WATCHDOG_BLOCK_HEARTBEAT;
    } else if (in.rtos_fault || in.stack_critical) {
        reason = AMS_WATCHDOG_BLOCK_RTOS_INTEGRITY;
    }

    out.reason = reason;
    out.feed = in.hw_started &&
        ((reason == AMS_WATCHDOG_BLOCK_NONE) ||
         (reason == AMS_WATCHDOG_BLOCK_STARTUP_GRACE));
    out.ok = in.hw_started && reason == AMS_WATCHDOG_BLOCK_NONE;
    return out;
}
