#include "adbms_spi_internal.h"

#include <ams_platform/adbms_spi_lifecycle.h>

#include <errno.h>
#include <stddef.h>
#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/clock_control/stm32_clock_control.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/drivers/reset.h>
#include <zephyr/irq.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/util.h>

#include <soc.h>
#include <stm32_ll_spi.h>

#define AMS_ADBMS_NODE DT_NODELABEL(ams_adbms_interface)
#define AMS_SPI6_NODE DT_NODELABEL(spi6)

#define AMS_SPI_EXPECTED_INPUT_CLOCK_HZ DT_PROP(AMS_ADBMS_NODE, spi_input_clock_hz)
#define AMS_SPI_EXPECTED_FREQUENCY_HZ DT_PROP(AMS_ADBMS_NODE, spi_frequency_hz)
#define AMS_SPI_PRESCALER_DIV DT_PROP(AMS_ADBMS_NODE, spi_prescaler)
#define AMS_SPI_TIMEOUT_MS DT_PROP(AMS_ADBMS_NODE, spi_timeout_ms)
#define AMS_SPI_MAX_TRANSFER_BYTES DT_PROP(AMS_ADBMS_NODE, max_transfer_bytes)
#define AMS_SPI_READ_DUMMY_BYTE DT_PROP(AMS_ADBMS_NODE, read_dummy_byte)

#define AMS_SPI_ACHIEVED_FREQUENCY_HZ \
    (AMS_SPI_EXPECTED_INPUT_CLOCK_HZ / AMS_SPI_PRESCALER_DIV)

BUILD_ASSERT(DT_NODE_HAS_STATUS(AMS_ADBMS_NODE, okay),
             "Z-015 ADBMS interface node must be enabled");
BUILD_ASSERT(!DT_NODE_HAS_STATUS(AMS_SPI6_NODE, okay),
             "Z-015 private SPI6 backend requires stock SPI6 DT device disabled");
BUILD_ASSERT(DT_SAME_NODE(DT_PHANDLE(AMS_ADBMS_NODE, spi_controller), AMS_SPI6_NODE),
             "ADBMS interface must reference SPI6");
BUILD_ASSERT(DT_NUM_CLOCKS(AMS_SPI6_NODE) >= 1,
             "SPI6 must expose an RCC gating clock in SoC Devicetree");
BUILD_ASSERT(DT_NUM_IRQS(AMS_SPI6_NODE) >= 1,
             "SPI6 IRQ metadata is required so the private backend can disable it");
BUILD_ASSERT(AMS_SPI_EXPECTED_INPUT_CLOCK_HZ == 108000000U,
             "Z-015 oracle requires SPI6 input clock 108 MHz");
BUILD_ASSERT(AMS_SPI_PRESCALER_DIV == 256U,
             "Z-015 oracle requires SPI6 /256 prescaler");
BUILD_ASSERT(AMS_SPI_EXPECTED_FREQUENCY_HZ == 421875U,
             "Z-015 oracle requires 421875 Hz SPI6 clock");
BUILD_ASSERT(AMS_SPI_ACHIEVED_FREQUENCY_HZ == AMS_SPI_EXPECTED_FREQUENCY_HZ,
             "SPI6 achieved frequency contract mismatch");
BUILD_ASSERT(AMS_SPI_TIMEOUT_MS == 500U,
             "Z-015 retains v2.6.27 SPI_TIMEOUT=500 ms");
BUILD_ASSERT(AMS_SPI_MAX_TRANSFER_BYTES == 512U,
             "Z-015 retains v2.6.27 BUFSZ=512 bytes");
BUILD_ASSERT(AMS_SPI_READ_DUMMY_BYTE == 0xFFU,
             "Z-015 read dummy byte must remain 0xFF");
BUILD_ASSERT(IS_ENABLED(CONFIG_AMS_ADBMS_SPI_PRIVATE_BACKEND),
             "Z-015 private ADBMS SPI backend capability missing");
BUILD_ASSERT(!IS_ENABLED(CONFIG_SPI),
             "Z-015 SPI6 is privately owned; generic Zephyr SPI must remain disabled");
BUILD_ASSERT(IS_ENABLED(CONFIG_USE_STM32_LL_SPI),
             "private SPI6 backend requires STM32 LL SPI support");
BUILD_ASSERT(IS_ENABLED(CONFIG_RESET),
             "private SPI6 recovery requires Zephyr reset-controller support");

PINCTRL_DT_DEFINE(AMS_ADBMS_NODE);

static const struct gpio_dt_spec cs_a = GPIO_DT_SPEC_GET(AMS_ADBMS_NODE, cs_a_gpios);
static const struct gpio_dt_spec cs_b = GPIO_DT_SPEC_GET(AMS_ADBMS_NODE, cs_b_gpios);
static const struct reset_dt_spec spi6_reset = RESET_DT_SPEC_GET(AMS_ADBMS_NODE);
static const struct stm32_pclken spi6_clocks[] = STM32_DT_CLOCKS(AMS_SPI6_NODE);
static SPI_TypeDef *const spi6 = (SPI_TypeDef *)DT_REG_ADDR(AMS_SPI6_NODE);

static atomic_t platform_state = ATOMIC_INIT(AMS_ADBMS_SPI_PLATFORM_UNINITIALIZED);
static atomic_t platform_last_error;
static atomic_t platform_last_result = ATOMIC_INIT(AMS_ADBMS_SPI_RESULT_OK);
static atomic_t platform_input_clock_hz;
static atomic_t platform_init_attempt_count;
static atomic_t platform_transfer_success_count;
static atomic_t platform_transfer_timeout_count;
static atomic_t platform_transfer_io_error_count;
static atomic_t platform_integrity_violation_count;
static atomic_t platform_recovery_success_count;
static atomic_t platform_recovery_failure_count;
static atomic_t platform_cs_idle_guaranteed;
static atomic_t platform_irq_path_disabled;

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

static const struct gpio_dt_spec *cs_spec(ams_adbms_spi_string_t string)
{
    if (string == AMS_ADBMS_SPI_STRING_A) {
        return &cs_a;
    }
    if (string == AMS_ADBMS_SPI_STRING_B) {
        return &cs_b;
    }
    return NULL;
}

static int set_one_cs(ams_adbms_spi_string_t string, bool active)
{
    const struct gpio_dt_spec *spec = cs_spec(string);

    if (spec == NULL) {
        return -EINVAL;
    }

    /* gpio_pin_set_dt() consumes logical active/inactive values, so active=1
     * correctly drives these GPIO_ACTIVE_LOW chip selects physically low. */
    return gpio_pin_set_dt(spec, active ? 1 : 0);
}

static int force_both_cs_inactive(void)
{
    int a = gpio_pin_set_dt(&cs_a, 0);
    int b = gpio_pin_set_dt(&cs_b, 0);

    atomic_set(&platform_cs_idle_guaranteed, (a == 0 && b == 0) ? 1 : 0);
    return (a == 0 && b == 0) ? 0 : -EIO;
}

static void disable_and_clear_spi6_irq(void)
{
    const unsigned int irq = DT_IRQN(AMS_SPI6_NODE);

    irq_disable(irq);
    NVIC_ClearPendingIRQ((IRQn_Type)irq);
    atomic_set(&platform_irq_path_disabled, 1);
}

static int spi6_clock_enable_and_verify(void)
{
    const struct device *clock = DEVICE_DT_GET(STM32_CLOCK_CONTROL_NODE);
    uint32_t rate = 0U;
    int ret;

    if (!device_is_ready(clock)) {
        return -ENODEV;
    }

    ret = clock_control_on(clock, (clock_control_subsys_t)&spi6_clocks[0]);
    if (ret != 0) {
        return ret;
    }

    ret = clock_control_get_rate(clock,
                                 (clock_control_subsys_t)&spi6_clocks[0],
                                 &rate);
    if (ret != 0) {
        return ret;
    }

    atomic_set(&platform_input_clock_hz, (atomic_val_t)rate);
    if (rate != AMS_SPI_EXPECTED_INPUT_CLOCK_HZ) {
        return -ERANGE;
    }

    return 0;
}

static void spi6_program_contract(void)
{
    LL_SPI_Disable(spi6);

    /* Explicitly keep every interrupt source disabled. Z-015 has no SPI6 ISR
     * and no async/DMA transport path. */
    LL_SPI_DisableIT_TXE(spi6);
    LL_SPI_DisableIT_RXNE(spi6);
    LL_SPI_DisableIT_ERR(spi6);

    LL_SPI_SetMode(spi6, LL_SPI_MODE_MASTER);
    LL_SPI_SetTransferDirection(spi6, LL_SPI_FULL_DUPLEX);
    LL_SPI_SetDataWidth(spi6, LL_SPI_DATAWIDTH_8BIT);
    LL_SPI_SetClockPolarity(spi6, LL_SPI_POLARITY_HIGH);
    LL_SPI_SetClockPhase(spi6, LL_SPI_PHASE_2EDGE);
    LL_SPI_SetNSSMode(spi6, LL_SPI_NSS_SOFT);
    LL_SPI_SetBaudRatePrescaler(spi6, LL_SPI_BAUDRATEPRESCALER_DIV256);
    LL_SPI_SetTransferBitOrder(spi6, LL_SPI_MSB_FIRST);
    LL_SPI_SetStandard(spi6, LL_SPI_PROTOCOL_MOTOROLA);
    LL_SPI_DisableCRC(spi6);
    LL_SPI_DisableNSSPulseMgt(spi6);
    LL_SPI_SetRxFIFOThreshold(spi6, LL_SPI_RX_FIFO_TH_QUARTER);
}

static bool spi6_contract_readback_valid(void)
{
    return (LL_SPI_GetMode(spi6) == LL_SPI_MODE_MASTER) &&
           (LL_SPI_GetTransferDirection(spi6) == LL_SPI_FULL_DUPLEX) &&
           (LL_SPI_GetDataWidth(spi6) == LL_SPI_DATAWIDTH_8BIT) &&
           (LL_SPI_GetClockPolarity(spi6) == LL_SPI_POLARITY_HIGH) &&
           (LL_SPI_GetClockPhase(spi6) == LL_SPI_PHASE_2EDGE) &&
           (LL_SPI_GetNSSMode(spi6) == LL_SPI_NSS_SOFT) &&
           (LL_SPI_GetBaudRatePrescaler(spi6) == LL_SPI_BAUDRATEPRESCALER_DIV256) &&
           (LL_SPI_GetTransferBitOrder(spi6) == LL_SPI_MSB_FIRST) &&
           (LL_SPI_GetStandard(spi6) == LL_SPI_PROTOCOL_MOTOROLA) &&
           (LL_SPI_IsEnabledCRC(spi6) == 0U) &&
           (LL_SPI_IsEnabledNSSPulse(spi6) == 0U) &&
           (LL_SPI_GetRxFIFOThreshold(spi6) == LL_SPI_RX_FIFO_TH_QUARTER) &&
           (LL_SPI_IsEnabledIT_TXE(spi6) == 0U) &&
           (LL_SPI_IsEnabledIT_RXNE(spi6) == 0U) &&
           (LL_SPI_IsEnabledIT_ERR(spi6) == 0U) &&
           (LL_SPI_IsEnabled(spi6) == 0U);
}

static int spi6_reset_and_reconfigure(void)
{
    int ret;

    atomic_set(&platform_state, AMS_ADBMS_SPI_PLATFORM_RECOVERING);

    /* Selection safety comes before transport recovery. */
    ret = force_both_cs_inactive();
    disable_and_clear_spi6_irq();
    LL_SPI_Disable(spi6);

    if (ret != 0) {
        atomic_inc_saturating(&platform_recovery_failure_count);
        atomic_set(&platform_state, AMS_ADBMS_SPI_PLATFORM_FAULTED);
        return ret;
    }

    ret = reset_line_toggle_dt(&spi6_reset);
    if (ret != 0) {
        atomic_inc_saturating(&platform_recovery_failure_count);
        atomic_set(&platform_state, AMS_ADBMS_SPI_PLATFORM_FAULTED);
        return ret;
    }

    /* RCC reset does not own the NVIC pending latch. Clear it explicitly after
     * reset as well and never enable the SPI6 NVIC line in this backend. */
    disable_and_clear_spi6_irq();
    spi6_program_contract();

    if (!spi6_contract_readback_valid()) {
        atomic_inc_saturating(&platform_recovery_failure_count);
        atomic_set(&platform_state, AMS_ADBMS_SPI_PLATFORM_FAULTED);
        return -EIO;
    }

    atomic_inc_saturating(&platform_recovery_success_count);
    return 0;
}

static uint32_t backend_now_ms(void *context)
{
    ARG_UNUSED(context);
    return k_uptime_get_32();
}

static int backend_set_cs(void *context,
                          ams_adbms_spi_string_t string,
                          bool active)
{
    ARG_UNUSED(context);
    int ret = set_one_cs(string, active);

    if (active) {
        atomic_set(&platform_cs_idle_guaranteed, 0);
    } else if (ret == 0) {
        /* The other string is never selected by the same transaction. Still
         * verify both physical outputs are returned to logical inactive. */
        ret = force_both_cs_inactive();
    }
    return ret;
}

static int backend_start(void *context)
{
    ARG_UNUSED(context);

    if (!spi6_contract_readback_valid()) {
        return -EIO;
    }

    /* A four-entry receive FIFO is the only stale-data reservoir on this F7
     * SPI implementation. Drain at most four entries; a fifth RXNE indication
     * is treated as a hardware fault rather than becoming an unbounded loop. */
    for (unsigned int i = 0U; i < 4U && LL_SPI_IsActiveFlag_RXNE(spi6); ++i) {
        (void)LL_SPI_ReceiveData8(spi6);
    }
    if (LL_SPI_IsActiveFlag_RXNE(spi6)) {
        return -EIO;
    }

    LL_SPI_Enable(spi6);
    return LL_SPI_IsEnabled(spi6) ? 0 : -EIO;
}

static bool backend_tx_ready(void *context)
{
    ARG_UNUSED(context);
    return LL_SPI_IsActiveFlag_TXE(spi6) != 0U;
}

static bool backend_rx_ready(void *context)
{
    ARG_UNUSED(context);
    return LL_SPI_IsActiveFlag_RXNE(spi6) != 0U;
}

static bool backend_busy(void *context)
{
    ARG_UNUSED(context);
    return LL_SPI_IsActiveFlag_BSY(spi6) != 0U;
}

static bool backend_fault(void *context)
{
    ARG_UNUSED(context);
    return (LL_SPI_IsActiveFlag_OVR(spi6) != 0U) ||
           (LL_SPI_IsActiveFlag_MODF(spi6) != 0U) ||
           (LL_SPI_IsActiveFlag_CRCERR(spi6) != 0U) ||
           (LL_SPI_IsActiveFlag_FRE(spi6) != 0U);
}

static int backend_write_byte(void *context, uint8_t value)
{
    ARG_UNUSED(context);
    LL_SPI_TransmitData8(spi6, value);
    return 0;
}

static int backend_read_byte(void *context, uint8_t *value)
{
    ARG_UNUSED(context);
    if (value == NULL) {
        return -EINVAL;
    }
    *value = LL_SPI_ReceiveData8(spi6);
    return 0;
}

static int backend_stop(void *context)
{
    ARG_UNUSED(context);
    LL_SPI_Disable(spi6);
    return LL_SPI_IsEnabled(spi6) ? -EIO : 0;
}

static int backend_recover(void *context)
{
    ARG_UNUSED(context);
    return spi6_reset_and_reconfigure();
}

static const struct ams_adbms_spi_backend backend = {
    .context = NULL,
    .now_ms = backend_now_ms,
    .set_cs_active = backend_set_cs,
    .start = backend_start,
    .tx_ready = backend_tx_ready,
    .rx_ready = backend_rx_ready,
    .busy = backend_busy,
    .fault = backend_fault,
    .write_byte = backend_write_byte,
    .read_byte = backend_read_byte,
    .stop = backend_stop,
    .recover = backend_recover,
};

static const struct ams_adbms_spi_engine_config engine_config = {
    .timeout_ms = AMS_SPI_TIMEOUT_MS,
    .max_transfer_bytes = AMS_SPI_MAX_TRANSFER_BYTES,
    .read_dummy_byte = AMS_SPI_READ_DUMMY_BYTE,
};

int ams_adbms_spi_platform_init(void)
{
    int ret;

    atomic_inc_saturating(&platform_init_attempt_count);

    if (atomic_get(&platform_state) != AMS_ADBMS_SPI_PLATFORM_UNINITIALIZED) {
        return -EALREADY;
    }

    atomic_set(&platform_cs_idle_guaranteed, 0);
    atomic_set(&platform_irq_path_disabled, 0);

    if (!gpio_is_ready_dt(&cs_a) || !gpio_is_ready_dt(&cs_b) ||
        !device_is_ready(spi6_reset.dev)) {
        ret = -ENODEV;
        goto fail;
    }

    /* GPIO_OUTPUT_INACTIVE honors GPIO_ACTIVE_LOW and therefore drives PE2
     * and PE4 physically high without generating a wake/select pulse. */
    ret = gpio_pin_configure_dt(&cs_a, GPIO_OUTPUT_INACTIVE);
    if (ret != 0) {
        goto fail;
    }
    ret = gpio_pin_configure_dt(&cs_b, GPIO_OUTPUT_INACTIVE);
    if (ret != 0) {
        goto fail;
    }
    ret = force_both_cs_inactive();
    if (ret != 0) {
        goto fail;
    }

    ret = pinctrl_apply_state(PINCTRL_DT_DEV_CONFIG_GET(AMS_ADBMS_NODE),
                              PINCTRL_STATE_DEFAULT);
    if (ret != 0) {
        goto fail;
    }

    ret = spi6_clock_enable_and_verify();
    if (ret != 0) {
        goto fail;
    }

    disable_and_clear_spi6_irq();

    ret = reset_line_toggle_dt(&spi6_reset);
    if (ret != 0) {
        goto fail;
    }
    disable_and_clear_spi6_irq();

    spi6_program_contract();
    if (!spi6_contract_readback_valid()) {
        ret = -EIO;
        goto fail;
    }

    /* Z-015 closeout invariant: initialization ends with both strings
     * deselected and SPI disabled; it creates no clocks on the wire. */
    ret = force_both_cs_inactive();
    if (ret != 0) {
        goto fail;
    }

    atomic_set(&platform_last_error, 0);
    atomic_set(&platform_last_result, AMS_ADBMS_SPI_RESULT_OK);
    atomic_set(&platform_state, AMS_ADBMS_SPI_PLATFORM_READY);
    return 0;

fail:
    disable_and_clear_spi6_irq();
    LL_SPI_Disable(spi6);
    (void)force_both_cs_inactive();
    atomic_set(&platform_last_error, ret);
    atomic_set(&platform_state, AMS_ADBMS_SPI_PLATFORM_FAULTED);
    return ret;
}

#ifdef CONFIG_AMS_Z016_LINK_PROBE
#include "adbms_time_internal.h"
static k_tid_t link_owner;
bool ams_adbms_spi_bind_owner(void)
{
 /* Only the dedicated ADBMS thread calls this private entry once, before
  * its first operation. There are no other bind callers or external requests. */
 if (k_is_in_isr() || link_owner != NULL) return false;
 link_owner=k_current_get();
 return true;
}
static bool link_owner_valid(void)
{
 return !k_is_in_isr() && link_owner != NULL && link_owner==k_current_get();
}
bool ams_adbms_spi_wake_b(bool cold)
{
 if (!link_owner_valid()) return false;
 if (!atomic_cas(&platform_state,AMS_ADBMS_SPI_PLATFORM_READY,
                 AMS_ADBMS_SPI_PLATFORM_ACTIVE)) return false;
 bool ok=force_both_cs_inactive()==0;
 LL_SPI_Disable(spi6);
 for (unsigned train=0; ok && train<(cold?2U:1U); ++train) {
  ok=backend_set_cs(NULL,AMS_ADBMS_SPI_STRING_B,true)==0;
  if (ok) ok=ams_adbms_time_delay(1000U);
  /* Cleanup is unconditional, including an interrupted/failed low phase. */
  if (force_both_cs_inactive()!=0) ok=false;
  if (ok) ok=ams_adbms_time_delay(1000U);
 }
 if (force_both_cs_inactive()!=0) ok=false;
 atomic_set(&platform_last_error,ok?0:-EIO);
 atomic_set(&platform_state,ok?AMS_ADBMS_SPI_PLATFORM_READY:AMS_ADBMS_SPI_PLATFORM_FAULTED);
 return ok;
}
#endif

static ams_adbms_spi_result_t run_transfer(bool read,
                                            ams_adbms_spi_string_t string,
                                            const uint8_t *tx,
                                            size_t tx_len,
                                            uint8_t *rx,
                                            size_t rx_len)
{
    ams_adbms_spi_result_t result;
#ifdef CONFIG_AMS_Z016_LINK_PROBE
    if (!link_owner_valid() || string != AMS_ADBMS_SPI_STRING_B) {
        atomic_inc_saturating(&platform_integrity_violation_count);
        return AMS_ADBMS_SPI_RESULT_INTERNAL_FAULT;
    }
#endif

    if (!atomic_cas(&platform_state,
                    AMS_ADBMS_SPI_PLATFORM_READY,
                    AMS_ADBMS_SPI_PLATFORM_ACTIVE)) {
        /* Any transfer attempt outside READY violates the single-owner/lifecycle
         * contract. Preserve durable field evidence even though last_result may
         * later be overwritten by the transaction that currently owns SPI6. */
        atomic_inc_saturating(&platform_integrity_violation_count);
        atomic_set(&platform_last_error, -EFAULT);
        atomic_set(&platform_last_result, AMS_ADBMS_SPI_RESULT_INTERNAL_FAULT);
        return AMS_ADBMS_SPI_RESULT_INTERNAL_FAULT;
    }

    if (read) {
        result = ams_adbms_spi_engine_write_read(&backend, &engine_config,
                                                 string, tx, tx_len, rx, rx_len);
    } else {
        result = ams_adbms_spi_engine_write(&backend, &engine_config,
                                            string, tx, tx_len);
    }

    atomic_set(&platform_last_result, (atomic_val_t)result);

    switch (result) {
    case AMS_ADBMS_SPI_RESULT_OK:
        atomic_inc_saturating(&platform_transfer_success_count);
        atomic_set(&platform_last_error, 0);
        atomic_set(&platform_state, AMS_ADBMS_SPI_PLATFORM_READY);
        break;
    case AMS_ADBMS_SPI_RESULT_TIMEOUT:
        atomic_inc_saturating(&platform_transfer_timeout_count);
        atomic_set(&platform_last_error, -ETIMEDOUT);
        atomic_set(&platform_state, AMS_ADBMS_SPI_PLATFORM_READY);
        break;
    case AMS_ADBMS_SPI_RESULT_RECOVERY_FAILED:
        atomic_inc_saturating(&platform_transfer_io_error_count);
        atomic_set(&platform_last_error, -EIO);
        atomic_set(&platform_state, AMS_ADBMS_SPI_PLATFORM_FAULTED);
        break;
    case AMS_ADBMS_SPI_RESULT_IO_ERROR:
        atomic_inc_saturating(&platform_transfer_io_error_count);
        atomic_set(&platform_last_error, -EIO);
        atomic_set(&platform_state, AMS_ADBMS_SPI_PLATFORM_READY);
        break;
    case AMS_ADBMS_SPI_RESULT_INVALID_ARGUMENT:
        atomic_set(&platform_last_error, -EINVAL);
        atomic_set(&platform_state, AMS_ADBMS_SPI_PLATFORM_READY);
        break;
    case AMS_ADBMS_SPI_RESULT_INTERNAL_FAULT:
    default:
        atomic_inc_saturating(&platform_transfer_io_error_count);
        atomic_set(&platform_last_error, -EFAULT);
        atomic_set(&platform_state, AMS_ADBMS_SPI_PLATFORM_FAULTED);
        break;
    }

    return result;
}

ams_adbms_spi_result_t ams_adbms_spi_write(ams_adbms_spi_string_t string,
                                            const uint8_t *tx,
                                            size_t tx_len)
{
    return run_transfer(false, string, tx, tx_len, NULL, 0U);
}

ams_adbms_spi_result_t ams_adbms_spi_write_read(ams_adbms_spi_string_t string,
                                                 const uint8_t *tx,
                                                 size_t tx_len,
                                                 uint8_t *rx,
                                                 size_t rx_len)
{
    return run_transfer(true, string, tx, tx_len, rx, rx_len);
}

ams_adbms_spi_platform_status_t ams_adbms_spi_platform_status(void)
{
    ams_adbms_spi_platform_status_t status = {
        .state = (ams_adbms_spi_platform_state_t)atomic_get(&platform_state),
        .input_clock_hz = (uint32_t)atomic_get(&platform_input_clock_hz),
        .achieved_clock_hz = AMS_SPI_ACHIEVED_FREQUENCY_HZ,
        .timeout_ms = AMS_SPI_TIMEOUT_MS,
        .max_transfer_bytes = AMS_SPI_MAX_TRANSFER_BYTES,
        .init_attempt_count = (uint32_t)atomic_get(&platform_init_attempt_count),
        .transfer_success_count = (uint32_t)atomic_get(&platform_transfer_success_count),
        .transfer_timeout_count = (uint32_t)atomic_get(&platform_transfer_timeout_count),
        .transfer_io_error_count = (uint32_t)atomic_get(&platform_transfer_io_error_count),
        .integrity_violation_count = (uint32_t)atomic_get(&platform_integrity_violation_count),
        .recovery_success_count = (uint32_t)atomic_get(&platform_recovery_success_count),
        .recovery_failure_count = (uint32_t)atomic_get(&platform_recovery_failure_count),
        .last_error = (int)atomic_get(&platform_last_error),
        .last_transport_result = (uint32_t)atomic_get(&platform_last_result),
        .cs_idle_guaranteed = atomic_get(&platform_cs_idle_guaranteed) != 0,
        .irq_path_disabled = atomic_get(&platform_irq_path_disabled) != 0,
    };

    return status;
}
