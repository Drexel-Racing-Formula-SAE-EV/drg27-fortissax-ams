#ifndef DRG27_AMS_SAFETY_H_
#define DRG27_AMS_SAFETY_H_

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Configure the normal Zephyr GPIO path while preserving fail-low state.
 *
 * Returns 0 on success or a negative errno-style error.
 */
int ams_safety_init(void);

/*
 * Direct STM32 fail-low primitive.
 *
 * This path deliberately does not depend on:
 * - scheduler state
 * - mutexes
 * - workqueues
 * - heap
 * - logging
 * - Zephyr GPIO driver readiness
 *
 * It is safe to use from fatal handling and early initialization.
 */
void ams_bms_ok_force_low_direct(void);

#ifdef __cplusplus
}
#endif

#endif /* DRG27_AMS_SAFETY_H_ */