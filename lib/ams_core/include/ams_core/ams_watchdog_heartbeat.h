#ifndef AMS_CORE_WATCHDOG_HEARTBEAT_H_
#define AMS_CORE_WATCHDOG_HEARTBEAT_H_

#include <stdbool.h>
#include <stdint.h>

#include <ams_core/ams_watchdog_policy.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Exact v2.6.27 heartbeat timing oracle. Keep these independent from Zephyr
 * thread periods: the source intentionally gives several actors wider liveness
 * windows than their nominal release period. */
#define AMS_WATCHDOG_HEARTBEAT_STARTUP_GRACE_MS AMS_WATCHDOG_STARTUP_GRACE_MS
#define AMS_WATCHDOG_HEARTBEAT_ADBMS_TIMEOUT_MS 3000U
#define AMS_WATCHDOG_HEARTBEAT_CURRENT_TIMEOUT_MS 200U
#define AMS_WATCHDOG_HEARTBEAT_TEMP_TIMEOUT_MS 3000U
#define AMS_WATCHDOG_HEARTBEAT_CAN_TIMEOUT_MS 2000U
#define AMS_WATCHDOG_HEARTBEAT_LOGGER_TIMEOUT_MS 2000U
#define AMS_WATCHDOG_HEARTBEAT_IMD_TIMEOUT_MS 500U
#define AMS_WATCHDOG_HEARTBEAT_FAN_TIMEOUT_MS 1000U
#define AMS_WATCHDOG_HEARTBEAT_ESTIMATOR_TIMEOUT_MS 500U

#define AMS_WATCHDOG_HEARTBEAT_ALL_MASK AMS_WATCHDOG_HEARTBEAT_VALID_MASK
#define AMS_WATCHDOG_HEARTBEAT_LOGGER_MASK \
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
} ams_watchdog_heartbeat_monitor_t;

uint32_t ams_watchdog_heartbeat_timeout_ms(ams_watchdog_heartbeat_id_t id);

void ams_watchdog_heartbeat_init(ams_watchdog_heartbeat_monitor_t *monitor,
                                 uint32_t now_ms);

/* Returns false for a null monitor or invalid heartbeat ID. This function is
 * deliberately synchronization-free; the RTOS integration owns the bounded
 * critical section/spinlock around concurrent kick/update operations. */
bool ams_watchdog_heartbeat_kick(ams_watchdog_heartbeat_monitor_t *monitor,
                                 ams_watchdog_heartbeat_id_t id,
                                 uint32_t now_ms);

/* Recomputes all stale bits using the exact source semantics:
 * - unseen heartbeat becomes stale when global startup age >= 3000 ms;
 * - seen heartbeat is fresh at age == timeout and stale only at age > timeout;
 * - elapsed ages use uint32_t subtraction and are wrap-safe.
 * safety_required_mask is supplied by the migration/oracle policy so dynamic
 * IMD/estimator requirements never leak into this portable monitor. */
uint16_t ams_watchdog_heartbeat_update(
    ams_watchdog_heartbeat_monitor_t *monitor,
    uint32_t now_ms,
    uint16_t safety_required_mask);

#ifdef __cplusplus
}
#endif

#endif /* AMS_CORE_WATCHDOG_HEARTBEAT_H_ */
