#include <ams_core/ams_watchdog_heartbeat.h>

#include <limits.h>
#include <stddef.h>
#include <string.h>

uint32_t ams_watchdog_heartbeat_timeout_ms(ams_watchdog_heartbeat_id_t id)
{
    switch (id) {
    case AMS_WATCHDOG_HEARTBEAT_ADBMS:
        return AMS_WATCHDOG_HEARTBEAT_ADBMS_TIMEOUT_MS;
    case AMS_WATCHDOG_HEARTBEAT_CURRENT:
        return AMS_WATCHDOG_HEARTBEAT_CURRENT_TIMEOUT_MS;
    case AMS_WATCHDOG_HEARTBEAT_TEMP:
        return AMS_WATCHDOG_HEARTBEAT_TEMP_TIMEOUT_MS;
    case AMS_WATCHDOG_HEARTBEAT_CAN:
        return AMS_WATCHDOG_HEARTBEAT_CAN_TIMEOUT_MS;
    case AMS_WATCHDOG_HEARTBEAT_LOGGER:
        return AMS_WATCHDOG_HEARTBEAT_LOGGER_TIMEOUT_MS;
    case AMS_WATCHDOG_HEARTBEAT_IMD:
        return AMS_WATCHDOG_HEARTBEAT_IMD_TIMEOUT_MS;
    case AMS_WATCHDOG_HEARTBEAT_FAN:
        return AMS_WATCHDOG_HEARTBEAT_FAN_TIMEOUT_MS;
    case AMS_WATCHDOG_HEARTBEAT_ESTIMATOR:
        return AMS_WATCHDOG_HEARTBEAT_ESTIMATOR_TIMEOUT_MS;
    default:
        return 0U;
    }
}

void ams_watchdog_heartbeat_init(ams_watchdog_heartbeat_monitor_t *monitor,
                                 uint32_t now_ms)
{
    if (monitor == NULL) {
        return;
    }

    memset(monitor, 0, sizeof(*monitor));
    monitor->boot_ms = now_ms;
    for (uint8_t i = 0U; i < (uint8_t)AMS_WATCHDOG_HEARTBEAT_COUNT; ++i) {
        monitor->last_ms[i] = now_ms;
    }
}

bool ams_watchdog_heartbeat_kick(ams_watchdog_heartbeat_monitor_t *monitor,
                                 ams_watchdog_heartbeat_id_t id,
                                 uint32_t now_ms)
{
    uint16_t bit;
    uint32_t prior_count;

    if ((monitor == NULL) || ((unsigned int)id >= AMS_WATCHDOG_HEARTBEAT_COUNT)) {
        return false;
    }

    prior_count = monitor->count[id];
    if (prior_count != 0U) {
        uint32_t gap = (uint32_t)(now_ms - monitor->last_ms[id]);
        monitor->last_gap_ms[id] = gap;
        if (gap > monitor->max_gap_ms[id]) {
            monitor->max_gap_ms[id] = gap;
        }
    }

    monitor->last_ms[id] = now_ms;
    if (prior_count != UINT32_MAX) {
        monitor->count[id] = prior_count + 1U;
    }

    bit = AMS_WATCHDOG_HEARTBEAT_BIT(id);
    monitor->seen_mask =
        (uint16_t)((monitor->seen_mask | bit) & AMS_WATCHDOG_HEARTBEAT_ALL_MASK);
    return true;
}

uint16_t ams_watchdog_heartbeat_update(
    ams_watchdog_heartbeat_monitor_t *monitor,
    uint32_t now_ms,
    uint16_t safety_required_mask)
{
    uint16_t stale = 0U;
    bool startup_grace;

    if (monitor == NULL) {
        /* The legacy app-level function returned 0 for a NULL app pointer, but
         * its caller returned before watchdog feeding.  At this portable API
         * boundary a NULL monitor must therefore fail closed rather than look
         * like a healthy all-fresh snapshot.  This is defensive behavior
         * outside the valid v2.6.27 oracle domain. */
        return AMS_WATCHDOG_HEARTBEAT_ALL_MASK;
    }

    startup_grace =
        (uint32_t)(now_ms - monitor->boot_ms) <
        AMS_WATCHDOG_HEARTBEAT_STARTUP_GRACE_MS;

    for (uint8_t i = 0U; i < (uint8_t)AMS_WATCHDOG_HEARTBEAT_COUNT; ++i) {
        ams_watchdog_heartbeat_id_t id = (ams_watchdog_heartbeat_id_t)i;
        uint16_t bit = AMS_WATCHDOG_HEARTBEAT_BIT(id);
        uint32_t timeout_ms = ams_watchdog_heartbeat_timeout_ms(id);

        if (timeout_ms == 0U) {
            continue;
        }

        if ((monitor->seen_mask & bit) == 0U) {
            if (!startup_grace) {
                stale |= bit;
            }
        } else if ((uint32_t)(now_ms - monitor->last_ms[i]) > timeout_ms) {
            stale |= bit;
        }
    }

    monitor->stale_mask =
        (uint16_t)(stale & AMS_WATCHDOG_HEARTBEAT_ALL_MASK);
    monitor->safety_stale_mask =
        (uint16_t)(monitor->stale_mask & safety_required_mask &
                   AMS_WATCHDOG_HEARTBEAT_ALL_MASK);
    monitor->logger_stale_mask =
        (uint16_t)(monitor->stale_mask & AMS_WATCHDOG_HEARTBEAT_LOGGER_MASK);

    return monitor->stale_mask;
}
