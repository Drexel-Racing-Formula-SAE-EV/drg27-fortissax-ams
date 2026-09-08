#ifndef ZEPHYR_DEVICE_H_
#define ZEPHYR_DEVICE_H_
#include <stdbool.h>
struct device { int dummy; };
extern struct device fake_watchdog_device;
#define DEVICE_DT_GET(node_id) (&fake_watchdog_device)
bool device_is_ready(const struct device *dev);
#endif
