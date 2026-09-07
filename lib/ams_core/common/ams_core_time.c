#include <ams_core/ams_core_time.h>

uint32_t ams_elapsed_ms(ams_time_ms_t now_ms,
                        ams_time_ms_t then_ms)
{
    /*
     * Unsigned subtraction deliberately provides modulo-2^32
     * wrap-safe elapsed time.
     */
    return (uint32_t)(now_ms - then_ms);
}

bool ams_age_within_ms(ams_time_ms_t now_ms,
                       ams_time_ms_t sample_ms,
                       uint32_t max_age_ms)
{
    return ams_elapsed_ms(now_ms, sample_ms) <= max_age_ms;
}

bool ams_age_expired_ms(ams_time_ms_t now_ms,
                        ams_time_ms_t sample_ms,
                        uint32_t max_age_ms)
{
    return ams_elapsed_ms(now_ms, sample_ms) > max_age_ms;
}