#ifndef AMS_CORE_CONFIG_H_
#define AMS_CORE_CONFIG_H_

#include <stdint.h>

/*
 * Physical DER26 accumulator topology.
 *
 * These are physical-product constants, not historical build-profile
 * selections such as NSMBS.
 */
#define AMS_PHYSICAL_SEGMENT_COUNT        5U
#define AMS_CELLS_PER_SEGMENT            15U
#define AMS_TEMP_SENSORS_PER_SEGMENT     24U

#define AMS_TOTAL_CELL_COUNT \
    (AMS_PHYSICAL_SEGMENT_COUNT * AMS_CELLS_PER_SEGMENT)

#define AMS_TOTAL_TEMP_SENSOR_COUNT \
    (AMS_PHYSICAL_SEGMENT_COUNT * AMS_TEMP_SENSORS_PER_SEGMENT)

/*
 * Frozen v2.6.27 measurement-integrity sentinel.
 */
#define AMS_CURRENT_UNCERTAINTY_UNKNOWN  UINT16_MAX

#endif /* AMS_CORE_CONFIG_H_ */