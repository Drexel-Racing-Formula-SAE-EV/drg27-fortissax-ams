#ifndef AMS_Z021_SUPPORT_H_
#define AMS_Z021_SUPPORT_H_
#include <ams_core/ams_adbms_protocol.h>
/* Portable host foundation only. No target adapter or hardware authority. */
#define AMS_Z021_SMBS 5U
#define AMS_Z021_DEVICES 6U
#define AMS_Z021_FULL_RING_MASK 0x3fU
typedef enum { AMS_Z021_FROM_A, AMS_Z021_FROM_B } ams_z021_direction_t;
/* Physical index is canonical from A: SMB0..4, then APM. Full-ring blocks. */
bool ams_z021_block_index(ams_z021_direction_t direction, unsigned physical,
                          bool write_order, unsigned *block);
typedef struct {
    ams_adbms_counter_tracker_t smb[AMS_Z021_SMBS];
    uint64_t generation;
    uint32_t awake_at_us, awake_budget_us;
    bool awake_token, in_flight, cleanup_required;
} ams_z021_ring_t;
/* First construction only; recovery uses interrupt and proven cleanup. */
void ams_z021_ring_reset(ams_z021_ring_t *r);
/* Invalidate on reset/initialization/uncertainty; never forget UNSNAP debt. */
void ams_z021_ring_interrupt(ams_z021_ring_t *r);
/* Caller must prove full-ring wake and supply a hardware-qualified time budget.
 * No default timing claim is made by this support layer. */
bool ams_z021_ring_awake(ams_z021_ring_t *r, uint8_t proven_mask,
                         uint32_t now_us, uint32_t budget_us);
/* Consume token before any APM traffic. Ticket belongs to this generation. */
bool ams_z021_ring_begin(ams_z021_ring_t *r, uint32_t now_us, uint64_t *ticket);
/* complete means successful UNSNAP/SNAP/reads/UNSNAP; exactly three increments.
 * Failed/uncertain transactions invalidate all five counter expectations. */
bool ams_z021_ring_finish(ams_z021_ring_t *r, uint64_t ticket,
                          bool complete, bool unsnap_proven);

bool ams_z021_ring_cleanup(ams_z021_ring_t *r, uint64_t generation, bool unsnap_proven);

typedef struct { float shunt_ohm, gain, offset_uv, vb1_ratio; int8_t polarity; } ams_z021_calibration_t;
typedef struct { bool conversion_seen; uint16_t last_conversion; } ams_z021_apm_history_t;
typedef struct {
    bool valid, current_valid, voltage_valid;
    int32_t current_raw;
    int16_t voltage_raw;
    float current_a, voltage_v;
    uint16_t conversion_count;
    uint32_t updated_ms;
} ams_z021_apm_sample_t;
ams_z021_calibration_t ams_z021_calibration_der(void);
ams_z021_calibration_t ams_z021_calibration_eval(void);
/* Feed complete eight-byte RDSTAT/RDIVB1/RDFLAG packets only after proven
 * UNSNAP completion. Reset history explicitly at a new ADI1/init epoch.
 * Calibration and expected SNAP counter must come from verified owner state. */
bool ams_z021_apm_decode(ams_z021_apm_history_t *history,
                         const ams_z021_calibration_t *cal,
                         const uint8_t status[8], const uint8_t ivb[8],
                         const uint8_t flag[8], uint8_t expected_counter,
                         bool unsnap_proven, bool dividers_verified_on,
                         uint32_t now_ms, ams_z021_apm_sample_t *out);
#endif
