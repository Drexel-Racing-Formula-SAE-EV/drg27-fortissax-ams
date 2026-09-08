#ifndef FAKE_ZEPHYR_SPI_H_
#define FAKE_ZEPHYR_SPI_H_
#include <stdbool.h>
#include <stdint.h>

struct device { bool ready; };
struct pinctrl_dev_config { int dummy; };

typedef struct {
    uint32_t enabled;
    uint32_t mode;
    uint32_t direction;
    uint32_t data_width;
    uint32_t polarity;
    uint32_t phase;
    uint32_t nss_mode;
    uint32_t prescaler;
    uint32_t bit_order;
    uint32_t standard;
    uint32_t crc_enabled;
    uint32_t nss_pulse;
    uint32_t rx_threshold;
    uint32_t it_txe;
    uint32_t it_rxne;
    uint32_t it_err;
    uint8_t data;
} SPI_TypeDef;

struct fake_spi_platform {
    SPI_TypeDef regs;
    struct device gpioe;
    struct device clock;
    struct device reset;
    struct pinctrl_dev_config pinctrl;
    uint32_t now_ms;
    uint32_t clock_rate;
    bool cs_a_active;
    bool cs_b_active;
    bool irq_enabled;
    bool irq_pending;
    bool txe_stuck;
    bool rxne_stuck;
    bool bsy_stuck;
    bool fault_ovr;
    bool pinctrl_fail;
    bool reset_fail;
    bool clock_on_fail;
    bool clock_rate_fail;
    bool cs_config_fail;
    bool cs_assert_fail_once;
    unsigned irq_disable_count;
    unsigned irq_clear_count;
    unsigned reset_count;
    unsigned pinctrl_count;
    unsigned clock_on_count;
    unsigned cs_assert_count;
    unsigned cs_deassert_count;
    unsigned tx_count;
    unsigned rx_count;
    unsigned fifo_pending;
    uint8_t tx_log[1024];
};

extern struct fake_spi_platform fake_spi;
extern SPI_TypeDef fake_spi6_regs;
extern struct device fake_gpioe_dev;
extern struct device fake_clock_dev;
extern struct device fake_reset_dev;
extern struct pinctrl_dev_config fake_pinctrl_cfg;

void fake_spi_reset(void);
void fake_spi_sync_regs_to_platform(void);

#endif
