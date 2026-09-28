#ifndef AMS_TEMP_IMAGE_H_
#define AMS_TEMP_IMAGE_H_
#include <stdbool.h>
#include <stdint.h>
#define AMS_TEMP_COUNT 24U
#define AMS_TEMP_ALL_MASK 0x00FFFFFFUL
#define AMS_TEMP_STALE_MS 12000U
#define AMS_TEMP_MAX_MISSES 10U
#define AMS_TEMP_JUMP_DECI_C 250U
#define AMS_TEMP_RATE_DECI_C_PER_S 50U
typedef struct {
 int16_t raw[AMS_TEMP_COUNT], deci_c[AMS_TEMP_COUNT], filtered_deci_c[AMS_TEMP_COUNT];
 uint32_t last_update_ms[AMS_TEMP_COUNT];
 uint8_t misses[AMS_TEMP_COUNT];
 uint32_t valid_mask, filter_valid_mask, fresh_mask, usable_mask, stale_mask;
 uint32_t invalid_mask, open_mask, short_mask, jump_mask, rate_mask;
 bool startup_scan_complete;
} ams_temp_image_t;
/* Apply once per owner release. Capture times belong to individual samples. */
void ams_temp_image_apply(ams_temp_image_t *image, const int16_t raw[AMS_TEMP_COUNT],
 uint32_t received_mask, const uint32_t captured_ms[AMS_TEMP_COUNT], uint32_t now_ms);
#endif
