#ifndef FAKE_ZEPHYR_DEVICE_H_
#define FAKE_ZEPHYR_DEVICE_H_
struct device { int id; };
extern const struct device fake_adc1_device;
extern const struct device fake_adc2_device;
#endif
