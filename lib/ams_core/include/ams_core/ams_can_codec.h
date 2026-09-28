#ifndef AMS_CAN_CODEC_H_
#define AMS_CAN_CODEC_H_
#include <ams_core/ams_can_tx_scheduler.h>
typedef struct {
 uint32_t latest_sample_ms, sequence, calibration_id;
 uint16_t uncertainty_ma;
 bool valid, calibration_confident;
} ams_can_current_provenance_t;
/* Frozen v2.6.27 non-APM 0x68B encoder. Input must be one immutable window.
 * This carries provenance only; it does not grant current/pack authority. */
bool ams_can_encode_current_diagnostic(const ams_can_current_provenance_t *,
 uint32_t now,ams_can_tx_frame_t *);
#endif
