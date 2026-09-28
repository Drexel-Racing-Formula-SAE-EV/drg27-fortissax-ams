#ifndef AMS_CORE_CELL_IMAGE_H_
#define AMS_CORE_CELL_IMAGE_H_

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AMS_CELL_IMAGE_REGISTER_COUNT 16U
#define AMS_CELL_IMAGE_MONITORED_COUNT 15U
#define AMS_CELL_IMAGE_MONITORED_MASK 0x7FFFU
#define AMS_CELL_IMAGE_STALE_TIMEOUT_MS 2500U
#define AMS_CELL_IMAGE_MAX_CONSEC_MISSES 2U
#define AMS_CELL_IMAGE_VALID_MIN_MV 500U
#define AMS_CELL_IMAGE_VALID_MAX_MV 5000U
#define AMS_CELL_IMAGE_JUMP_MV 250U
#define AMS_CELL_IMAGE_STUCK_SAME_COUNT 120U
#define AMS_CELL_IMAGE_IIR_MIN_EPOCH_GAP_MS 100U

typedef struct {
    uint16_t raw_mv[AMS_CELL_IMAGE_MONITORED_COUNT];
    uint16_t avg8_mv[AMS_CELL_IMAGE_MONITORED_COUNT];
    uint16_t iir_mv[AMS_CELL_IMAGE_MONITORED_COUNT];
    bool raw_valid[AMS_CELL_IMAGE_MONITORED_COUNT];
    uint32_t last_update_ms[AMS_CELL_IMAGE_MONITORED_COUNT];
    uint8_t consecutive_misses[AMS_CELL_IMAGE_MONITORED_COUNT];
    uint8_t same_count[AMS_CELL_IMAGE_MONITORED_COUNT];

    uint16_t updated_mask;
    uint16_t usable_mask;
    uint16_t stale_mask;
    uint16_t jump_mask;
    uint16_t stuck_mask;
    uint16_t avg8_usable_mask;
    uint16_t iir_usable_mask;

    uint16_t max_delta_mv;
    uint8_t max_delta_cell;
    uint16_t min_mv;
    uint16_t max_mv;
    uint32_t sum_mv;
    uint8_t usable_count;

    bool iir_ready;
    uint8_t filtered_successful_epoch_count;
    bool filtered_last_epoch_valid;
    uint32_t filtered_last_epoch_ms;
} ams_cell_image_t;

void ams_cell_image_init(ams_cell_image_t *image);

/* A complete raw candidate provides 16 register codes but only cells 0..14
 * are monitored. fresh_mask records newly parsed register channels; a set bit
 * in bad_mask means the corresponding channel failed integrity/data checks. */
void ams_cell_image_apply_raw(ams_cell_image_t *image,
                              const int16_t codes[AMS_CELL_IMAGE_REGISTER_COUNT],
                              uint16_t fresh_mask,
                              uint16_t bad_mask,
                              uint32_t now_ms);

void ams_cell_image_apply_avg8(ams_cell_image_t *image,
                               const int16_t codes[AMS_CELL_IMAGE_REGISTER_COUNT],
                               uint16_t fresh_mask,
                               uint16_t bad_mask);

void ams_cell_image_apply_iir(ams_cell_image_t *image,
                              const int16_t codes[AMS_CELL_IMAGE_REGISTER_COUNT],
                              uint16_t fresh_mask,
                              uint16_t bad_mask,
                              uint32_t now_ms,
                              bool complete_epoch);

void ams_cell_image_invalidate_iir(ams_cell_image_t *image);

#ifdef __cplusplus
}
#endif

#endif /* AMS_CORE_CELL_IMAGE_H_ */
