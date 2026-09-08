#include <ams_platform/current_adc.h>

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/clock_control/stm32_clock_control.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/drivers/reset.h>
#include <zephyr/irq.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/util.h>

#include <soc.h>
#include <stm32_ll_adc.h>

#define CURRENT_ADC_NODE DT_NODELABEL(ams_current_sense)
#define CURRENT_ADC_HIGH_NODE DT_PHANDLE(CURRENT_ADC_NODE, high_controller)
#define CURRENT_ADC_LOW_NODE DT_PHANDLE(CURRENT_ADC_NODE, low_controller)
#define CURRENT_ADC3_NODE DT_NODELABEL(adc3)

#define CURRENT_ADC_EXPECTED_INPUT_CLOCK_HZ DT_PROP(CURRENT_ADC_NODE, adc_input_clock_hz)
#define CURRENT_ADC_EXPECTED_PRESCALER DT_PROP(CURRENT_ADC_NODE, adc_prescaler)
#define CURRENT_ADC_EXPECTED_CLOCK_HZ DT_PROP(CURRENT_ADC_NODE, adc_clock_hz)
#define CURRENT_ADC_EXPECTED_RESOLUTION DT_PROP(CURRENT_ADC_NODE, resolution_bits)
#define CURRENT_ADC_EXPECTED_ACQ_TICKS DT_PROP(CURRENT_ADC_NODE, acquisition_ticks)
#define CURRENT_ADC_TIMEOUT_MS DT_PROP(CURRENT_ADC_NODE, conversion_timeout_ms)

BUILD_ASSERT(IS_ENABLED(CONFIG_AMS_CAP_CURRENT_ADC_ADAPTER_PRESENT),
             "current ADC adapter capability must remain present");
BUILD_ASSERT(IS_ENABLED(CONFIG_AMS_CURRENT_ADC_PRIVATE_BACKEND),
             "private current ADC backend capability missing");
BUILD_ASSERT(!IS_ENABLED(CONFIG_ADC),
             "current ADC1/ADC2 are privately owned; generic Zephyr ADC must remain disabled");
BUILD_ASSERT(IS_ENABLED(CONFIG_USE_STM32_LL_ADC),
             "private current ADC backend requires STM32 LL ADC support");
BUILD_ASSERT(IS_ENABLED(CONFIG_RESET),
             "private current ADC recovery requires reset-controller support");
BUILD_ASSERT(IS_ENABLED(CONFIG_ARCH_HAS_IRQ_PENDING_OPS),
             "private current ADC recovery requires NVIC pending-clear support");

BUILD_ASSERT(DT_NODE_HAS_STATUS(CURRENT_ADC_NODE, okay),
             "typed AMS current-sense node must be enabled");
BUILD_ASSERT(DT_SAME_NODE(CURRENT_ADC_HIGH_NODE, DT_NODELABEL(adc1)),
             "current high range must use ADC1");
BUILD_ASSERT(DT_SAME_NODE(CURRENT_ADC_LOW_NODE, DT_NODELABEL(adc2)),
             "current low range must use ADC2");
BUILD_ASSERT(!DT_NODE_HAS_STATUS(CURRENT_ADC_HIGH_NODE, okay) &&
             !DT_NODE_HAS_STATUS(CURRENT_ADC_LOW_NODE, okay) &&
             !DT_NODE_HAS_STATUS(CURRENT_ADC3_NODE, okay),
             "private current ADC backend requires ADC1/ADC2/ADC3 Zephyr devices disabled");
BUILD_ASSERT(DT_NUM_CLOCKS(CURRENT_ADC_HIGH_NODE) >= 1 &&
             DT_NUM_CLOCKS(CURRENT_ADC_LOW_NODE) >= 1,
             "ADC1/ADC2 RCC clock metadata is required");
BUILD_ASSERT(DT_NUM_IRQS(CURRENT_ADC_HIGH_NODE) >= 1 &&
             DT_NUM_IRQS(CURRENT_ADC_LOW_NODE) >= 1,
             "ADC1/ADC2 shared IRQ metadata is required");
BUILD_ASSERT(DT_IRQN(CURRENT_ADC_HIGH_NODE) == DT_IRQN(CURRENT_ADC_LOW_NODE),
             "ADC1/ADC2 must retain the STM32F767 shared ADC IRQ");
BUILD_ASSERT(DT_IRQN(CURRENT_ADC_HIGH_NODE) == 18,
             "STM32F767 ADC1/2 shared IRQ must remain ADC_IRQn 18");
BUILD_ASSERT(DT_PROP(CURRENT_ADC_NODE, high_channel) == AMS_CURRENT_ADC_HIGH_CHANNEL,
             "current high range must remain ADC1_IN3");
BUILD_ASSERT(DT_PROP(CURRENT_ADC_NODE, low_channel) == AMS_CURRENT_ADC_LOW_CHANNEL,
             "current low range must remain ADC2_IN10");
BUILD_ASSERT(CURRENT_ADC_EXPECTED_INPUT_CLOCK_HZ == 108000000U,
             "current ADC input clock must remain 108 MHz APB2");
BUILD_ASSERT(CURRENT_ADC_EXPECTED_PRESCALER == AMS_CURRENT_ADC_PRESCALER,
             "current ADC prescaler must remain /6");
BUILD_ASSERT(CURRENT_ADC_EXPECTED_CLOCK_HZ == 18000000U,
             "current ADC conversion clock must remain 18 MHz");
BUILD_ASSERT(CURRENT_ADC_EXPECTED_INPUT_CLOCK_HZ / CURRENT_ADC_EXPECTED_PRESCALER ==
                 CURRENT_ADC_EXPECTED_CLOCK_HZ,
             "current ADC achieved-clock contract mismatch");
BUILD_ASSERT(CURRENT_ADC_EXPECTED_RESOLUTION == AMS_CURRENT_ADC_RESOLUTION_BITS,
             "current ADC resolution must remain 12-bit");
BUILD_ASSERT(CURRENT_ADC_EXPECTED_ACQ_TICKS == AMS_CURRENT_ADC_ACQUISITION_TICKS,
             "current ADC sampling time must remain 480 cycles");
BUILD_ASSERT(CURRENT_ADC_TIMEOUT_MS == AMS_CURRENT_ADC_TIMEOUT_MS,
             "current ADC conversion timeout must remain 5 ms");

PINCTRL_DT_DEFINE(CURRENT_ADC_NODE);

static const struct reset_dt_spec adc_common_reset = RESET_DT_SPEC_GET(CURRENT_ADC_NODE);
static const struct stm32_pclken adc1_clocks[] = STM32_DT_CLOCKS(CURRENT_ADC_HIGH_NODE);
static const struct stm32_pclken adc2_clocks[] = STM32_DT_CLOCKS(CURRENT_ADC_LOW_NODE);
static ADC_TypeDef *const adc_high = (ADC_TypeDef *)DT_REG_ADDR(CURRENT_ADC_HIGH_NODE);
static ADC_TypeDef *const adc_low = (ADC_TypeDef *)DT_REG_ADDR(CURRENT_ADC_LOW_NODE);

/* ADC1/ADC2/ADC3 share one ADC common register block and one RCC ADCRST bit on
 * STM32F767. ADC3 is contractually disabled so a recovery reset cannot disturb
 * another live owner. */
static ADC_Common_TypeDef *const adc_common = ADC123_COMMON;

typedef enum {
    CURRENT_ADC_STATE_UNINITIALIZED = 0,
    CURRENT_ADC_STATE_READY,
    CURRENT_ADC_STATE_ACTIVE,
    CURRENT_ADC_STATE_RECOVERING,
    CURRENT_ADC_STATE_FAULTED,
} current_adc_state_t;

static atomic_t adapter_state = ATOMIC_INIT(CURRENT_ADC_STATE_UNINITIALIZED);
static atomic_t recovery_count;
static atomic_t recovery_failure_count;
static atomic_t integrity_violation_count;

static void atomic_inc_saturating(atomic_t *value)
{
    atomic_val_t observed = atomic_get(value);

    while ((uint32_t)observed != UINT32_MAX) {
        if (atomic_cas(value, observed,
                       (atomic_val_t)((uint32_t)observed + 1U))) {
            return;
        }
        observed = atomic_get(value);
    }
}

static void disable_and_clear_adc_irq(void)
{
    const unsigned int irq = DT_IRQN(CURRENT_ADC_HIGH_NODE);

    irq_disable(irq);
    k_irq_clear_pending(irq);
}

static int enable_adc_clocks_and_verify(void)
{
    const struct device *clock = DEVICE_DT_GET(STM32_CLOCK_CONTROL_NODE);
    uint32_t adc1_rate = 0U;
    uint32_t adc2_rate = 0U;
    int ret;

    if (!device_is_ready(clock)) {
        return -ENODEV;
    }

    ret = clock_control_on(clock, (clock_control_subsys_t)&adc1_clocks[0]);
    if (ret != 0) {
        return ret;
    }
    ret = clock_control_on(clock, (clock_control_subsys_t)&adc2_clocks[0]);
    if (ret != 0) {
        return ret;
    }

    ret = clock_control_get_rate(clock,
                                 (clock_control_subsys_t)&adc1_clocks[0],
                                 &adc1_rate);
    if (ret != 0) {
        return ret;
    }
    ret = clock_control_get_rate(clock,
                                 (clock_control_subsys_t)&adc2_clocks[0],
                                 &adc2_rate);
    if (ret != 0) {
        return ret;
    }

    if ((adc1_rate != CURRENT_ADC_EXPECTED_INPUT_CLOCK_HZ) ||
        (adc2_rate != CURRENT_ADC_EXPECTED_INPUT_CLOCK_HZ)) {
        return -ERANGE;
    }

    return 0;
}

static void program_one_adc(ADC_TypeDef *adc, uint32_t channel)
{
    LL_ADC_Disable(adc);

    /* Polling only: no ADC ISR, DMA, injected conversion or watchdog path is
     * allowed to own completion state. */
    LL_ADC_DisableIT_EOCS(adc);
    LL_ADC_DisableIT_OVR(adc);
    LL_ADC_DisableIT_JEOS(adc);
    LL_ADC_DisableIT_AWD1(adc);

    LL_ADC_SetResolution(adc, LL_ADC_RESOLUTION_12B);
    LL_ADC_SetDataAlignment(adc, LL_ADC_DATA_ALIGN_RIGHT);
    LL_ADC_SetSequencersScanMode(adc, LL_ADC_SEQ_SCAN_DISABLE);
    LL_ADC_REG_SetTriggerSource(adc, LL_ADC_REG_TRIG_SOFTWARE);
    LL_ADC_REG_SetSequencerLength(adc, LL_ADC_REG_SEQ_SCAN_DISABLE);
    LL_ADC_REG_SetSequencerDiscont(adc, LL_ADC_REG_SEQ_DISCONT_DISABLE);
    LL_ADC_REG_SetContinuousMode(adc, LL_ADC_REG_CONV_SINGLE);
    LL_ADC_REG_SetDMATransfer(adc, LL_ADC_REG_DMA_TRANSFER_NONE);
    LL_ADC_REG_SetFlagEndOfConversion(adc, LL_ADC_REG_FLAG_EOC_UNITARY_CONV);
    LL_ADC_REG_SetSequencerRanks(adc, LL_ADC_REG_RANK_1, channel);
    LL_ADC_SetChannelSamplingTime(adc, channel, LL_ADC_SAMPLINGTIME_480CYCLES);

    LL_ADC_ClearFlag_EOCS(adc);
    LL_ADC_ClearFlag_OVR(adc);
}

static void program_adc_contract(void)
{
    LL_ADC_Disable(adc_high);
    LL_ADC_Disable(adc_low);

    LL_ADC_SetCommonClock(adc_common, LL_ADC_CLOCK_SYNC_PCLK_DIV6);
    LL_ADC_SetMultimode(adc_common, LL_ADC_MULTI_INDEPENDENT);
    LL_ADC_SetMultiDMATransfer(adc_common, LL_ADC_MULTI_REG_DMA_EACH_ADC);

    program_one_adc(adc_high, LL_ADC_CHANNEL_3);
    program_one_adc(adc_low, LL_ADC_CHANNEL_10);
}

static bool one_adc_contract_valid(ADC_TypeDef *adc, uint32_t channel)
{
    return (LL_ADC_IsEnabled(adc) == 0U) &&
           (LL_ADC_IsEnabledIT_EOCS(adc) == 0U) &&
           (LL_ADC_IsEnabledIT_OVR(adc) == 0U) &&
           (LL_ADC_IsEnabledIT_JEOS(adc) == 0U) &&
           (LL_ADC_IsEnabledIT_AWD1(adc) == 0U) &&
           (LL_ADC_GetResolution(adc) == LL_ADC_RESOLUTION_12B) &&
           (LL_ADC_GetDataAlignment(adc) == LL_ADC_DATA_ALIGN_RIGHT) &&
           (LL_ADC_GetSequencersScanMode(adc) == LL_ADC_SEQ_SCAN_DISABLE) &&
           (LL_ADC_REG_GetTriggerSource(adc) == LL_ADC_REG_TRIG_SOFTWARE) &&
           (LL_ADC_REG_GetSequencerLength(adc) == LL_ADC_REG_SEQ_SCAN_DISABLE) &&
           (LL_ADC_REG_GetSequencerDiscont(adc) == LL_ADC_REG_SEQ_DISCONT_DISABLE) &&
           (LL_ADC_REG_GetContinuousMode(adc) == LL_ADC_REG_CONV_SINGLE) &&
           (LL_ADC_REG_GetDMATransfer(adc) == LL_ADC_REG_DMA_TRANSFER_NONE) &&
           (LL_ADC_REG_GetFlagEndOfConversion(adc) ==
                LL_ADC_REG_FLAG_EOC_UNITARY_CONV) &&
           (LL_ADC_REG_GetSequencerRanks(adc, LL_ADC_REG_RANK_1) ==
                __LL_ADC_CHANNEL_TO_DECIMAL_NB(channel)) &&
           (LL_ADC_GetChannelSamplingTime(adc, channel) ==
                LL_ADC_SAMPLINGTIME_480CYCLES);
}

static bool adc_contract_readback_valid(void)
{
    return (LL_ADC_GetCommonClock(adc_common) == LL_ADC_CLOCK_SYNC_PCLK_DIV6) &&
           (LL_ADC_GetMultimode(adc_common) == LL_ADC_MULTI_INDEPENDENT) &&
           (LL_ADC_GetMultiDMATransfer(adc_common) ==
                LL_ADC_MULTI_REG_DMA_EACH_ADC) &&
           one_adc_contract_valid(adc_high, LL_ADC_CHANNEL_3) &&
           one_adc_contract_valid(adc_low, LL_ADC_CHANNEL_10);
}

static int reset_and_reconfigure_adc(void)
{
    int ret;

    atomic_set(&adapter_state, CURRENT_ADC_STATE_RECOVERING);

    disable_and_clear_adc_irq();
    LL_ADC_Disable(adc_high);
    LL_ADC_Disable(adc_low);

    ret = reset_line_toggle_dt(&adc_common_reset);
    if (ret != 0) {
        atomic_inc_saturating(&recovery_failure_count);
        atomic_set(&adapter_state, CURRENT_ADC_STATE_FAULTED);
        return ret;
    }

    /* The common RCC ADC reset owns ADC peripheral state, not the NVIC pending
     * latch. Keep the shared ADC IRQ disabled and clear pending after reset too. */
    disable_and_clear_adc_irq();
    program_adc_contract();

    if (!adc_contract_readback_valid()) {
        atomic_inc_saturating(&recovery_failure_count);
        atomic_set(&adapter_state, CURRENT_ADC_STATE_FAULTED);
        return -EIO;
    }

    atomic_inc_saturating(&recovery_count);
    return 0;
}

static int recover_and_return(int original_error)
{
    int recovery_ret = reset_and_reconfigure_adc();

    if (recovery_ret != 0) {
        return -EIO;
    }

    atomic_set(&adapter_state, CURRENT_ADC_STATE_READY);
    return original_error;
}

static int current_adc_read_one(ADC_TypeDef *adc,
                                uint32_t channel,
                                uint16_t *count,
                                uint32_t *wait_ms)
{
    uint32_t start_ms;
    uint32_t poll_start_ms;
    uint32_t elapsed_ms;

    if ((adc == NULL) || (count == NULL) || (wait_ms == NULL)) {
        return -EINVAL;
    }

    *count = 0U;
    *wait_ms = 0U;

    /* v2.6.27 calls HAL_ADC_ConfigChannel immediately before every conversion.
     * Preserve that ordering even though the channel is fixed per ADC. */
    LL_ADC_REG_SetSequencerRanks(adc, LL_ADC_REG_RANK_1, channel);
    LL_ADC_SetChannelSamplingTime(adc, channel, LL_ADC_SAMPLINGTIME_480CYCLES);

    if (!one_adc_contract_valid(adc, channel)) {
        return recover_and_return(-EIO);
    }

    start_ms = k_uptime_get_32();

    LL_ADC_ClearFlag_EOCS(adc);
    LL_ADC_ClearFlag_OVR(adc);
    LL_ADC_Enable(adc);

    /* HAL_ADC_Start() waits ADC_STAB_DELAY_US=3 after transitioning ADON from
     * disabled to enabled. Preserve that hardware stabilization requirement. */
    k_busy_wait(3U);
    if (LL_ADC_IsEnabled(adc) == 0U) {
        *wait_ms = k_uptime_get_32() - start_ms;
        return recover_and_return(-EIO);
    }

    LL_ADC_REG_StartConversionSWStart(adc);
    poll_start_ms = k_uptime_get_32();

    /* Deliberately bounded busy polling: this mirrors v2.6.27
     * HAL_ADC_PollForConversion() without creating Zephyr
     * adc_context/signal/ISR ownership. HAL starts its timeout clock only
     * after HAL_ADC_Start() returns, times out when elapsed > Timeout (not
     * >=), and rechecks EOC before committing a timeout so a conversion that
     * completes at the boundary is not falsely rejected. Preserve all three
     * details here. Do not add k_yield()/k_sleep() inside a conversion without
     * re-validating timing; the nominal timeout is short and higher-priority
     * IRQs remain able to preempt this normal thread. */
    for (;;) {
        if (LL_ADC_IsActiveFlag_EOCS(adc) != 0U) {
            break;
        }

        if (LL_ADC_IsActiveFlag_OVR(adc) != 0U) {
            LL_ADC_Disable(adc);
            *wait_ms = k_uptime_get_32() - start_ms;
            return recover_and_return(-EIO);
        }

        elapsed_ms = k_uptime_get_32() - poll_start_ms;
        if (elapsed_ms > CURRENT_ADC_TIMEOUT_MS) {
            if (LL_ADC_IsActiveFlag_EOCS(adc) == 0U) {
                LL_ADC_Disable(adc);
                *wait_ms = k_uptime_get_32() - start_ms;
                return recover_and_return(-ETIMEDOUT);
            }
            break;
        }
    }

    /* HAL_ADC_PollForConversion clears both STRT and EOC before
     * HAL_ADC_GetValue().  STM32F7 LL exposes EOC clearing but no dedicated
     * STRT helper, so reproduce the HAL write-zero-to-clear register operation
     * exactly rather than leaving a stale regular-start flag behind. */
    WRITE_REG(adc->SR, ~(LL_ADC_FLAG_STRT | LL_ADC_FLAG_EOCS));
    *count = LL_ADC_REG_ReadConversionData12(adc);
    LL_ADC_Disable(adc);

    *wait_ms = k_uptime_get_32() - start_ms;

    if (LL_ADC_IsEnabled(adc) != 0U) {
        *count = 0U;
        return recover_and_return(-EIO);
    }

    return 0;
}

int ams_current_adc_init(void)
{
    int ret;

    if (atomic_get(&adapter_state) == CURRENT_ADC_STATE_READY) {
        return 0;
    }
    if (atomic_get(&adapter_state) != CURRENT_ADC_STATE_UNINITIALIZED) {
        return -EIO;
    }

    if (!device_is_ready(adc_common_reset.dev)) {
        atomic_set(&adapter_state, CURRENT_ADC_STATE_FAULTED);
        return -ENODEV;
    }

    ret = pinctrl_apply_state(PINCTRL_DT_DEV_CONFIG_GET(CURRENT_ADC_NODE),
                              PINCTRL_STATE_DEFAULT);
    if (ret != 0) {
        atomic_set(&adapter_state, CURRENT_ADC_STATE_FAULTED);
        return ret;
    }

    ret = enable_adc_clocks_and_verify();
    if (ret != 0) {
        atomic_set(&adapter_state, CURRENT_ADC_STATE_FAULTED);
        return ret;
    }

    disable_and_clear_adc_irq();
    ret = reset_line_toggle_dt(&adc_common_reset);
    if (ret != 0) {
        atomic_set(&adapter_state, CURRENT_ADC_STATE_FAULTED);
        return ret;
    }
    disable_and_clear_adc_irq();

    program_adc_contract();
    if (!adc_contract_readback_valid()) {
        atomic_set(&adapter_state, CURRENT_ADC_STATE_FAULTED);
        return -EIO;
    }

    atomic_set(&adapter_state, CURRENT_ADC_STATE_READY);
    return 0;
}

int ams_current_adc_read_pair(ams_current_adc_pair_t *pair)
{
    int ret;

    if (pair == NULL) {
        return -EINVAL;
    }

    *pair = (ams_current_adc_pair_t){
        .high_status = -EAGAIN,
        .low_status = -EAGAIN,
    };

    if (!atomic_cas(&adapter_state,
                    CURRENT_ADC_STATE_READY,
                    CURRENT_ADC_STATE_ACTIVE)) {
        if (atomic_get(&adapter_state) == CURRENT_ADC_STATE_FAULTED) {
            pair->adapter_faulted = true;
            pair->high_status = -EIO;
            pair->low_status = -EIO;
            return -EIO;
        }

        atomic_inc_saturating(&integrity_violation_count);
        pair->high_status = -EBUSY;
        pair->low_status = -EBUSY;
        return -EBUSY;
    }

    /* Frozen acquisition order: HIGH first. Any HIGH failure suppresses LOW. */
    ret = current_adc_read_one(adc_high,
                               LL_ADC_CHANNEL_3,
                               &pair->high_count,
                               &pair->high_wait_ms);
    pair->high_status = ret;
    if (ret != 0) {
        pair->adapter_faulted =
            atomic_get(&adapter_state) == CURRENT_ADC_STATE_FAULTED;
        if (!pair->adapter_faulted &&
            atomic_get(&adapter_state) == CURRENT_ADC_STATE_ACTIVE) {
            atomic_set(&adapter_state, CURRENT_ADC_STATE_READY);
        }
        return ret;
    }
    pair->high_fresh = true;

    ret = current_adc_read_one(adc_low,
                               LL_ADC_CHANNEL_10,
                               &pair->low_count,
                               &pair->low_wait_ms);
    pair->low_status = ret;
    if (ret != 0) {
        pair->adapter_faulted =
            atomic_get(&adapter_state) == CURRENT_ADC_STATE_FAULTED;
        if (!pair->adapter_faulted &&
            atomic_get(&adapter_state) == CURRENT_ADC_STATE_ACTIVE) {
            atomic_set(&adapter_state, CURRENT_ADC_STATE_READY);
        }
        return ret;
    }

    pair->low_fresh = true;
    pair->complete = true;
    pair->adapter_faulted = false;
    atomic_set(&adapter_state, CURRENT_ADC_STATE_READY);
    return 0;
}

bool ams_current_adc_is_faulted(void)
{
    return atomic_get(&adapter_state) == CURRENT_ADC_STATE_FAULTED;
}

#ifdef AMS_CURRENT_ADC_ADAPTER_TEST
void ams_current_adc_test_reset_state(void)
{
    atomic_set(&adapter_state, CURRENT_ADC_STATE_UNINITIALIZED);
    atomic_set(&recovery_count, 0);
    atomic_set(&recovery_failure_count, 0);
    atomic_set(&integrity_violation_count, 0);
}

uint32_t ams_current_adc_test_recovery_count(void)
{
    return (uint32_t)atomic_get(&recovery_count);
}

uint32_t ams_current_adc_test_recovery_failure_count(void)
{
    return (uint32_t)atomic_get(&recovery_failure_count);
}

uint32_t ams_current_adc_test_integrity_violation_count(void)
{
    return (uint32_t)atomic_get(&integrity_violation_count);
}
#endif
