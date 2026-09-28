#ifndef AMS_BALANCE_SHADOW_H_
#define AMS_BALANCE_SHADOW_H_
#include <ams_core/ams_cell_image.h>
/* Diagnostic calculation only. This type cannot be submitted to transport. */
#define AMS_BALANCE_SHADOW_START_MV 4100U
#define AMS_BALANCE_SHADOW_DELTA_MV 20U
#define AMS_BALANCE_SHADOW_MAX_CELLS 4U
typedef struct {
    bool valid;
    uint16_t candidate_mask;
    uint16_t cohort_min_mv;
    uint32_t evaluated_ms;
} ams_balance_shadow_t;
/* Caller supplies owner readiness; every cell is independently revalidated.
 * Result is advisory, never evidence of charge permission or applied PWM. */
void ams_balance_shadow_evaluate(const ams_cell_image_t *cells,
                                bool owner_ready, uint32_t now_ms,
                                ams_balance_shadow_t *out);
#endif
