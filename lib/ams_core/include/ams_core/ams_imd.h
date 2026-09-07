#ifndef AMS_CORE_AMS_IMD_H_
#define AMS_CORE_AMS_IMD_H_

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Exact v2.6.27 stale-capture contract. */
#define AMS_IMD_CAPTURE_TIMEOUT_MS 250U

/* Exact v2.6.27 frequency/status mapping. */
typedef enum {
    AMS_IMD_SHORT_TO_CHASSIS_GROUND = 0,
    AMS_IMD_NORMAL                  = 1,
    AMS_IMD_UNDERVOLT               = 2,
    AMS_IMD_SPEED_START             = 3,
    AMS_IMD_DEVICE_ERROR            = 4,
    AMS_IMD_GROUND_FAULT            = 5,
    AMS_IMD_UNKNOWN                 = 0xFF
} ams_imd_status_t;

typedef struct {
    bool ok_hs;
    ams_imd_status_t status;

    uint32_t clock_freq_hz;

    uint32_t high_count;
    uint32_t total_count;
    float duty_percent;
    float frequency_hz;

    bool capture_started;
    volatile bool capture_seen;

    /* ISR/thread seqlock copied from the v2.6.27 IMD publication contract.
     * Even values are stable. Odd values mean a capture tuple is being
     * published. */
    volatile uint32_t capture_sequence;
    volatile uint32_t captured_high_count;
    volatile uint32_t captured_total_count;
    volatile uint32_t capture_count;
    volatile uint32_t last_capture_tick_ms;

    int ret;
} ams_imd_t;

void ams_imd_init(ams_imd_t *dev,
                  uint32_t clock_freq_hz,
                  bool capture_started);

void ams_imd_set_capture_started(ams_imd_t *dev, bool capture_started);

void ams_imd_capture_publish(ams_imd_t *dev,
                             uint32_t high_count,
                             uint32_t total_count,
                             uint32_t now_ms);

int ams_imd_read_at(ams_imd_t *dev,
                    bool status_sample_valid,
                    bool ok_hs,
                    uint32_t now_ms);

void ams_imd_force_fail_closed(ams_imd_t *dev);

bool ams_imd_is_ok(const ams_imd_t *dev);

#ifdef __cplusplus
}
#endif

#endif /* AMS_CORE_AMS_IMD_H_ */
