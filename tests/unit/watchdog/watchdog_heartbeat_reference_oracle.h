#ifndef WATCHDOG_HEARTBEAT_REFERENCE_ORACLE_H_
#define WATCHDOG_HEARTBEAT_REFERENCE_ORACLE_H_

#include <stdbool.h>
#include <stdint.h>

#include <ams_core/ams_watchdog_policy.h>

#define REF_HEARTBEAT_ALL_MASK \
    ((uint16_t)((1U << (uint16_t)AMS_WATCHDOG_HEARTBEAT_COUNT) - 1U))
#define REF_HEARTBEAT_LOGGER_MASK \
    AMS_WATCHDOG_HEARTBEAT_BIT(AMS_WATCHDOG_HEARTBEAT_LOGGER)

typedef struct {
    uint32_t boot_ms;
    uint32_t last_ms[AMS_WATCHDOG_HEARTBEAT_COUNT];
    uint32_t last_gap_ms[AMS_WATCHDOG_HEARTBEAT_COUNT];
    uint32_t max_gap_ms[AMS_WATCHDOG_HEARTBEAT_COUNT];
    uint32_t count[AMS_WATCHDOG_HEARTBEAT_COUNT];
    uint16_t seen_mask;
    uint16_t stale_mask;
    uint16_t safety_stale_mask;
    uint16_t logger_stale_mask;
} ref_heartbeat_monitor_t;

uint32_t ref_heartbeat_timeout_ms(ams_watchdog_heartbeat_id_t id);
void ref_heartbeat_init(ref_heartbeat_monitor_t *monitor, uint32_t now_ms);
bool ref_heartbeat_kick(ref_heartbeat_monitor_t *monitor,
                        ams_watchdog_heartbeat_id_t id,
                        uint32_t now_ms);
uint16_t ref_heartbeat_update(ref_heartbeat_monitor_t *monitor,
                              uint32_t now_ms,
                              uint16_t safety_required_mask);

#endif
