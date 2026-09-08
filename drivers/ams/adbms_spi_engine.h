#ifndef DRG27_AMS_ADBMS_SPI_ENGINE_H_
#define DRG27_AMS_ADBMS_SPI_ENGINE_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    AMS_ADBMS_SPI_STRING_A = 0,
    AMS_ADBMS_SPI_STRING_B = 1,
} ams_adbms_spi_string_t;

typedef enum {
    AMS_ADBMS_SPI_RESULT_OK = 0,
    AMS_ADBMS_SPI_RESULT_INVALID_ARGUMENT,
    AMS_ADBMS_SPI_RESULT_IO_ERROR,
    AMS_ADBMS_SPI_RESULT_TIMEOUT,
    AMS_ADBMS_SPI_RESULT_RECOVERY_FAILED,
    AMS_ADBMS_SPI_RESULT_INTERNAL_FAULT,
} ams_adbms_spi_result_t;

/*
 * Hardware-facing hooks for the bounded synchronous transport engine.
 *
 * The engine intentionally has no Zephyr, STM32, RTOS, heap, callback,
 * interrupt, DMA, or async-completion dependency.  Z-015 host/SIL compiles
 * this exact implementation against a deterministic fake backend; the target
 * adapter binds the same engine to the private STM32F767 SPI6 polling backend.
 */
struct ams_adbms_spi_backend {
    void *context;

    uint32_t (*now_ms)(void *context);
    int (*set_cs_active)(void *context, ams_adbms_spi_string_t string, bool active);
    int (*start)(void *context);
    bool (*tx_ready)(void *context);
    bool (*rx_ready)(void *context);
    bool (*busy)(void *context);
    bool (*fault)(void *context);
    int (*write_byte)(void *context, uint8_t value);
    int (*read_byte)(void *context, uint8_t *value);
    int (*stop)(void *context);
    int (*recover)(void *context);
};

struct ams_adbms_spi_engine_config {
    uint32_t timeout_ms;
    size_t max_transfer_bytes;
    uint8_t read_dummy_byte;
};

ams_adbms_spi_result_t ams_adbms_spi_engine_write(
    const struct ams_adbms_spi_backend *backend,
    const struct ams_adbms_spi_engine_config *config,
    ams_adbms_spi_string_t string,
    const uint8_t *tx,
    size_t tx_len);

ams_adbms_spi_result_t ams_adbms_spi_engine_write_read(
    const struct ams_adbms_spi_backend *backend,
    const struct ams_adbms_spi_engine_config *config,
    ams_adbms_spi_string_t string,
    const uint8_t *tx,
    size_t tx_len,
    uint8_t *rx,
    size_t rx_len);

#ifdef __cplusplus
}
#endif

#endif /* DRG27_AMS_ADBMS_SPI_ENGINE_H_ */
