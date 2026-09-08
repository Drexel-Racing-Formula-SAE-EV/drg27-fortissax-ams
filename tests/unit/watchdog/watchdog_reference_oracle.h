#ifndef WATCHDOG_REFERENCE_ORACLE_H_
#define WATCHDOG_REFERENCE_ORACLE_H_

#include <stdbool.h>
#include <stdint.h>

#include <ams_core/ams_watchdog_policy.h>

typedef struct {
    bool runtime_enabled;
    bool hw_started;
    bool panic;
    bool stop_feed;
    uint32_t now_ms;
    uint32_t boot_ms;
    uint16_t safety_stale_mask;
    bool rtos_fault;
    bool stack_critical;
} watchdog_reference_input_t;

typedef struct {
    ams_watchdog_block_reason_t reason;
    bool feed;
    bool ok;
} watchdog_reference_output_t;

watchdog_reference_output_t watchdog_reference_step(watchdog_reference_input_t in);

#endif
