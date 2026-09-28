#include <ams_core/ams_balance_shadow.h>
#include <string.h>

void ams_balance_shadow_evaluate(const ams_cell_image_t *cells,
                                bool owner_ready, uint32_t now_ms,
                                ams_balance_shadow_t *out)
{
    uint16_t minimum = UINT16_MAX;
    unsigned count = 0U;
    if (out == NULL) return;
    memset(out, 0, sizeof(*out));
    out->evaluated_ms = now_ms;
    if (cells == NULL || !owner_ready ||
        cells->usable_mask != AMS_CELL_IMAGE_MONITORED_MASK) return;
    for (unsigned i = 0U; i < AMS_CELL_IMAGE_MONITORED_COUNT; ++i) {
        uint16_t mv = cells->raw_mv[i];
        if (!cells->raw_valid[i] ||
            mv < AMS_CELL_IMAGE_VALID_MIN_MV || mv > AMS_CELL_IMAGE_VALID_MAX_MV ||
            (uint32_t)(now_ms - cells->last_update_ms[i]) > AMS_CELL_IMAGE_STALE_TIMEOUT_MS ||
            cells->consecutive_misses[i] > AMS_CELL_IMAGE_MAX_CONSEC_MISSES) return;
        if (mv >= AMS_BALANCE_SHADOW_START_MV && mv < minimum) minimum = mv;
    }
    out->valid = true;
    if (minimum == UINT16_MAX) return;
    out->cohort_min_mv = minimum;
    /* Oracle uses ascending physical cell index, not voltage ranking. */
    for (unsigned i = 0U; i < AMS_CELL_IMAGE_MONITORED_COUNT; ++i) {
        uint16_t mv = cells->raw_mv[i];
        if (mv >= AMS_BALANCE_SHADOW_START_MV && mv > minimum &&
            (uint16_t)(mv - minimum) > AMS_BALANCE_SHADOW_DELTA_MV &&
            count < AMS_BALANCE_SHADOW_MAX_CELLS) {
            out->candidate_mask |= (uint16_t)(1U << i);
            ++count;
        }
    }
}
