#include "ams_safety.h"

#include <errno.h>
#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/fatal.h>
#include <zephyr/init.h>
#include <zephyr/sys/util.h>

#include <soc.h>


#define AMS_USER_NODE DT_PATH(zephyr_user)

#define AMS_BMS_OK_PIN          0U
#define AMS_GPIO_MODE_WIDTH     2U
#define AMS_GPIO_MODE_OUTPUT    1U


/*
 * Migration authority invariant.
 *
 * Even if somebody accidentally changes prj.conf or menuconfig, the
 * Z-003 image must refuse to compile rather than gain authority.
 */
BUILD_ASSERT(!IS_ENABLED(CONFIG_AMS_BMS_AUTHORITY),
             "Z-003 forbids BMS_OK assertion authority");

BUILD_ASSERT(!IS_ENABLED(CONFIG_AMS_BALANCE_AUTHORITY),
             "Z-003 forbids balancing authority");


/*
 * Compile-time board-contract checks.
 *
 * BMS_OK is frozen to PE0 and is active-high.
 */
BUILD_ASSERT(DT_NODE_EXISTS(AMS_USER_NODE),
             "/zephyr,user must exist");

BUILD_ASSERT(DT_NODE_HAS_PROP(AMS_USER_NODE, bms_ok_gpios),
             "/zephyr,user must define bms-ok-gpios");

BUILD_ASSERT(
    DT_SAME_NODE(
        DT_GPIO_CTLR(AMS_USER_NODE, bms_ok_gpios),
        DT_NODELABEL(gpioe)),
    "BMS_OK controller must be GPIOE");

BUILD_ASSERT(
    DT_GPIO_PIN(AMS_USER_NODE, bms_ok_gpios) == AMS_BMS_OK_PIN,
    "BMS_OK must remain on PE0");

BUILD_ASSERT(
    DT_GPIO_FLAGS(AMS_USER_NODE, bms_ok_gpios) == GPIO_ACTIVE_HIGH,
    "BMS_OK must remain active-high");


static const struct gpio_dt_spec bms_ok =
    GPIO_DT_SPEC_GET(AMS_USER_NODE, bms_ok_gpios);


/*
 * Force PE0 to a physical low level without relying on the Zephyr GPIO
 * abstraction.
 *
 * Sequence matters:
 *
 * 1. Enable GPIOE peripheral clock.
 * 2. Preload PE0 output latch LOW while the pin is still not an output.
 * 3. Configure push-pull / no-pull / low-speed attributes.
 * 4. Change PE0 to output mode last.
 * 5. Re-assert the LOW latch.
 *
 * All other GPIOE pins are preserved by read-modify-write operations.
 */
void ams_bms_ok_force_low_direct(void)
{
    const uint32_t pin_bit = BIT(AMS_BMS_OK_PIN);
    const uint32_t mode_shift =
        AMS_BMS_OK_PIN * AMS_GPIO_MODE_WIDTH;
    const uint32_t mode_mask =
        0x3UL << mode_shift;

    /*
     * GPIOE resides on AHB1.
     *
     * The readback provides the required peripheral-clock enable delay
     * before touching GPIOE registers.
     */
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOEEN;
    (void)RCC->AHB1ENR;

    /*
     * STM32 BSRR upper 16 bits atomically reset output bits.
     * Preload PE0 LOW before switching it into output mode.
     */
    GPIOE->BSRR = BIT(AMS_BMS_OK_PIN + 16U);

    /* Push-pull output. */
    GPIOE->OTYPER &= ~pin_bit;

    /* No internal pull-up or pull-down. */
    GPIOE->PUPDR &= ~mode_mask;

    /* Low output speed is sufficient for a static safety line. */
    GPIOE->OSPEEDR &= ~mode_mask;

    /*
     * Configure PE0 as GPIO output.
     * MODER = 01b for output mode.
     */
    GPIOE->MODER =
        (GPIOE->MODER & ~mode_mask) |
        (AMS_GPIO_MODE_OUTPUT << mode_shift);

    /*
     * Re-assert LOW after the mode transition.
     */
    GPIOE->BSRR = BIT(AMS_BMS_OK_PIN + 16U);

    __DSB();
}


/*
 * Earliest application-controlled safety initialization.
 *
 * This deliberately uses the direct register path because ordinary Zephyr
 * GPIO device initialization is not something the safety primitive should
 * depend on.
 */
static int ams_bms_ok_early_init(void)
{
    ams_bms_ok_force_low_direct();
    return 0;
}

SYS_INIT(ams_bms_ok_early_init, PRE_KERNEL_1, 0);


/*
 * Establish the normal Zephyr GPIO ownership once device initialization has
 * completed.
 *
 * The direct path is called first and again on every failure path.
 */
int ams_safety_init(void)
{
    int ret;

    ams_bms_ok_force_low_direct();

    if (!gpio_is_ready_dt(&bms_ok)) {
        ams_bms_ok_force_low_direct();
        return -ENODEV;
    }

    /*
     * BMS_OK is active-high, therefore logical INACTIVE is physical LOW.
     */
    ret = gpio_pin_configure_dt(&bms_ok, GPIO_OUTPUT_INACTIVE);
    if (ret != 0) {
        ams_bms_ok_force_low_direct();
        return ret;
    }

    /*
     * Explicitly write logical inactive as a second fail-low assertion.
     */
    ret = gpio_pin_set_dt(&bms_ok, 0);
    if (ret != 0) {
        ams_bms_ok_force_low_direct();
        return ret;
    }

    return 0;
}


/*
 * Zephyr fatal-policy override.
 *
 * Zephyr invokes this for fatal conditions such as CPU exceptions,
 * stack-check failures, kernel oopses, and kernel panics.
 *
 * Safety action happens BEFORE any attempt to halt the system.
 *
 * Do not add mutex, heap, workqueue, driver, or logging dependencies before
 * the direct fail-low call.
 */
void k_sys_fatal_error_handler(unsigned int reason,
                               const struct arch_esf *esf)
{
    ARG_UNUSED(esf);

    ams_bms_ok_force_low_direct();

    k_fatal_halt(reason);
}