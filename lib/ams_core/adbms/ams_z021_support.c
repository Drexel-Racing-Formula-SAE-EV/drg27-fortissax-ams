#include <ams_core/ams_z021_support.h>
#include <math.h>
#include <string.h>

bool ams_z021_block_index(ams_z021_direction_t d, unsigned p, bool write, unsigned *block)
{
    if (block == NULL) return false;
    *block = AMS_Z021_DEVICES;
    if (p >= AMS_Z021_DEVICES || (d != AMS_Z021_FROM_A && d != AMS_Z021_FROM_B)) return false;
    unsigned read = d == AMS_Z021_FROM_A ? p : AMS_Z021_DEVICES - 1U - p;
    *block = write ? AMS_Z021_DEVICES - 1U - read : read;
    return true;
}
void ams_z021_ring_reset(ams_z021_ring_t *r)
{
    if (r == NULL) return;
    memset(r, 0, sizeof(*r)); r->generation = 1U;
}
void ams_z021_ring_interrupt(ams_z021_ring_t *r)
{
    if (r == NULL) return;
    r->awake_token = false; r->in_flight = false;
    if (r->generation != UINT64_MAX) ++r->generation;
    for (unsigned i=0; i<AMS_Z021_SMBS; ++i) ams_adbms_counter_unknown(&r->smb[i]);
}
bool ams_z021_ring_awake(ams_z021_ring_t *r, uint8_t mask, uint32_t now, uint32_t budget)
{
    if (r == NULL) return false;
    r->awake_token = false;
    if (r->in_flight || r->cleanup_required || r->generation == UINT64_MAX ||
        mask != AMS_Z021_FULL_RING_MASK || budget == 0U || budget > INT32_MAX) return false;
    for (unsigned i=0; i<AMS_Z021_SMBS; ++i) if (!r->smb[i].known) return false;
    r->awake_at_us = now; r->awake_budget_us = budget; r->awake_token = true;
    return true;
}
bool ams_z021_ring_begin(ams_z021_ring_t *r, uint32_t now, uint64_t *ticket)
{
    if (ticket != NULL) *ticket = 0U;
    if (r == NULL) return false;
    bool awake = r->awake_token; r->awake_token = false;
    if (ticket == NULL || !awake || r->in_flight || r->cleanup_required ||
        r->generation == UINT64_MAX ||
        (uint32_t)(now - r->awake_at_us) >= r->awake_budget_us) return false;
    for (unsigned i=0; i<AMS_Z021_SMBS; ++i) if (!r->smb[i].known) return false;
    ++r->generation; *ticket = r->generation;
    r->cleanup_required = true; r->in_flight = true;
    return true;
}
bool ams_z021_ring_finish(ams_z021_ring_t *r, uint64_t ticket, bool complete, bool unsnap)
{
    if (r == NULL || !r->in_flight || ticket != r->generation) return false;
    r->in_flight = false;
    if (unsnap) r->cleanup_required = false;
    if (!complete || !unsnap) { ams_z021_ring_interrupt(r); return false; }
    for (unsigned i=0; i<AMS_Z021_SMBS; ++i)
        for (unsigned n=0; n<3U; ++n)
            ams_adbms_counter_note_success(&r->smb[i], AMS_ADBMS_COUNTER_INCREMENT);
    return true;
}
bool ams_z021_ring_cleanup(ams_z021_ring_t *r, uint64_t generation, bool unsnap)
{
    if (!r || r->in_flight || generation != r->generation || !unsnap) return false;
    r->cleanup_required = false;
    return true;
}
ams_z021_calibration_t ams_z021_calibration_der(void)
{ return (ams_z021_calibration_t){0.000100f, 1.0f, 0.0f, 3622.0f/22.0f, 1}; }
ams_z021_calibration_t ams_z021_calibration_eval(void)
{ return (ams_z021_calibration_t){0.000050f, 1.0f, 0.0f, 3609.1f/9.1f, 1}; }
static bool calibration_valid(const ams_z021_calibration_t *c)
{
    return c && isfinite(c->shunt_ohm) && c->shunt_ohm > 0.0f && c->shunt_ohm < 0.01f &&
        isfinite(c->gain) && c->gain > 0.0f && c->gain < 100.0f &&
        isfinite(c->offset_uv) && isfinite(c->vb1_ratio) && c->vb1_ratio > 0.0f &&
        (c->polarity == 1 || c->polarity == -1);
}
bool ams_z021_apm_decode(ams_z021_apm_history_t *h, const ams_z021_calibration_t *c,
                         const uint8_t status[8], const uint8_t ivb[8], const uint8_t flag[8],
                         uint8_t expected, bool unsnap, bool dividers,
                         uint32_t now, ams_z021_apm_sample_t *out)
{
    if (out == NULL) return false;
    memset(out, 0, sizeof(*out));
    if (!h || !calibration_valid(c) || !status || !ivb || !flag || expected > 63U || !unsnap) return false;
    ams_adbms_counter_tracker_t tracker = {.known=true, .expected=expected};
    ams_adbms_packet_t s, v, f;
    if (ams_adbms_decode_packet(status,&tracker,&s) != AMS_ADBMS_RESULT_OK ||
        ams_adbms_decode_packet(ivb,&tracker,&v) != AMS_ADBMS_RESULT_OK ||
        ams_adbms_decode_packet(flag,&tracker,&f) != AMS_ADBMS_RESULT_OK ||
        (s.data[1] & 0x40U) == 0U) return false;
    uint32_t raw = (uint32_t)v.data[0] | ((uint32_t)v.data[1]<<8) | ((uint32_t)v.data[2]<<16);
    uint16_t vb = (uint16_t)((uint16_t)v.data[4] | ((uint16_t)v.data[5]<<8));
    if (raw == 0x03ffffU || raw == 0xfc0000U || vb == 0x7fffU || vb == 0x8000U) return false;
    uint16_t count = (uint16_t)(((uint16_t)(f.data[2]&0x1fU)<<6) | (f.data[3]>>2));
    if (h->conversion_seen ? count == h->last_conversion : count == 0U) return false;
    int32_t signed_i = raw & 0x800000U ? (int32_t)raw - 0x1000000 : (int32_t)raw;
    int32_t signed_v = vb & 0x8000U ? (int32_t)vb - 0x10000 : (int32_t)vb;
    float current = (((float)signed_i * 0.000001f - c->offset_uv * 1.0e-6f) / c->shunt_ohm) * c->gain * (float)c->polarity;
    float voltage = (float)signed_v * 0.0001f * c->vb1_ratio;
    if (!isfinite(current) || !isfinite(voltage)) return false;
    out->valid = true; out->current_valid = true; out->voltage_valid = dividers;
    out->current_raw = signed_i; out->voltage_raw = (int16_t)signed_v;
    out->current_a = current; out->voltage_v = voltage;
    out->conversion_count = count; out->updated_ms = now;
    h->conversion_seen = true; h->last_conversion = count;
    return true;
}
