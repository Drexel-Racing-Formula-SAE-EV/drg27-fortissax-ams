#ifndef ZEPHYR_DEVICETREE_H_
#define ZEPHYR_DEVICETREE_H_
#include <stdint.h>
#include "fake_zephyr_adc.h"
#define DT_NODELABEL(name) DT_NODELABEL_I(name)
#define DT_NODELABEL_I(name) DT_NODELABEL_##name
#define DT_NODELABEL_ams_current_sense 1
#define DT_NODELABEL_adc1 2
#define DT_NODELABEL_adc2 3
#define DT_NODELABEL_adc3 4
#define DT_PHANDLE(node, prop) DT_PHANDLE_I(prop)
#define DT_PHANDLE_I(prop) DT_PHANDLE_##prop
#define DT_PHANDLE_high_controller DT_NODELABEL_adc1
#define DT_PHANDLE_low_controller DT_NODELABEL_adc2
#define DT_NODE_HAS_STATUS(node, status) ((node) == DT_NODELABEL_ams_current_sense)
#define DT_SAME_NODE(a,b) ((a) == (b))
#define DT_NUM_CLOCKS(node) (((node) == DT_NODELABEL_adc1 || (node) == DT_NODELABEL_adc2) ? 1 : 0)
#define DT_NUM_IRQS(node) (((node) == DT_NODELABEL_adc1 || (node) == DT_NODELABEL_adc2) ? 1 : 0)
#define DT_IRQN(node) (18U)
#define DT_REG_ADDR(node) ((node) == DT_NODELABEL_adc1 ? (uintptr_t)&fake_adc1_regs : \
                           (node) == DT_NODELABEL_adc2 ? (uintptr_t)&fake_adc2_regs : \
                           (uintptr_t)&fake_adc3_regs)
#define DT_PROP(node, prop) DT_PROP_I(prop)
#define DT_PROP_I(prop) DT_PROP_##prop
#define DT_PROP_adc_input_clock_hz 108000000U
#define DT_PROP_adc_prescaler 6U
#define DT_PROP_adc_clock_hz 18000000U
#define DT_PROP_resolution_bits 12U
#define DT_PROP_acquisition_ticks 480U
#define DT_PROP_conversion_timeout_ms 5U
#define DT_PROP_high_channel 3U
#define DT_PROP_low_channel 10U
#endif
