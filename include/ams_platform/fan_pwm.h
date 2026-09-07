#ifndef AMS_PLATFORM_FAN_PWM_H_
#define AMS_PLATFORM_FAN_PWM_H_

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AMS_FAN_ZONE_COUNT 6U
#define AMS_FAN_PWM_PERIOD_CYCLES 3361U
#define AMS_FAN_PWM_MAX_COMPARE_CYCLES 3360U
#define AMS_FAN_PWM_EXPECTED_TIMER_CLOCK_HZ 108000000ULL

/* Initialize the three timer/PWM devices and attempt an explicit 0% command
 * on every fan zone. Timer/platform failures are returned to the caller and
 * are fatal at application startup; per-channel write failures are recorded
 * as soft process faults and retried by the 5 Hz fan worker. */
int ams_fan_pwm_init(void);

int ams_fan_pwm_set_percent(uint8_t zone, float percent);

uint32_t ams_fan_pwm_force_all_off(void);

uint32_t ams_fan_pwm_startup_fail_mask(void);

bool ams_fan_pwm_platform_ready(void);

#ifdef __cplusplus
}
#endif

#endif /* AMS_PLATFORM_FAN_PWM_H_ */
