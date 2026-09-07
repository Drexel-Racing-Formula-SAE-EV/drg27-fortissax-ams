#include <stdint.h>

#include <ams_core/ams_core_config.h>
#include <ams_core/ams_core_contract.h>
#include <ams_core/ams_core_time.h>
#include <ams_core/ams_measurement.h>
#include <ams_core/ams_core_types.h>

typedef char ams_time_ms_must_be_32_bits[
    (sizeof(ams_time_ms_t) == 4U) ? 1 : -1
];

typedef char ams_sequence_must_be_32_bits[
    (sizeof(ams_sequence_t) == 4U) ? 1 : -1
];

typedef char ams_current_uncertainty_must_be_16_bits[
    (sizeof(ams_current_uncertainty_t) == 2U) ? 1 : -1
];

#if AMS_PHYSICAL_SEGMENT_COUNT != 5U
#error "DER26 physical segment count contract changed"
#endif

#if AMS_CELLS_PER_SEGMENT != 15U
#error "DER26 cells-per-segment contract changed"
#endif

#if AMS_TEMP_SENSORS_PER_SEGMENT != 24U
#error "DER26 temperature-sensors-per-segment contract changed"
#endif

#if AMS_TOTAL_CELL_COUNT != 75U
#error "DER26 total-cell-count contract changed"
#endif

#if AMS_TOTAL_TEMP_SENSOR_COUNT != 120U
#error "DER26 total-temperature-sensor-count contract changed"
#endif

int ams_core_contract_check(void)
{
    /*
     * Normal elapsed time.
     */
    if (ams_elapsed_ms(100U, 90U) != 10U) {
        return -1;
    }

    /*
     * Wrap:
     * UINT32_MAX - 4 == 0xFFFFFFFB
     * 0xFFFFFFFB -> 5 == 10 ms
     */
    if (ams_elapsed_ms(5U, UINT32_MAX - 4U) != 10U) {
        return -2;
    }

    /*
     * Exact freshness boundary is valid.
     */
    if (!ams_age_within_ms(100U, 90U, 10U)) {
        return -3;
    }

    /*
     * Beyond the boundary is expired.
     */
    if (ams_age_within_ms(100U, 90U, 9U)) {
        return -4;
    }

    if (!ams_age_expired_ms(100U, 90U, 9U)) {
        return -5;
    }

    if (ams_age_expired_ms(100U, 90U, 10U)) {
        return -6;
    }

    if ((ams_current_uncertainty_t)AMS_CURRENT_UNCERTAINTY_UNKNOWN !=
        (ams_current_uncertainty_t)UINT16_MAX) {
        return -7;
    }

    if (ams_measurement_buffer_count() != AMS_MEASUREMENT_BUFFER_COUNT) {
        return -8;
    }

    if (ams_measurement_snapshot_size_bytes() >
        AMS_MEASUREMENT_SNAPSHOT_MAX_BYTES) {
        return -9;
    }

    if (ams_measurement_store_size_bytes() >
        AMS_MEASUREMENT_STORE_MAX_BYTES) {
        return -10;
    }

    return 0;
}