#ifndef AMS_PLATFORM_IMD_CAPTURE_H_
#define AMS_PLATFORM_IMD_CAPTURE_H_

#include <stdbool.h>
#include <stdint.h>

#include <ams_core/ams_imd.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 216 MHz SYSCLK, APB1 /4 and STM32 timer x2 rule => 108 MHz TIM2. */
#define AMS_IMD_EXPECTED_TIMER_CLOCK_HZ 108000000ULL
#define AMS_IMD_PWM_CHANNEL 1U

/* Initialize the physical PA5/TIM2_CH1 PWM capture and PC5 OK_HS input.
 *
 * Return semantics intentionally mirror the source architecture:
 * - device/configuration/clock/GPIO failures are platform-init failures;
 * - capture-enable failure is retained as a fail-closed IMD process fault,
 *   because v2.6.27 HAL_TIM_IC_Start*() failure did not panic the firmware.
 */
int ams_imd_capture_init(ams_imd_t *state);

/* Read the independent OK_HS GPIO and evaluate the latest coherent capture. */
int ams_imd_capture_read_at(ams_imd_t *state, uint32_t now_ms);

bool ams_imd_capture_platform_ready(void);
bool ams_imd_capture_started(void);
int ams_imd_capture_start_error(void);

uint32_t ams_imd_capture_callback_count(void);
uint32_t ams_imd_capture_callback_error_count(void);
bool ams_imd_capture_callback_faulted(void);

#ifdef __cplusplus
}
#endif

#endif /* AMS_PLATFORM_IMD_CAPTURE_H_ */
