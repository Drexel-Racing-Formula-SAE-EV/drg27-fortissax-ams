#include "watchdog_heartbeat_reference_oracle.h"

#include <limits.h>
#include <stddef.h>
#include <string.h>

/* Independent host adaptation of DER26 v2.6.27 app.c heartbeat logic.  Keep
 * the oracle values literal here so a production constant drift is observable
 * instead of being inherited by both sides of the differential test. */
uint32_t ref_heartbeat_timeout_ms(ams_watchdog_heartbeat_id_t id)
{
    switch (id) {
    case AMS_WATCHDOG_HEARTBEAT_ADBMS: return 3000U;
    case AMS_WATCHDOG_HEARTBEAT_CURRENT: return 200U;
    case AMS_WATCHDOG_HEARTBEAT_TEMP: return 3000U;
    case AMS_WATCHDOG_HEARTBEAT_CAN: return 2000U;
    case AMS_WATCHDOG_HEARTBEAT_LOGGER: return 2000U;
    case AMS_WATCHDOG_HEARTBEAT_IMD: return 500U;
    case AMS_WATCHDOG_HEARTBEAT_FAN: return 1000U;
    case AMS_WATCHDOG_HEARTBEAT_ESTIMATOR: return 500U;
    default: return 0U;
    }
}

void ref_heartbeat_init(ref_heartbeat_monitor_t *monitor, uint32_t now_ms)
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

bool ref_heartbeat_kick(ref_heartbeat_monitor_t *monitor,
                        ams_watchdog_heartbeat_id_t id,
                        uint32_t now_ms)
{
    if ((monitor == NULL) || ((unsigned int)id >= AMS_WATCHDOG_HEARTBEAT_COUNT)) {
        return false;
    }

    if (monitor->count[id] != 0U) {
        uint32_t gap = (uint32_t)(now_ms - monitor->last_ms[id]);
        monitor->last_gap_ms[id] = gap;
        if (gap > monitor->max_gap_ms[id]) {
            monitor->max_gap_ms[id] = gap;
        }
    }

    monitor->last_ms[id] = now_ms;
    if (monitor->count[id] != UINT32_MAX) {
        monitor->count[id]++;
    }
    monitor->seen_mask |= AMS_WATCHDOG_HEARTBEAT_BIT(id);
    monitor->seen_mask &= REF_HEARTBEAT_ALL_MASK;
    return true;
}

uint16_t ref_heartbeat_update(ref_heartbeat_monitor_t *monitor,
                              uint32_t now_ms,
                              uint16_t safety_required_mask)
{
    uint16_t stale = 0U;
    bool startup_grace;

    if (monitor == NULL) {
        /* Valid oracle calls never pass NULL; make the reference defensive too
         * so the differential harness itself cannot accidentally authorize. */
        return REF_HEARTBEAT_ALL_MASK;
    }

    startup_grace = (uint32_t)(now_ms - monitor->boot_ms) < 3000U;
    for (uint8_t i = 0U; i < (uint8_t)AMS_WATCHDOG_HEARTBEAT_COUNT; ++i) {
        ams_watchdog_heartbeat_id_t id = (ams_watchdog_heartbeat_id_t)i;
        uint16_t bit = AMS_WATCHDOG_HEARTBEAT_BIT(id);
        uint32_t timeout_ms = ref_heartbeat_timeout_ms(id);

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

    monitor->stale_mask = (uint16_t)(stale & REF_HEARTBEAT_ALL_MASK);
    monitor->safety_stale_mask =
        (uint16_t)(monitor->stale_mask & safety_required_mask & REF_HEARTBEAT_ALL_MASK);
    monitor->logger_stale_mask =
        (uint16_t)(monitor->stale_mask & REF_HEARTBEAT_LOGGER_MASK);
    return monitor->stale_mask;
}
