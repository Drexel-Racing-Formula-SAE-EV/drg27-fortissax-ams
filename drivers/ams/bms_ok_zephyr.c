#include <ams_platform/bms_ok.h>

#include <ams_platform/fail_low.h>

#include <errno.h>

#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/util.h>

#define AMS_SAFETY_NODE DT_NODELABEL(ams_safety_io)

BUILD_ASSERT(IS_ENABLED(CONFIG_AMS_CAP_BMS_OK_PLATFORM_ADAPTER_PRESENT),
             "BMS_OK platform adapter capability must remain present at Z-013");

BUILD_ASSERT(DT_NODE_EXISTS(AMS_SAFETY_NODE),
             "typed AMS safety I/O node must exist");
BUILD_ASSERT(DT_NODE_HAS_PROP(AMS_SAFETY_NODE, bms_ok_gpios),
             "AMS safety I/O must define bms-ok-gpios");
BUILD_ASSERT(DT_SAME_NODE(DT_GPIO_CTLR(AMS_SAFETY_NODE, bms_ok_gpios),
                          DT_NODELABEL(gpioe)),
             "BMS_OK controller must remain GPIOE");
BUILD_ASSERT(DT_GPIO_PIN(AMS_SAFETY_NODE, bms_ok_gpios) == 0U,
             "BMS_OK must remain on PE0");
BUILD_ASSERT(DT_GPIO_FLAGS(AMS_SAFETY_NODE, bms_ok_gpios) == GPIO_ACTIVE_HIGH,
             "BMS_OK must remain active-high");
BUILD_ASSERT(!IS_ENABLED(CONFIG_AMS_BMS_AUTHORITY),
             "Z-013 BMS_OK platform adapter must not gain assertion authority");

static const struct gpio_dt_spec bms_ok =
    GPIO_DT_SPEC_GET(AMS_SAFETY_NODE, bms_ok_gpios);

int ams_bms_ok_platform_init_low(void)
{
    int ret;

    /* The emergency path is the first action and is repeated on every driver
     * error.  Normal Zephyr GPIO ownership must never be required to make the
     * line safe. */
    ams_bms_ok_force_low_direct();

    if (!gpio_is_ready_dt(&bms_ok)) {
        ams_bms_ok_force_low_direct();
        return -ENODEV;
    }

    /* BMS_OK is active-high, so logical INACTIVE is physical LOW. */
    ret = gpio_pin_configure_dt(&bms_ok, GPIO_OUTPUT_INACTIVE);
    if (ret != 0) {
        ams_bms_ok_force_low_direct();
        return ret;
    }

    ret = gpio_pin_set_dt(&bms_ok, 0);
    if (ret != 0) {
        ams_bms_ok_force_low_direct();
        return ret;
    }

    return 0;
}
