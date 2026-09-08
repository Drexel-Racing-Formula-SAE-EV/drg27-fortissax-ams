#ifndef FAKE_ZEPHYR_ADC_H_
#define FAKE_ZEPHYR_ADC_H_

#include <stdbool.h>
#include <stdint.h>

struct device { bool ready; };
struct pinctrl_dev_config { int dummy; };

typedef struct {
    uint32_t SR;
    uint32_t enabled;
    uint32_t it_eocs;
    uint32_t it_ovr;
    uint32_t it_jeos;
    uint32_t it_awd1;
    uint32_t resolution;
    uint32_t alignment;
    uint32_t scan_mode;
    uint32_t trigger;
    uint32_t seq_len;
    uint32_t discont;
    uint32_t continuous;
    uint32_t dma;
    uint32_t eoc_mode;
    uint32_t rank1;
    uint32_t sample_time_ch3;
    uint32_t sample_time_ch10;
    uint32_t flag_eoc;
    uint32_t flag_ovr;
    uint16_t data;
} ADC_TypeDef;

typedef struct {
    uint32_t clock;
    uint32_t multimode;
    uint32_t multi_dma;
} ADC_Common_TypeDef;

typedef enum {
    FAKE_ADC_EVT_IRQ_DISABLE = 1,
    FAKE_ADC_EVT_IRQ_CLEAR,
    FAKE_ADC_EVT_PINCTRL,
    FAKE_ADC_EVT_CLOCK_ON_HIGH,
    FAKE_ADC_EVT_CLOCK_ON_LOW,
    FAKE_ADC_EVT_RESET,
    FAKE_ADC_EVT_START_HIGH,
    FAKE_ADC_EVT_START_LOW,
    FAKE_ADC_EVT_RECOVERY_RESET,
} fake_adc_event_t;

#define FAKE_ADC_TRACE_MAX 128U

struct fake_adc_platform {
    ADC_TypeDef adc1;
    ADC_TypeDef adc2;
    ADC_TypeDef adc3;
    ADC_Common_TypeDef common;
    struct device clock_dev;
    struct device reset_dev;
    struct pinctrl_dev_config pinctrl;

    uint32_t now_ms;
    uint32_t clock_rate_high;
    uint32_t clock_rate_low;
    uint16_t high_count;
    uint16_t low_count;
    uint32_t high_complete_after_ms;
    uint32_t low_complete_after_ms;
    uint32_t high_start_ms;
    uint32_t low_start_ms;

    bool pinctrl_fail;
    bool reset_fail;
    bool clock_on_high_fail;
    bool clock_on_low_fail;
    bool clock_rate_high_fail;
    bool clock_rate_low_fail;
    bool high_stuck;
    bool low_stuck;
    bool high_ovr;
    bool low_ovr;
    bool high_enable_fail;
    bool low_enable_fail;
    bool corrupt_after_reset;
    bool irq_enabled;
    bool irq_pending;
    bool reentry_hook_fired;
    void (*reentry_hook)(void);

    unsigned pinctrl_count;
    unsigned clock_on_high_count;
    unsigned clock_on_low_count;
    unsigned irq_disable_count;
    unsigned irq_clear_count;
    unsigned reset_count;
    unsigned start_high_count;
    unsigned start_low_count;
    fake_adc_event_t trace[FAKE_ADC_TRACE_MAX];
    unsigned trace_count;
};

extern struct fake_adc_platform fake_adc;
extern struct device fake_adc_clock_dev;
extern struct device fake_adc_reset_dev;
extern struct pinctrl_dev_config fake_adc_pinctrl;
extern ADC_TypeDef fake_adc1_regs;
extern ADC_TypeDef fake_adc2_regs;
extern ADC_TypeDef fake_adc3_regs;
extern ADC_Common_TypeDef fake_adc_common_regs;

void fake_adc_reset(void);
void fake_adc_trace(fake_adc_event_t event);
void fake_adc_copy_registers_to_platform(void);

#endif
