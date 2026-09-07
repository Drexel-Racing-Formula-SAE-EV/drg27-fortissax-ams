#include <ams_platform/fail_low.h>

#include <stdint.h>

#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/init.h>
#include <zephyr/sys/util.h>

#include <soc.h>

#define AMS_SAFETY_NODE DT_NODELABEL(ams_safety_io)
#define AMS_BMS_OK_PIN 0U
#define AMS_GPIO_MODE_WIDTH 2U
#define AMS_GPIO_MODE_OUTPUT 1U

/* The emergency primitive is deliberately tied to the frozen DER26 board and
 * STM32F767 register model. Keep the physical contract adjacent to the only
 * approved direct-register hardware escape hatch. */
BUILD_ASSERT(IS_ENABLED(CONFIG_BOARD_DER26_AMS),
             "DER26 fail-low primitive requires the DER26 AMS board");
BUILD_ASSERT(IS_ENABLED(CONFIG_SOC_STM32F767XX),
             "DER26 fail-low primitive requires STM32F767XX");
BUILD_ASSERT(DT_NODE_EXISTS(AMS_SAFETY_NODE),
             "typed AMS safety I/O node must exist");
BUILD_ASSERT(DT_NODE_HAS_PROP(AMS_SAFETY_NODE, bms_ok_gpios),
             "AMS safety I/O must define bms-ok-gpios");
BUILD_ASSERT(DT_SAME_NODE(DT_GPIO_CTLR(AMS_SAFETY_NODE, bms_ok_gpios),
                          DT_NODELABEL(gpioe)),
             "BMS_OK controller must remain GPIOE");
BUILD_ASSERT(DT_GPIO_PIN(AMS_SAFETY_NODE, bms_ok_gpios) == AMS_BMS_OK_PIN,
             "BMS_OK must remain on PE0");
BUILD_ASSERT(DT_GPIO_FLAGS(AMS_SAFETY_NODE, bms_ok_gpios) == GPIO_ACTIVE_HIGH,
             "BMS_OK must remain active-high");

void ams_bms_ok_force_low_direct(void)
{
    const uint32_t pin_bit = BIT(AMS_BMS_OK_PIN);
    const uint32_t mode_shift = AMS_BMS_OK_PIN * AMS_GPIO_MODE_WIDTH;
    const uint32_t mode_mask = 0x3UL << mode_shift;

    /* GPIOE resides on AHB1. Readback provides the peripheral-clock enable
     * delay before the first GPIOE register access. */
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOEEN;
    (void)RCC->AHB1ENR;

    /* Preload PE0 low while the pin is not yet an output. */
    GPIOE->BSRR = BIT(AMS_BMS_OK_PIN + 16U);

    GPIOE->OTYPER &= ~pin_bit;
    GPIOE->PUPDR &= ~mode_mask;
    GPIOE->OSPEEDR &= ~mode_mask;

    /* Output mode last, after the low latch and static electrical attributes. */
    GPIOE->MODER =
        (GPIOE->MODER & ~mode_mask) |
        (AMS_GPIO_MODE_OUTPUT << mode_shift);

    GPIOE->BSRR = BIT(AMS_BMS_OK_PIN + 16U);

    __DSB();
    __ISB();
}


/*
 * Earliest application-controlled board safety action.  Keeping registration
 * beside the emergency primitive prevents app orchestration from owning the
 * physical PE0 initialization mechanism.
 */
static int ams_bms_ok_early_init(void)
{
    ams_bms_ok_force_low_direct();
    return 0;
}

SYS_INIT(ams_bms_ok_early_init, PRE_KERNEL_1, 0);
