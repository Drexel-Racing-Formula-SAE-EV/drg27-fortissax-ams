#ifndef TESTS_CURRENT_ADC_FAKE_ZEPHYR_ADC_H_
#define TESTS_CURRENT_ADC_FAKE_ZEPHYR_ADC_H_
#include <stdbool.h>
#include <stdint.h>

#define FAKE_ADC_TRACE_MAX 64

typedef enum {
    FAKE_EVT_SETUP_HIGH = 1,
    FAKE_EVT_ASYNC_HIGH,
    FAKE_EVT_POLL_HIGH,
    FAKE_EVT_SETUP_LOW,
    FAKE_EVT_ASYNC_LOW,
    FAKE_EVT_POLL_LOW,
} fake_adc_event_t;

typedef struct {
    bool ready_high;
    bool ready_low;
    int setup_high_status;
    int setup_low_status;
    int sequence_high_status;
    int sequence_low_status;
    int async_high_status;
    int async_low_status;
    int poll_high_status;
    int poll_low_status;
    int completion_high_status;
    int completion_low_status;
    uint16_t high_count;
    uint16_t low_count;
    uint32_t high_wait_ms;
    uint32_t low_wait_ms;
    bool suppress_completion_signal;
    fake_adc_event_t trace[FAKE_ADC_TRACE_MAX];
    unsigned int trace_count;
} fake_adc_control_t;

extern fake_adc_control_t fake_adc;

void fake_adc_reset(void);
void fake_adc_trace_clear(void);

#endif
