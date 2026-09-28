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
 * The base image has zero runtime transfer callers. This header is kept
 * under drivers/ams instead of include/ams_platform so CLI/application code
 * cannot acquire a convenient raw-SPI escape hatch. Z016 and Z017 are
 * mutually-exclusive profiles; their dedicated ADBMS thread is the only
 * approved production owner.
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

#if (defined(CONFIG_AMS_Z016_LINK_PROBE) && CONFIG_AMS_Z016_LINK_PROBE) || \
    (defined(CONFIG_AMS_Z017_CELL_VALIDATION) && CONFIG_AMS_Z017_CELL_VALIDATION) || \
    (defined(CONFIG_AMS_Z018_TEMP_VALIDATION) && CONFIG_AMS_Z018_TEMP_VALIDATION)
bool ams_adbms_spi_bind_owner(void);
ams_adbms_spi_result_t ams_adbms_spi_wake_b(bool cold);
#endif
#ifdef __cplusplus
}
#endif
#endif /* DRG27_AMS_ADBMS_SPI_INTERNAL_H_ */
