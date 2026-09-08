#include "fake_zephyr_spi.h"
#include <string.h>
struct fake_spi_platform fake_spi;
SPI_TypeDef fake_spi6_regs;
struct device fake_gpioe_dev = { true };
struct device fake_clock_dev = { true };
struct device fake_reset_dev = { true };
struct pinctrl_dev_config fake_pinctrl_cfg;
void fake_spi_reset(void) {
    memset(&fake_spi,0,sizeof(fake_spi));
    memset(&fake_spi6_regs,0,sizeof(fake_spi6_regs));
    fake_gpioe_dev.ready=true; fake_clock_dev.ready=true; fake_reset_dev.ready=true;
    fake_spi.clock_rate=108000000U;
}
void fake_spi_sync_regs_to_platform(void) { fake_spi.regs=fake_spi6_regs; }
