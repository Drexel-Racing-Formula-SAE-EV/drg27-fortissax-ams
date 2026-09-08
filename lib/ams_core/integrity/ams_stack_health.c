#include <ams_core/ams_stack_health.h>

#include <stdint.h>

static size_t percent_ceiling(size_t value, size_t percent)
{
    size_t quotient = value / 100U;
    size_t remainder = value % 100U;
    return quotient * percent + (remainder * percent + 99U) / 100U;
}

static size_t threshold_bytes(size_t configured_bytes,
                              size_t floor_bytes,
                              size_t percent)
{
    size_t proportional = percent_ceiling(configured_bytes, percent);
    return proportional > floor_bytes ? proportional : floor_bytes;
}

size_t ams_stack_warning_threshold_bytes(size_t configured_bytes)
{
    return threshold_bytes(configured_bytes,
                           AMS_STACK_WARN_FLOOR_BYTES,
                           AMS_STACK_WARN_PERCENT);
}

size_t ams_stack_critical_threshold_bytes(size_t configured_bytes)
{
    return threshold_bytes(configured_bytes,
                           AMS_STACK_CRITICAL_FLOOR_BYTES,
                           AMS_STACK_CRITICAL_PERCENT);
}

ams_stack_health_t ams_stack_health_evaluate(size_t configured_bytes,
                                             size_t unused_bytes,
                                             bool query_valid,
                                             bool enabled)
{
    ams_stack_health_t health;

    health.warning_threshold_bytes =
        ams_stack_warning_threshold_bytes(configured_bytes);
    health.critical_threshold_bytes =
        ams_stack_critical_threshold_bytes(configured_bytes);
    health.query_valid = !enabled || query_valid;
    health.warning = false;
    health.critical = false;

    if (!enabled) {
        return health;
    }

    if (!query_valid) {
        /* Zephyr cannot distinguish "zero bytes remaining" from a failed
         * query unless the return code is retained. Unknown integrity is a
         * fail-closed critical condition, not fabricated headroom. */
        health.warning = true;
        health.critical = true;
        return health;
    }

    /* Exact FreeRTOS comparison semantics: equality is still acceptable. */
    health.warning = unused_bytes < health.warning_threshold_bytes;
    health.critical = unused_bytes < health.critical_threshold_bytes;
    return health;
}
