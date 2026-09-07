#ifndef AMS_PLATFORM_FAIL_LOW_H_
#define AMS_PLATFORM_FAIL_LOW_H_

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Unconditional board-level BMS_OK fail-low primitive.
 *
 * This API deliberately has no scheduler, heap, logging, mutex, or ordinary
 * Zephyr GPIO-driver dependency. The board implementation is allowed to use
 * direct MCU registers so pre-kernel/fatal handling can still force the
 * shutdown output to its safe physical level.
 */
void ams_bms_ok_force_low_direct(void);

#ifdef __cplusplus
}
#endif

#endif /* AMS_PLATFORM_FAIL_LOW_H_ */
