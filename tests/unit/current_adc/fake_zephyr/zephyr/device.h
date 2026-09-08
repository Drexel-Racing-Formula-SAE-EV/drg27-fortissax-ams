#ifndef ZEPHYR_DEVICE_H_
#define ZEPHYR_DEVICE_H_
#include "fake_zephyr_adc.h"
static inline bool device_is_ready(const struct device *dev) { return dev != 0 && dev->ready; }
#define DEVICE_DT_GET(node) (&fake_adc_clock_dev)
#endif
