#ifndef AMS_CORE_STACK_HEALTH_H_
#define AMS_CORE_STACK_HEALTH_H_

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AMS_STACK_WARN_FLOOR_BYTES 384U
#define AMS_STACK_WARN_PERCENT 25U
#define AMS_STACK_CRITICAL_FLOOR_BYTES 256U
#define AMS_STACK_CRITICAL_PERCENT 15U

typedef struct {
    size_t warning_threshold_bytes;
    size_t critical_threshold_bytes;
    bool query_valid;
    bool warning;
    bool critical;
} ams_stack_health_t;

size_t ams_stack_warning_threshold_bytes(size_t configured_bytes);
size_t ams_stack_critical_threshold_bytes(size_t configured_bytes);

ams_stack_health_t ams_stack_health_evaluate(size_t configured_bytes,
                                             size_t unused_bytes,
                                             bool query_valid,
                                             bool enabled);

#ifdef __cplusplus
}
#endif

#endif /* AMS_CORE_STACK_HEALTH_H_ */
