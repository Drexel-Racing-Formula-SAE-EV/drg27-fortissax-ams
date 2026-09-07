#ifndef FAKE_ZEPHYR_ADC_H_
#define FAKE_ZEPHYR_ADC_H_
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <zephyr/device.h>
#include <zephyr/kernel.h>

struct adc_dt_spec {
    const struct device *dev;
    uint8_t channel_id;
};

struct adc_sequence {
    void *buffer;
    size_t buffer_size;
};

#define ADC_DT_SPEC_GET_BY_IDX(node, idx) \
    { .dev = ((idx) == 0 ? &fake_adc1_device : &fake_adc2_device), \
      .channel_id = ((idx) == 0 ? 3U : 10U) }

bool adc_is_ready_dt(const struct adc_dt_spec *spec);
int adc_channel_setup_dt(const struct adc_dt_spec *spec);
int adc_sequence_init_dt(const struct adc_dt_spec *spec,
                         struct adc_sequence *sequence);
int adc_read_async_dt(const struct adc_dt_spec *spec,
                      const struct adc_sequence *sequence,
                      struct k_poll_signal *signal);

#endif
