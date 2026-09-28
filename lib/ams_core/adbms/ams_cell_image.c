#include <ams_core/ams_adbms_protocol.h>
#include <ams_core/ams_cell_image.h>

#include <limits.h>
#include <string.h>

static uint8_t sat_inc_u8(uint8_t value)
{
    return value == UINT8_MAX ? UINT8_MAX : (uint8_t)(value + 1U);
}

static bool code_to_plausible_mv(int16_t code, uint16_t *mv)
{
    return ams_adbms_cell_code_to_mv(code, mv) &&
           (*mv >= AMS_CELL_IMAGE_VALID_MIN_MV) &&
           (*mv <= AMS_CELL_IMAGE_VALID_MAX_MV);
}

static void refresh_usable(ams_cell_image_t *image, uint32_t now_ms)
{
    uint16_t min_mv = UINT16_MAX;
    uint16_t max_mv = 0U;
    uint32_t sum_mv = 0U;
    uint8_t count = 0U;

    image->usable_mask = 0U;
    image->stale_mask = 0U;
    for (uint8_t cell = 0U; cell < AMS_CELL_IMAGE_MONITORED_COUNT; ++cell) {
        uint16_t bit = (uint16_t)(1U << cell);
        bool usable = false;
        if (image->raw_valid[cell]) {
            uint32_t age_ms = (uint32_t)(now_ms - image->last_update_ms[cell]);
            usable = (age_ms <= AMS_CELL_IMAGE_STALE_TIMEOUT_MS) &&
                     (image->consecutive_misses[cell] <= AMS_CELL_IMAGE_MAX_CONSEC_MISSES);
        }
        if (usable) {
            uint16_t mv = image->raw_mv[cell];
            image->usable_mask |= bit;
            count++;
            sum_mv += mv;
            if (mv < min_mv) {
                min_mv = mv;
            }
            if (mv > max_mv) {
                max_mv = mv;
            }
        } else {
            image->stale_mask |= bit;
        }
    }
    image->usable_count = count;
    image->sum_mv = sum_mv;
    image->min_mv = count == 0U ? 0U : min_mv;
    image->max_mv = max_mv;
}

void ams_cell_image_init(ams_cell_image_t *image)
{
    if (image != NULL) {
        memset(image, 0, sizeof(*image));
        image->stale_mask = AMS_CELL_IMAGE_MONITORED_MASK;
    }
}

void ams_cell_image_apply_raw(ams_cell_image_t *image,
                              const int16_t codes[AMS_CELL_IMAGE_REGISTER_COUNT],
                              uint16_t fresh_mask,
                              uint16_t bad_mask,
                              uint32_t now_ms)
{
    if ((image == NULL) || (codes == NULL)) {
        return;
    }
    image->updated_mask = 0U;
    image->jump_mask = 0U;
    image->stuck_mask = 0U;
    image->max_delta_mv = 0U;
    image->max_delta_cell = 0U;

    for (uint8_t cell = 0U; cell < AMS_CELL_IMAGE_MONITORED_COUNT; ++cell) {
        uint16_t bit = (uint16_t)(1U << cell);
        bool updated = ((fresh_mask & bit) != 0U) && ((bad_mask & bit) == 0U);
        uint16_t mv = 0U;

        if (updated && code_to_plausible_mv(codes[cell], &mv)) {
            if (image->raw_valid[cell]) {
                uint16_t previous = image->raw_mv[cell];
                uint16_t delta = mv >= previous ? (uint16_t)(mv - previous) : (uint16_t)(previous - mv);
                if (delta > image->max_delta_mv) {
                    image->max_delta_mv = delta;
                    image->max_delta_cell = cell;
                }
                if (delta >= AMS_CELL_IMAGE_JUMP_MV) {
                    image->jump_mask |= bit;
                }
                if (mv == previous) {
                    image->same_count[cell] = sat_inc_u8(image->same_count[cell]);
                    if (image->same_count[cell] >= AMS_CELL_IMAGE_STUCK_SAME_COUNT) {
                        image->stuck_mask |= bit;
                    }
                } else {
                    image->same_count[cell] = 0U;
                }
            } else {
                image->same_count[cell] = 0U;
            }
            image->raw_mv[cell] = mv;
            image->raw_valid[cell] = true;
            image->last_update_ms[cell] = now_ms;
            image->consecutive_misses[cell] = 0U;
            image->updated_mask |= bit;
        } else {
            image->consecutive_misses[cell] = sat_inc_u8(image->consecutive_misses[cell]);
        }
    }
    refresh_usable(image, now_ms);
}

void ams_cell_image_apply_avg8(ams_cell_image_t *image,
                               const int16_t codes[AMS_CELL_IMAGE_REGISTER_COUNT],
                               uint16_t fresh_mask,
                               uint16_t bad_mask)
{
    if ((image == NULL) || (codes == NULL)) {
        return;
    }
    image->avg8_usable_mask = 0U;
    for (uint8_t cell = 0U; cell < AMS_CELL_IMAGE_MONITORED_COUNT; ++cell) {
        uint16_t bit = (uint16_t)(1U << cell);
        uint16_t mv = 0U;
        if (((fresh_mask & bit) != 0U) && ((bad_mask & bit) == 0U) &&
            code_to_plausible_mv(codes[cell], &mv)) {
            image->avg8_mv[cell] = mv;
            image->avg8_usable_mask |= bit;
        }
    }
}

void ams_cell_image_invalidate_iir(ams_cell_image_t *image)
{
    if (image == NULL) {
        return;
    }
    image->iir_usable_mask = 0U;
    image->iir_ready = false;
    image->filtered_successful_epoch_count = 0U;
    image->filtered_last_epoch_valid = false;
    image->filtered_last_epoch_ms = 0U;
}

void ams_cell_image_apply_iir(ams_cell_image_t *image,
                              const int16_t codes[AMS_CELL_IMAGE_REGISTER_COUNT],
                              uint16_t fresh_mask,
                              uint16_t bad_mask,
                              uint32_t now_ms,
                              bool complete_epoch)
{
    uint16_t usable = 0U;

    if ((image == NULL) || (codes == NULL)) {
        return;
    }
    image->iir_usable_mask = 0U;
    for (uint8_t cell = 0U; cell < AMS_CELL_IMAGE_MONITORED_COUNT; ++cell) {
        uint16_t bit = (uint16_t)(1U << cell);
        uint16_t mv = 0U;
        if (((fresh_mask & bit) != 0U) && ((bad_mask & bit) == 0U) &&
            code_to_plausible_mv(codes[cell], &mv)) {
            image->iir_mv[cell] = mv;
            usable |= bit;
        }
    }
    /* The latest filtered numeric product may be retained for diagnostics,
     * but it is not oracle-usable until FC readiness has been qualified. */
    image->iir_usable_mask = 0U;

    if (!complete_epoch || ((usable & AMS_CELL_IMAGE_MONITORED_MASK) != AMS_CELL_IMAGE_MONITORED_MASK)) {
        ams_cell_image_invalidate_iir(image);
        return;
    }

    if (!image->filtered_last_epoch_valid) {
        image->filtered_successful_epoch_count = 1U;
        image->filtered_last_epoch_valid = true;
        image->filtered_last_epoch_ms = now_ms;
        image->iir_ready = false;
        return;
    }

    if ((uint32_t)(now_ms - image->filtered_last_epoch_ms) < AMS_CELL_IMAGE_IIR_MIN_EPOCH_GAP_MS) {
        /* Accelerated service/test calls do not qualify filter settling. Keep
         * the complete product visible, but restart readiness qualification. */
        image->filtered_successful_epoch_count = 1U;
        image->filtered_last_epoch_ms = now_ms;
        image->iir_ready = false;
        return;
    }

    if (image->filtered_successful_epoch_count < UINT8_MAX) {
        image->filtered_successful_epoch_count++;
    }
    image->filtered_last_epoch_ms = now_ms;
    if (image->filtered_successful_epoch_count >= 2U) {
        image->iir_ready = true;
        image->iir_usable_mask = (uint16_t)(usable & AMS_CELL_IMAGE_MONITORED_MASK);
    }
}
