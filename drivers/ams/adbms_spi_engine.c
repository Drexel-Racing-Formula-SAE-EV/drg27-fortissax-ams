#include "adbms_spi_engine.h"

#include <string.h>

static bool backend_valid(const struct ams_adbms_spi_backend *backend)
{
    return (backend != NULL) &&
           (backend->now_ms != NULL) &&
           (backend->set_cs_active != NULL) &&
           (backend->start != NULL) &&
           (backend->tx_ready != NULL) &&
           (backend->rx_ready != NULL) &&
           (backend->busy != NULL) &&
           (backend->fault != NULL) &&
           (backend->write_byte != NULL) &&
           (backend->read_byte != NULL) &&
           (backend->stop != NULL) &&
           (backend->recover != NULL);
}

static bool config_valid(const struct ams_adbms_spi_engine_config *config)
{
    return (config != NULL) &&
           (config->timeout_ms != 0U) &&
           (config->max_transfer_bytes != 0U);
}

static bool string_valid(ams_adbms_spi_string_t string)
{
    return (string == AMS_ADBMS_SPI_STRING_A) ||
           (string == AMS_ADBMS_SPI_STRING_B);
}

static bool elapsed_timeout(uint32_t start_ms, uint32_t now_ms, uint32_t timeout_ms)
{
    /* Same wrap-safe unsigned elapsed-time form used throughout the AMS
     * runtime. The ready/fault condition is always inspected before this
     * deadline test, matching the HAL wait-loop ordering used by v2.6.27. */
    return (uint32_t)(now_ms - start_ms) >= timeout_ms;
}

static ams_adbms_spi_result_t recover_after_failure(
    const struct ams_adbms_spi_backend *backend,
    ams_adbms_spi_string_t string,
    ams_adbms_spi_result_t original_result)
{
    int cs_ret;
    int recovery_ret;

    /* CS-high is the first failure action. The target recovery backend also
     * redundantly drives both strings inactive before resetting SPI6. */
    cs_ret = backend->set_cs_active(backend->context, string, false);
    recovery_ret = backend->recover(backend->context);

    if ((cs_ret != 0) || (recovery_ret != 0)) {
        return AMS_ADBMS_SPI_RESULT_RECOVERY_FAILED;
    }

    return original_result;
}

static ams_adbms_spi_result_t wait_tx_ready(
    const struct ams_adbms_spi_backend *backend,
    uint32_t start_ms,
    uint32_t timeout_ms)
{
    for (;;) {
        if (backend->fault(backend->context)) {
            return AMS_ADBMS_SPI_RESULT_IO_ERROR;
        }
        if (backend->tx_ready(backend->context)) {
            return AMS_ADBMS_SPI_RESULT_OK;
        }
        if (elapsed_timeout(start_ms,
                            backend->now_ms(backend->context),
                            timeout_ms)) {
            return AMS_ADBMS_SPI_RESULT_TIMEOUT;
        }
    }
}

static ams_adbms_spi_result_t wait_rx_ready(
    const struct ams_adbms_spi_backend *backend,
    uint32_t start_ms,
    uint32_t timeout_ms)
{
    for (;;) {
        if (backend->fault(backend->context)) {
            return AMS_ADBMS_SPI_RESULT_IO_ERROR;
        }
        if (backend->rx_ready(backend->context)) {
            return AMS_ADBMS_SPI_RESULT_OK;
        }
        if (elapsed_timeout(start_ms,
                            backend->now_ms(backend->context),
                            timeout_ms)) {
            return AMS_ADBMS_SPI_RESULT_TIMEOUT;
        }
    }
}

static ams_adbms_spi_result_t wait_not_busy(
    const struct ams_adbms_spi_backend *backend,
    uint32_t start_ms,
    uint32_t timeout_ms)
{
    for (;;) {
        if (backend->fault(backend->context)) {
            return AMS_ADBMS_SPI_RESULT_IO_ERROR;
        }
        if (!backend->busy(backend->context)) {
            return AMS_ADBMS_SPI_RESULT_OK;
        }
        if (elapsed_timeout(start_ms,
                            backend->now_ms(backend->context),
                            timeout_ms)) {
            return AMS_ADBMS_SPI_RESULT_TIMEOUT;
        }
    }
}

static ams_adbms_spi_result_t transfer(
    const struct ams_adbms_spi_backend *backend,
    const struct ams_adbms_spi_engine_config *config,
    ams_adbms_spi_string_t string,
    const uint8_t *tx,
    size_t tx_len,
    uint8_t *rx,
    size_t rx_len)
{
    uint32_t start_ms;
    size_t total_len;
    ams_adbms_spi_result_t result;

    if (!backend_valid(backend) || !config_valid(config) || !string_valid(string) ||
        (tx == NULL) || (tx_len == 0U)) {
        return AMS_ADBMS_SPI_RESULT_INVALID_ARGUMENT;
    }

    if ((rx_len != 0U) && (rx == NULL)) {
        return AMS_ADBMS_SPI_RESULT_INVALID_ARGUMENT;
    }

    if (tx_len > config->max_transfer_bytes ||
        rx_len > config->max_transfer_bytes ||
        tx_len > (config->max_transfer_bytes - rx_len)) {
        return AMS_ADBMS_SPI_RESULT_INVALID_ARGUMENT;
    }

    total_len = tx_len + rx_len;
    start_ms = backend->now_ms(backend->context);

    if (backend->set_cs_active(backend->context, string, true) != 0) {
        /* A failed assertion has ambiguous electrical state. Do not assume
         * the line stayed inactive: force the same CS-high + peripheral
         * recovery path used for an in-flight transport failure. */
        return recover_after_failure(backend, string,
                                     AMS_ADBMS_SPI_RESULT_IO_ERROR);
    }

    if (backend->start(backend->context) != 0) {
        return recover_after_failure(backend, string,
                                     AMS_ADBMS_SPI_RESULT_IO_ERROR);
    }

    for (size_t index = 0U; index < total_len; ++index) {
        uint8_t rx_byte = 0U;
        uint8_t tx_byte = (index < tx_len) ? tx[index] : config->read_dummy_byte;

        result = wait_tx_ready(backend, start_ms, config->timeout_ms);
        if (result != AMS_ADBMS_SPI_RESULT_OK) {
            return recover_after_failure(backend, string, result);
        }

        if (backend->write_byte(backend->context, tx_byte) != 0) {
            return recover_after_failure(backend, string,
                                         AMS_ADBMS_SPI_RESULT_IO_ERROR);
        }

        /* SPI6 remains full duplex even for logical write-only operations.
         * Drain every received byte so RXNE/OVR cannot accumulate behind a
         * write transaction. */
        result = wait_rx_ready(backend, start_ms, config->timeout_ms);
        if (result != AMS_ADBMS_SPI_RESULT_OK) {
            return recover_after_failure(backend, string, result);
        }

        if (backend->read_byte(backend->context, &rx_byte) != 0) {
            return recover_after_failure(backend, string,
                                         AMS_ADBMS_SPI_RESULT_IO_ERROR);
        }

        if ((rx != NULL) && (index >= tx_len)) {
            rx[index - tx_len] = rx_byte;
        }
    }

    result = wait_not_busy(backend, start_ms, config->timeout_ms);
    if (result != AMS_ADBMS_SPI_RESULT_OK) {
        if (rx != NULL) {
            memset(rx, 0, rx_len);
        }
        return recover_after_failure(backend, string, result);
    }

    if (backend->stop(backend->context) != 0) {
        if (rx != NULL) {
            memset(rx, 0, rx_len);
        }
        return recover_after_failure(backend, string,
                                     AMS_ADBMS_SPI_RESULT_IO_ERROR);
    }

    if (backend->set_cs_active(backend->context, string, false) != 0) {
        if (rx != NULL) {
            memset(rx, 0, rx_len);
        }
        return recover_after_failure(backend, string,
                                     AMS_ADBMS_SPI_RESULT_IO_ERROR);
    }

    return AMS_ADBMS_SPI_RESULT_OK;
}

ams_adbms_spi_result_t ams_adbms_spi_engine_write(
    const struct ams_adbms_spi_backend *backend,
    const struct ams_adbms_spi_engine_config *config,
    ams_adbms_spi_string_t string,
    const uint8_t *tx,
    size_t tx_len)
{
    return transfer(backend, config, string, tx, tx_len, NULL, 0U);
}

ams_adbms_spi_result_t ams_adbms_spi_engine_write_read(
    const struct ams_adbms_spi_backend *backend,
    const struct ams_adbms_spi_engine_config *config,
    ams_adbms_spi_string_t string,
    const uint8_t *tx,
    size_t tx_len,
    uint8_t *rx,
    size_t rx_len)
{
    ams_adbms_spi_result_t result =
        transfer(backend, config, string, tx, tx_len, rx, rx_len);

    if ((result != AMS_ADBMS_SPI_RESULT_OK) && (rx != NULL) && (rx_len != 0U)) {
        memset(rx, 0, rx_len);
    }

    return result;
}
