#include <ams_platform/bms_ok.h>
#include <ams_platform/fail_low.h>

#include <errno.h>
#include <stdio.h>

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>

struct device fake_gpioe_device = { .ready = true };
int fake_gpio_configure_status;
int fake_gpio_set_status;
unsigned int fake_gpio_configure_calls;
unsigned int fake_gpio_set_calls;
int fake_gpio_last_config_flags;
int fake_gpio_last_set_value;
static unsigned int direct_fail_low_calls;
static unsigned int checks;
static unsigned int failures;

#define CHECK(cond) do { \
    checks++; \
    if (!(cond)) { \
        failures++; \
        fprintf(stderr, "FAIL:%d: %s\n", __LINE__, #cond); \
    } \
} while (0)

void ams_bms_ok_force_low_direct(void)
{
    direct_fail_low_calls++;
}

static void reset_fake(void)
{
    fake_gpioe_device.ready = true;
    fake_gpio_configure_status = 0;
    fake_gpio_set_status = 0;
    fake_gpio_configure_calls = 0U;
    fake_gpio_set_calls = 0U;
    fake_gpio_last_config_flags = -1;
    fake_gpio_last_set_value = -1;
    direct_fail_low_calls = 0U;
}

int main(void)
{
    reset_fake();
    CHECK(ams_bms_ok_platform_init_low() == 0);
    CHECK(direct_fail_low_calls == 1U);
    CHECK(fake_gpio_configure_calls == 1U);
    CHECK(fake_gpio_last_config_flags == GPIO_OUTPUT_INACTIVE);
    CHECK(fake_gpio_set_calls == 1U);
    CHECK(fake_gpio_last_set_value == 0);

    reset_fake();
    fake_gpioe_device.ready = false;
    CHECK(ams_bms_ok_platform_init_low() == -ENODEV);
    CHECK(direct_fail_low_calls == 2U);
    CHECK(fake_gpio_configure_calls == 0U);
    CHECK(fake_gpio_set_calls == 0U);

    reset_fake();
    fake_gpio_configure_status = -EIO;
    CHECK(ams_bms_ok_platform_init_low() == -EIO);
    CHECK(direct_fail_low_calls == 2U);
    CHECK(fake_gpio_configure_calls == 1U);
    CHECK(fake_gpio_set_calls == 0U);

    reset_fake();
    fake_gpio_set_status = -EFAULT;
    CHECK(ams_bms_ok_platform_init_low() == -EFAULT);
    CHECK(direct_fail_low_calls == 2U);
    CHECK(fake_gpio_configure_calls == 1U);
    CHECK(fake_gpio_set_calls == 1U);
    CHECK(fake_gpio_last_set_value == 0);

    if (failures != 0U) {
        fprintf(stderr, "BMS_OK adapter: FAIL (%u/%u)\n", failures, checks);
        return 1;
    }

    printf("BMS_OK adapter: PASS (%u checks)\n", checks);
    return 0;
}
