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


#ifdef __cplusplus
}
#endif

#endif /* DRG27_AMS_SAFETY_H_ */