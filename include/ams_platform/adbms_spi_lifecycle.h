#ifndef DRG27_AMS_PLATFORM_ADBMS_SPI_LIFECYCLE_H_
#define DRG27_AMS_PLATFORM_ADBMS_SPI_LIFECYCLE_H_

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    AMS_ADBMS_SPI_PLATFORM_UNINITIALIZED = 0,
    AMS_ADBMS_SPI_PLATFORM_READY,
    AMS_ADBMS_SPI_PLATFORM_ACTIVE,
    AMS_ADBMS_SPI_PLATFORM_RECOVERING,
    AMS_ADBMS_SPI_PLATFORM_FAULTED,
} ams_adbms_spi_platform_state_t;

typedef struct {
    ams_adbms_spi_platform_state_t state;
    uint32_t input_clock_hz;
    uint32_t achieved_clock_hz;
    uint32_t timeout_ms;
    uint32_t max_transfer_bytes;
    uint32_t init_attempt_count;
    uint32_t transfer_success_count;
    uint32_t transfer_timeout_count;
    uint32_t transfer_io_error_count;
    uint32_t integrity_violation_count;
    uint32_t recovery_success_count;
    uint32_t recovery_failure_count;
    int last_error;
    uint32_t last_transport_result;
    bool cs_idle_guaranteed;
    bool irq_path_disabled;
} ams_adbms_spi_platform_status_t;

/* Startup-only platform preparation. It configures/validates the private SPI6
 * backend and leaves CS_A/CS_B inactive. It performs no SPI transfer, wake
 * pulse, ADBMS command, or actor promotion. */
int ams_adbms_spi_platform_init(void);

ams_adbms_spi_platform_status_t ams_adbms_spi_platform_status(void);

#ifdef __cplusplus
}
#endif

#endif /* DRG27_AMS_PLATFORM_ADBMS_SPI_LIFECYCLE_H_ */
