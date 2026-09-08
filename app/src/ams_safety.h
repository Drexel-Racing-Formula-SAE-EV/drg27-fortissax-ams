#ifndef DRG27_AMS_SAFETY_H_
#define DRG27_AMS_SAFETY_H_

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Configure the normal Zephyr GPIO path while preserving fail-low state.
 *
 * Returns 0 on success or a negative errno-style error.
 */
int ams_safety_init(void);

/* Latched before Zephyr fatal halt after the direct fail-low action. */
bool ams_safety_panic_latched(void);


#ifdef __cplusplus
}
#endif

#endif /* DRG27_AMS_SAFETY_H_ */