#ifndef AMS_CORE_TIME_H_
#define AMS_CORE_TIME_H_

#include <stdbool.h>
#include <stdint.h>

#include <ams_core/ams_core_types.h>

#ifdef __cplusplus
extern "C" {
#endif

uint32_t ams_elapsed_ms(ams_time_ms_t now_ms,
                        ams_time_ms_t then_ms);

bool ams_age_within_ms(ams_time_ms_t now_ms,
                       ams_time_ms_t sample_ms,
                       uint32_t max_age_ms);

bool ams_age_expired_ms(ams_time_ms_t now_ms,
                        ams_time_ms_t sample_ms,
                        uint32_t max_age_ms);

#ifdef __cplusplus
}
#endif

#endif /* AMS_CORE_TIME_H_ */