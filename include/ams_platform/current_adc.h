#ifndef AMS_PLATFORM_CURRENT_ADC_H_
#define AMS_PLATFORM_CURRENT_ADC_H_

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
 * The hardened platform path privately owns ADC1/ADC2 and performs bounded
 * polling with no Zephyr adc_context, completion signal, DMA, or ADC ISR.
 * A timeout returns a bad sample, resets/reconfigures the shared STM32F767 ADC
 * block, and permits the next scan to retry, restoring the recoverability of
 * the v2.6.27 HAL oracle. Only a failed reset/reconfiguration latches the
 * adapter faulted until reboot.
 */
int ams_current_adc_read_pair(ams_current_adc_pair_t *pair);

bool ams_current_adc_is_faulted(void);

#ifdef __cplusplus
}
#endif

#endif /* AMS_PLATFORM_CURRENT_ADC_H_ */
