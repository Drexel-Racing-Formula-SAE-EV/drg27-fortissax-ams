#ifndef AMS_PLATFORM_BMS_OK_H_
#define AMS_PLATFORM_BMS_OK_H_

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Establish normal Zephyr GPIO ownership of BMS_OK while keeping the line
 * physically inactive.  This migration-stage interface intentionally exposes
 * no assertion/high operation; BMS_OK authority remains compile-time absent.
 *
 * The unconditional pre-kernel/fatal fail-low primitive is implemented by the
 * board safety layer and remains independent of the Zephyr GPIO driver.
 */
int ams_bms_ok_platform_init_low(void);

#ifdef __cplusplus
}
#endif

#endif /* AMS_PLATFORM_BMS_OK_H_ */
