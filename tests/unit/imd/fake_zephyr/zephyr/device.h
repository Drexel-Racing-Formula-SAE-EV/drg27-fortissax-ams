#ifndef ZEPHYR_DEVICE_H_
#define ZEPHYR_DEVICE_H_
#include <stdbool.h>
struct device { int id; bool ready; };
extern struct device fake_device_pwm2;
extern struct device fake_device_gpioc;
#define DEVICE_DT_GET(node_id) (&fake_device_pwm2)
static inline bool device_is_ready(const struct device *dev)
{
    return dev != 0 && dev->ready;
}
#endif
