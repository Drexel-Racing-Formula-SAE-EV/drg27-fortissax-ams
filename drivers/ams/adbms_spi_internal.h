#ifndef DRG27_AMS_ADBMS_SPI_INTERNAL_H_
#define DRG27_AMS_ADBMS_SPI_INTERNAL_H_

#include "adbms_spi_engine.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * PRIVATE TRANSPORT SURFACE.
 *
 * Z-015 has zero runtime transfer callers.  This header is deliberately kept
 * under drivers/ams instead of include/ams_platform so CLI/application code
 * cannot acquire a convenient raw-SPI escape hatch.  The future ADBMS owner
 * protocol implementation is the only production caller this header may gain.
 */
ams_adbms_spi_result_t ams_adbms_spi_write(
    ams_adbms_spi_string_t string,
    const uint8_t *tx,
    size_t tx_len);

ams_adbms_spi_result_t ams_adbms_spi_write_read(
    ams_adbms_spi_string_t string,
    const uint8_t *tx,
    size_t tx_len,
    uint8_t *rx,
    size_t rx_len);

#endif /* DRG27_AMS_ADBMS_SPI_INTERNAL_H_ */
