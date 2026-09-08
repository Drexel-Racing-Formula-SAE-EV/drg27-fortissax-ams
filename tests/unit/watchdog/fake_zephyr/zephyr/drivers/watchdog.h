#ifndef ZEPHYR_DRIVERS_WATCHDOG_H_
#define ZEPHYR_DRIVERS_WATCHDOG_H_
#include <stdint.h>
#include <zephyr/device.h>
#define WDT_FLAG_RESET_SOC 0x01U
struct wdt_window { uint32_t min; uint32_t max; };
struct wdt_timeout_cfg {
    struct wdt_window window;
    void (*callback)(const struct device *, int);
    uint8_t flags;
};
int wdt_install_timeout(const struct device *dev, const struct wdt_timeout_cfg *cfg);
int wdt_setup(const struct device *dev, uint8_t options);
int wdt_feed(const struct device *dev, int channel_id);
int wdt_disable(const struct device *dev);
#endif
