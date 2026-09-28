#include <ams_platform/adbms_monitor.h>
#ifdef CONFIG_AMS_Z023_SUPERVISION_VALIDATION
#include <ams_platform/supervision.h>
static bool supervision_work;
#endif
#ifdef CONFIG_AMS_Z022_MEASUREMENT_VALIDATION
#include <ams_platform/measurement_pipeline.h>
#endif
#include <ams_platform/adbms_spi_lifecycle.h>

#include <ams_core/ams_adbms_monitor.h>

#include "adbms_spi_internal.h"
#include "adbms_time_internal.h"

#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/spinlock.h>
#include <zephyr/sys/util.h>

BUILD_ASSERT(IS_ENABLED(CONFIG_AMS_Z017_CELL_VALIDATION) || IS_ENABLED(CONFIG_AMS_Z018_TEMP_VALIDATION),
             "Z017 monitor adapter must only build in its explicit profile");
BUILD_ASSERT(!IS_ENABLED(CONFIG_AMS_Z016_LINK_PROBE),
             "Z016 finite probe and Z017 acquisition are mutually exclusive");
BUILD_ASSERT(!IS_ENABLED(CONFIG_AMS_BMS_AUTHORITY) &&
             !IS_ENABLED(CONFIG_AMS_BALANCE_AUTHORITY),
             "Z017 monitor profile must remain no-authority");

static ams_adbms_monitor_t monitor;
static ams_adbms_monitor_platform_snapshot_t published;
static struct k_spinlock published_lock;
#if defined(CONFIG_AMS_Z020_BALANCE_DISABLED_VALIDATION) && CONFIG_AMS_Z020_BALANCE_DISABLED_VALIDATION
static uint32_t shadow_now_ms;
#endif
static bool owner_bound;
static bool init_attempted;
static bool initialized;
static k_tid_t owner_thread;
#if defined(CONFIG_AMS_Z018_TEMP_VALIDATION) && CONFIG_AMS_Z018_TEMP_VALIDATION
static uint32_t aux2_last_ms, ow_last_ms;
static uint8_t aux2_sensor, ow_sensor;
#endif

static ams_adbms_result_t map_spi_result(ams_adbms_spi_result_t result)
{
    ams_adbms_spi_platform_status_t status = ams_adbms_spi_platform_status();

    switch (result) {
    case AMS_ADBMS_SPI_RESULT_OK:
        return AMS_ADBMS_RESULT_OK;
    case AMS_ADBMS_SPI_RESULT_TIMEOUT:
        return AMS_ADBMS_RESULT_TRANSPORT_TIMEOUT;
    case AMS_ADBMS_SPI_RESULT_CLOCK_ERROR:
        return AMS_ADBMS_RESULT_CLOCK;
    case AMS_ADBMS_SPI_RESULT_INVALID_ARGUMENT:
        return AMS_ADBMS_RESULT_INVALID;
    case AMS_ADBMS_SPI_RESULT_RECOVERY_FAILED:
        return AMS_ADBMS_RESULT_TRANSPORT_TERMINAL;
    case AMS_ADBMS_SPI_RESULT_INTERNAL_FAULT:
        return status.state == AMS_ADBMS_SPI_PLATFORM_FAULTED
                   ? AMS_ADBMS_RESULT_TRANSPORT_TERMINAL
                   : AMS_ADBMS_RESULT_OWNER;
    case AMS_ADBMS_SPI_RESULT_IO_ERROR:
    default:
        return status.state == AMS_ADBMS_SPI_PLATFORM_FAULTED
                   ? AMS_ADBMS_RESULT_TRANSPORT_TERMINAL
                   : AMS_ADBMS_RESULT_TRANSPORT_IO;
    }
}

static ams_adbms_result_t z017_now_us(void *context, uint64_t *now_us)
{
    ARG_UNUSED(context);
    return ams_adbms_time_now(now_us) ? AMS_ADBMS_RESULT_OK
                                      : AMS_ADBMS_RESULT_CLOCK;
}

static ams_adbms_result_t z017_delay_us(void *context, uint32_t delay_us)
{
    uint64_t start;
    uint64_t now;
    uint64_t elapsed;
    uint32_t remaining;

    ARG_UNUSED(context);
    if (delay_us == 0U) {
        return AMS_ADBMS_RESULT_OK;
    }
    if (delay_us <= 1000U) {
        return ams_adbms_time_delay(delay_us) ? AMS_ADBMS_RESULT_OK
                                              : AMS_ADBMS_RESULT_CLOCK;
    }
    if (!ams_adbms_time_now(&start)) {
        return AMS_ADBMS_RESULT_CLOCK;
    }

    /* Release the CPU for the bulk of 3/17/20 ms oracle waits, then use the
     * finite cycle-counter helper only for the final <=1 ms. If an external
     * wake interrupts k_sleep() by more than that tail, fail closed instead of
     * silently shortening the protocol delay or looping without a bound. */
    k_sleep(K_USEC(delay_us - 1000U));
    if (!ams_adbms_time_now(&now) || (now < start)) {
        return AMS_ADBMS_RESULT_CLOCK;
    }
    elapsed = now - start;
    if (elapsed >= delay_us) {
        return AMS_ADBMS_RESULT_OK;
    }
    remaining = (uint32_t)(delay_us - elapsed);
    if (remaining > 1000U) {
        return AMS_ADBMS_RESULT_CLOCK;
    }
    return ams_adbms_time_delay(remaining) ? AMS_ADBMS_RESULT_OK
                                            : AMS_ADBMS_RESULT_CLOCK;
}

static ams_adbms_result_t z017_wake_b(void *context, bool cold)
{
    ARG_UNUSED(context);
    return map_spi_result(ams_adbms_spi_wake_b(cold));
}

static ams_adbms_result_t z017_write_b(void *context,
                                       const uint8_t *tx,
                                       uint16_t tx_len)
{
    ARG_UNUSED(context);
    return map_spi_result(ams_adbms_spi_write(AMS_ADBMS_SPI_STRING_B,
                                              tx, (size_t)tx_len));
}

static ams_adbms_result_t z017_write_read_b(void *context,
                                            const uint8_t *tx,
                                            uint16_t tx_len,
                                            uint8_t *rx,
                                            uint16_t rx_len)
{
    ARG_UNUSED(context);
    return map_spi_result(ams_adbms_spi_write_read(AMS_ADBMS_SPI_STRING_B,
                                                   tx, (size_t)tx_len,
                                                   rx, (size_t)rx_len));
}

static const ams_adbms_monitor_io_t monitor_io = {
    .context = NULL,
    .now_us = z017_now_us,
    .delay_us = z017_delay_us,
    .wake_b = z017_wake_b,
    .write_b = z017_write_b,
    .write_read_b = z017_write_read_b,
};

static void map_snapshot(const ams_adbms_monitor_snapshot_t *src,
                         ams_adbms_monitor_platform_snapshot_t *dst)
{
    memset(dst, 0, sizeof(*dst));
    dst->owner_bound = owner_bound;
    dst->init_attempted = init_attempted;
    dst->state = (ams_adbms_monitor_platform_state_t)src->state;
#define COPY(name) dst->name = src->name
    COPY(initialized);
    COPY(config_verified);
    COPY(startup_post_passed);
    COPY(acquisition_live);
    COPY(physical_validated);
    COPY(init_attempt_count);
    COPY(init_fail_count);
    COPY(post_run_count);
    COPY(post_fail_count);
    COPY(post_attempts);
    COPY(post_failed_stage_mask);
    COPY(post_unexpected_stage_mask);
    COPY(balance_inhibit_attempt_count);
    COPY(balance_inhibit_fail_count);
    COPY(balance_mute_verified);
    COPY(balance_durable_zero_verified);
    COPY(scan_attempt_count);
    COPY(scan_success_count);
    COPY(scan_fail_count);
    COPY(coherent_restart_count);
    COPY(coherent_restart_fail_count);
    COPY(session_expiry_count);
    dst->first_error = (int32_t)src->first_error;
    dst->cleanup_error = (int32_t)src->cleanup_error;
    dst->last_result = (int32_t)src->last_result;
    COPY(attempted_ms);
    COPY(successful_ms);
    COPY(epoch_attempts);
    COPY(snapshot_cleanup_required);
    COPY(raw_fresh_mask);
    COPY(raw_bad_mask);
    COPY(avg8_fresh_mask);
    COPY(avg8_bad_mask);
    COPY(iir_fresh_mask);
    COPY(iir_bad_mask);
    COPY(ccts);
    COPY(ccts_valid);
    COPY(statd_valid);
    dst->statd.valid = src->statd.valid;
    dst->statd.cell_uv_mask = src->statd.cell_uv_mask;
    dst->statd.cell_ov_mask = src->statd.cell_ov_mask;
    dst->statd.osc_counter = src->statd.osc_counter;
    memcpy(dst->raw_codes, src->raw_codes, sizeof(dst->raw_codes));
    memcpy(dst->avg8_codes, src->avg8_codes, sizeof(dst->avg8_codes));
    memcpy(dst->iir_codes, src->iir_codes, sizeof(dst->iir_codes));
    COPY(cells);
    COPY(temperature);
    COPY(recovery);
    dst->counter.known = src->counter.known;
    dst->counter.expected = src->counter.expected;
    dst->counter.mismatch_count = src->counter.mismatch_count;
    dst->counter.unexpected_reset_count = src->counter.unexpected_reset_count;
    dst->counter.unknown_count = src->counter.unknown_count;
    COPY(sticky_diag_faults);
    COPY(transport_timeout_count);
    COPY(transport_io_count);
    COPY(transport_terminal_count);
    COPY(pec_fail_count);
    COPY(counter_mismatch_count);
    COPY(config_mismatch_count);
    COPY(ccts_fault_count);
    COPY(diagnostic_fail_count);
#undef COPY
}

static void publish_snapshot(void)
{
    ams_adbms_monitor_snapshot_t core;
    ams_adbms_monitor_platform_snapshot_t next;
    k_spinlock_key_t key;

    ams_adbms_monitor_snapshot(&monitor, &core);
    map_snapshot(&core, &next);
#if defined(CONFIG_AMS_Z020_BALANCE_DISABLED_VALIDATION) && CONFIG_AMS_Z020_BALANCE_DISABLED_VALIDATION
    ams_balance_shadow_evaluate(&core.cells,
        core.initialized && core.config_verified && core.balance_mute_verified &&
        core.balance_durable_zero_verified && core.acquisition_live &&
        core.state == AMS_ADBMS_MONITOR_READY &&
        !core.recovery.pending && !core.recovery.terminal &&
        !core.recovery.continuity_lost && !core.snapshot_cleanup_required &&
        !core.temperature.config_cleanup_required,
        shadow_now_ms, &next.balance_shadow);
#endif
    key = k_spin_lock(&published_lock);
    published = next;
    k_spin_unlock(&published_lock, key);
#ifdef CONFIG_AMS_Z023_SUPERVISION_VALIDATION
    if (supervision_work) {
        supervision_work = false;
        ams_z023_monitor_result(&next);
    }
#endif
}

bool ams_adbms_monitor_platform_init_owner(void)
{
    ams_adbms_result_t result;

    if (k_is_in_isr() || init_attempted) {
        return false;
    }
#ifdef CONFIG_AMS_Z023_SUPERVISION_VALIDATION
    if (!ams_z023_owner(AMS_SUP_ADBMS)) return false;
    supervision_work = true;
#endif
    init_attempted = true;
    ams_adbms_monitor_reset(&monitor);
    if (!ams_adbms_spi_bind_owner()) {
        monitor.state = AMS_ADBMS_MONITOR_FAULTED;
        monitor.first_error = AMS_ADBMS_RESULT_OWNER;
        monitor.last_result = AMS_ADBMS_RESULT_OWNER;
        monitor.init_fail_count = 1U;
        publish_snapshot();
        return false;
    }
    owner_bound = true;
    owner_thread = k_current_get();
    result = ams_adbms_monitor_initialize(&monitor, &monitor_io);
    initialized = result == AMS_ADBMS_RESULT_OK;
    publish_snapshot();
    return initialized;
}

void ams_adbms_monitor_platform_step(uint32_t now_ms)
{
    if (!initialized || k_is_in_isr() || k_current_get() != owner_thread) {
        return;
    }
#ifdef CONFIG_AMS_Z023_SUPERVISION_VALIDATION
    if (!ams_z023_owner(AMS_SUP_ADBMS)) return;
    supervision_work = !monitor.recovery.terminal;
#endif
#if defined(CONFIG_AMS_Z019_RECOVERY_VALIDATION) && CONFIG_AMS_Z019_RECOVERY_VALIDATION
#if defined(CONFIG_AMS_Z020_BALANCE_DISABLED_VALIDATION) && CONFIG_AMS_Z020_BALANCE_DISABLED_VALIDATION
    shadow_now_ms = now_ms;
#endif
    bool was_pending = monitor.recovery.pending;
    if (ams_adbms_monitor_recovery_step(&monitor, &monitor_io, now_ms) != AMS_ADBMS_RESULT_OK || was_pending) {
        publish_snapshot();
        return;
    }
    ams_adbms_result_t cell_result = ams_adbms_monitor_acquire(&monitor, &monitor_io, now_ms);
    if (cell_result != AMS_ADBMS_RESULT_OK || monitor.recovery.continuity_lost) {
        ams_adbms_monitor_interrupt(&monitor, cell_result != AMS_ADBMS_RESULT_OK ? cell_result : AMS_ADBMS_RESULT_COUNTER);
        publish_snapshot();
        return;
    }
#else
    (void)ams_adbms_monitor_acquire(&monitor, &monitor_io, now_ms);
#endif
#ifdef CONFIG_AMS_Z022_MEASUREMENT_VALIDATION
    ams_z022_voltage_boundary();
#endif
#if defined(CONFIG_AMS_Z018_TEMP_VALIDATION) && CONFIG_AMS_Z018_TEMP_VALIDATION
    /* Timestamp at temperature acquisition start, after the cell wait. */
    uint32_t temp_now = k_uptime_get_32();
#ifdef CONFIG_AMS_Z023_SUPERVISION_VALIDATION
    uint8_t position = monitor.temperature.next_position;
#endif
    (void)ams_adbms_monitor_temperature(&monitor, &monitor_io, temp_now);
#ifdef CONFIG_AMS_Z023_SUPERVISION_VALIDATION
    uint32_t position_mask=(1UL<<position)|(1UL<<(position+8U))|(1UL<<(position+16U));
    ams_z023_temperature_result(monitor.recovery.generation,position,
        monitor.temperature.received_mask,
        monitor.temperature.attempted_mask & ~monitor.temperature.received_mask,
        position_mask & ~monitor.temperature.attempted_mask);
#endif
#if defined(CONFIG_AMS_Z019_RECOVERY_VALIDATION) && CONFIG_AMS_Z019_RECOVERY_VALIDATION
    if (monitor.temperature.result != AMS_ADBMS_RESULT_OK) {
        ams_adbms_monitor_interrupt(&monitor, (ams_adbms_result_t)monitor.temperature.result);
        publish_snapshot();
        return;
    }
#endif
    if (IS_ENABLED(CONFIG_AMS_Z018_AUX2_DIAGNOSTIC) &&
        (uint32_t)(temp_now - aux2_last_ms) >= 250U) {
        aux2_last_ms = temp_now;
        (void)ams_adbms_monitor_aux2(&monitor, &monitor_io, aux2_sensor);
        aux2_sensor = (uint8_t)((aux2_sensor + 1U) % AMS_TEMP_COUNT);
    }
    if (IS_ENABLED(CONFIG_AMS_Z018_THERM_OW_DIAGNOSTIC) &&
        (uint32_t)(temp_now - ow_last_ms) >= 2000U) {
        ow_last_ms = temp_now;
        (void)ams_adbms_monitor_therm_ow(&monitor, &monitor_io, ow_sensor);
        ow_sensor = (uint8_t)((ow_sensor + 1U) % AMS_TEMP_COUNT);
    }
#endif
#if defined(CONFIG_AMS_Z019_RECOVERY_VALIDATION) && CONFIG_AMS_Z019_RECOVERY_VALIDATION
    if (!monitor.config_verified || monitor.temperature.config_cleanup_required || monitor.recovery.continuity_lost) {
        ams_adbms_monitor_interrupt(&monitor, !monitor.config_verified || monitor.temperature.config_cleanup_required ?
                                   AMS_ADBMS_RESULT_CONFIG_MISMATCH : AMS_ADBMS_RESULT_COUNTER);
    }
#endif
    publish_snapshot();
}

bool ams_adbms_monitor_platform_snapshot(ams_adbms_monitor_platform_snapshot_t *out)
{
    k_spinlock_key_t key;

    if (out == NULL) {
        return false;
    }
    key = k_spin_lock(&published_lock);
    *out = published;
    k_spin_unlock(&published_lock, key);
    return init_attempted;
}
