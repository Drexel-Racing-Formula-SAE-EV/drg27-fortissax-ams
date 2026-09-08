#ifndef AMS_PLATFORM_WATCHDOG_H_
#define AMS_PLATFORM_WATCHDOG_H_

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AMS_WATCHDOG_PLATFORM_TIMEOUT_MS 5000U

typedef enum {
    AMS_WATCHDOG_PLATFORM_UNPREPARED = 0,
    AMS_WATCHDOG_PLATFORM_READY_NOT_STARTED,
    AMS_WATCHDOG_PLATFORM_PREPARE_FAILED_RETRYABLE,
    AMS_WATCHDOG_PLATFORM_STARTED,
    AMS_WATCHDOG_PLATFORM_START_AMBIGUOUS_TERMINAL,
    AMS_WATCHDOG_PLATFORM_FEED_FAILED_TERMINAL
} ams_watchdog_platform_state_t;

typedef enum {
    AMS_WATCHDOG_START_RESULT_STARTED = 0,
    AMS_WATCHDOG_START_RESULT_ALREADY_STARTED = 1,
    AMS_WATCHDOG_START_RESULT_NOT_READY = -1,
    AMS_WATCHDOG_START_RESULT_AMBIGUOUS_TERMINAL = -2
} ams_watchdog_start_result_t;

typedef struct {
    ams_watchdog_platform_state_t state;
    int channel_id;
    int last_error;

    uint32_t prepare_attempt_count;
    uint32_t start_attempt_count;
    uint32_t feed_success_count;
    uint32_t feed_failure_count;

    bool reset_cause_checked;
    bool reset_cause_valid;
    bool reset_was_watchdog;
    int reset_cause_error;
    int reset_cause_clear_error;
} ams_watchdog_platform_status_t;

/* Prepare is explicitly reversible/safe: it checks device readiness, captures
 * reset cause and installs the 5000 ms RESET_SOC timeout, but does NOT call
 * wdt_setup() and therefore does not start STM32 IWDG. Failures here may be
 * retried by the safety supervisor. */
int ams_watchdog_platform_prepare(void);

/* Irreversible boundary. On STM32/Zephyr 4.4, wdt_setup() enables IWDG before
 * a later ready-wait can fail. Any negative setup return is therefore
 * classified as terminal/ambiguous and must never be retried or disabled. */
ams_watchdog_start_result_t ams_watchdog_platform_start(void);

/* Feed is legal only in STARTED state. Any driver feed error becomes terminal
 * platform integrity failure; there is no alternate feeder or recovery path. */
int ams_watchdog_platform_feed(void);

ams_watchdog_platform_status_t ams_watchdog_platform_status(void);

#ifdef __cplusplus
}
#endif

#endif /* AMS_PLATFORM_WATCHDOG_H_ */
