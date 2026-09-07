#ifndef ZEPHYR_DEVICE_H_
#define ZEPHYR_DEVICE_H_
#include <stdbool.h>
struct device { int id; bool ready; };
extern struct device fake_device_pwm3;
extern struct device fake_device_pwm4;
extern struct device fake_device_pwm5;
#define _AMS_CAT(a,b) a##b
#define _AMS_XCAT(a,b) _AMS_CAT(a,b)
#define DEVICE_DT_GET(node_id) (&_AMS_XCAT(fake_device_, node_id))
static inline bool device_is_ready(const struct device *dev) { return dev != 0 && dev->ready; }
#endif
