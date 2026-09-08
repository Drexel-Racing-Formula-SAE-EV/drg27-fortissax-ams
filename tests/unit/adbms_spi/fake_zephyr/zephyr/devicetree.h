#ifndef ZEPHYR_DEVICETREE_H_
#define ZEPHYR_DEVICETREE_H_
#include <stdint.h>
#include "fake_zephyr_spi.h"
#define DT_NODELABEL(name) DT_NODELABEL_I(name)
#define DT_NODELABEL_I(name) DT_NODELABEL_##name
#define DT_NODELABEL_ams_adbms_interface 1
#define DT_NODELABEL_spi6 2
#define DT_NODE_HAS_STATUS(node, status) ((node) == 1)
#define DT_SAME_NODE(a,b) ((a)==(b))
#define DT_PHANDLE(node, prop) 2
#define DT_NUM_CLOCKS(node) 1
#define DT_NUM_IRQS(node) 1
#define DT_IRQN(node) 86
#define DT_REG_ADDR(node) ((uintptr_t)&fake_spi6_regs)
#define DT_PROP(node, prop) DT_PROP_I(prop)
#define DT_PROP_I(prop) DT_PROP_##prop
#define DT_PROP_spi_input_clock_hz 108000000U
#define DT_PROP_spi_frequency_hz 421875U
#define DT_PROP_spi_prescaler 256U
#define DT_PROP_spi_timeout_ms 500U
#define DT_PROP_max_transfer_bytes 512U
#define DT_PROP_read_dummy_byte 255U
#endif
