#ifndef DRIVERS_AMS_CURRENT_ADC_ZEPHYR_H_
#define DRIVERS_AMS_CURRENT_ADC_ZEPHYR_H_

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AMS_CURRENT_ADC_TIMEOUT_MS 5U
#define AMS_CURRENT_ADC_HIGH_CHANNEL 3U
#define AMS_CURRENT_ADC_LOW_CHANNEL 10U
#define AMS_CURRENT_ADC_RESOLUTION_BITS 12U
#define AMS_CURRENT_ADC_ACQUISITION_TICKS 480U
#define AMS_CURRENT_ADC_PRESCALER 6U
#define AMS_CURRENT_ADC_NOMINAL_VREF_MV 3300U

typedef struct {
    uint16_t high_count;
    uint16_t low_count;
    bool high_fresh;
    bool low_fresh;
    bool complete;
    bool adapter_faulted;
    int high_status;
    int low_status;
    uint32_t high_wait_ms;
    uint32_t low_wait_ms;
} ams_current_adc_pair_t;

/*
 * Initializes only the Zephyr ADC adapter bookkeeping/readiness contract.
 * No conversion is started here. BMS and balancing authority are unrelated.
 */
int ams_current_adc_init(void);

/*
 * Acquire exactly one high-range sample followed by one low-range sample.
 * A failed high read suppresses the low read, matching v2.6.27.
 *
 * The Zephyr STM32 ADC synchronous API waits K_FOREVER internally by default.
 * Z-011 therefore uses adc_read_async_dt() plus a 5 ms k_poll timeout. If a
 * conversion times out, Zephyr exposes no public cancellation API for the
 * in-flight STM32 adc_context operation. The adapter latches itself faulted
 * and refuses buffer/device reuse until reboot. This is more fail-closed than
 * the HAL oracle and prevents an overdue ISR from writing into reused storage.
 */
int ams_current_adc_read_pair(ams_current_adc_pair_t *pair);

bool ams_current_adc_is_faulted(void);

#ifdef __cplusplus
}
#endif

#endif /* DRIVERS_AMS_CURRENT_ADC_ZEPHYR_H_ */
