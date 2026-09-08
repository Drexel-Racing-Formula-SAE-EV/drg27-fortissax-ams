#ifndef ZEPHYR_DRIVERS_CLOCK_CONTROL_H_
#define ZEPHYR_DRIVERS_CLOCK_CONTROL_H_
#include <errno.h>
#include <stdint.h>
#include "fake_zephyr_adc.h"
struct stm32_pclken { unsigned bus; unsigned enr; };
typedef void *clock_control_subsys_t;
static inline int fake_clock_is_low(clock_control_subsys_t subsys)
{
    const struct stm32_pclken *p = (const struct stm32_pclken *)subsys;
    return p != 0 && p->enr == 9U;
}
static inline int clock_control_on(const struct device *dev, clock_control_subsys_t subsys)
{
    (void)dev;
    if (fake_clock_is_low(subsys)) {
        fake_adc.clock_on_low_count++;
        fake_adc_trace(FAKE_ADC_EVT_CLOCK_ON_LOW);
        return fake_adc.clock_on_low_fail ? -EIO : 0;
    }
    fake_adc.clock_on_high_count++;
    fake_adc_trace(FAKE_ADC_EVT_CLOCK_ON_HIGH);
    return fake_adc.clock_on_high_fail ? -EIO : 0;
}
static inline int clock_control_get_rate(const struct device *dev, clock_control_subsys_t subsys,
                                         uint32_t *rate)
{
    (void)dev;
    if (fake_clock_is_low(subsys)) {
        if (fake_adc.clock_rate_low_fail) return -EIO;
        *rate = fake_adc.clock_rate_low;
    } else {
        if (fake_adc.clock_rate_high_fail) return -EIO;
        *rate = fake_adc.clock_rate_high;
    }
    return 0;
}
#endif
