#ifndef AMS_ADBMS_RECOVERY_H_
#define AMS_ADBMS_RECOVERY_H_
#include <stdbool.h>
#include <stdint.h>
typedef struct {
    uint32_t audit_count, interruption_count, attempt_count, success_count, failure_count;
    uint32_t generation, expected_fingerprint, observed_fingerprint;
    int32_t reason, result;
    bool pending, terminal, identity_valid, continuity_lost;
    uint8_t sid[6];
} ams_adbms_recovery_t;
#endif
