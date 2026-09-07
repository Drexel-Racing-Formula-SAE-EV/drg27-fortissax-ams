/*
 * Portable DER26 DHAB current-sensor processing.
 *
 * Behavioral oracle: DER26 AMS v2.6.27 / FW0.5.30.
 * Hardware acquisition is intentionally outside this object; Z-011 feeds the
 * same high-then-low ADC freshness state through the injection functions below.
 */

#ifndef AMS_CORE_AMS_CURRENT_SENSOR_H_
#define AMS_CORE_AMS_CURRENT_SENSOR_H_

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    CURRENT_SENSOR_RANGE_UNKNOWN = 0,
    CURRENT_SENSOR_RANGE_50A,
    CURRENT_SENSOR_RANGE_800A
} current_sensor_range_t;

typedef enum
{
    CURRENT_SENSOR_REASON_OK = 0,
    CURRENT_SENSOR_REASON_NULL,
    CURRENT_SENSOR_REASON_ADC_READ,
    CURRENT_SENSOR_REASON_ADC_IMPLAUSIBLE,
    CURRENT_SENSOR_REASON_SENSOR_SATURATION,
    CURRENT_SENSOR_REASON_CHANNEL_MISMATCH,
    CURRENT_SENSOR_REASON_NOT_MAPPED,
    CURRENT_SENSOR_REASON_ZERO_CAL_REJECTED,
    CURRENT_SENSOR_REASON_CALIBRATION_CHANGED
} current_sensor_reason_t;

#define CURRENT_SENSOR_CALIBRATION_MAGIC       0x4943414Cu
#define CURRENT_SENSOR_CALIBRATION_SCHEMA      1u
#define CURRENT_SENSOR_CALIBRATION_RECORD_SIZE 44u
#define CURRENT_SENSOR_CALIBRATION_TIME_UNKNOWN 0u
#define CURRENT_SENSOR_CALIBRATION_UNCERTAINTY_UNKNOWN UINT16_MAX

typedef struct
{
    uint32_t magic;
    uint16_t schema;
    uint16_t size;
    uint32_t calibration_id;
    uint32_t capture_time_s;
    int32_t zero_offset_50a_mA;
    int32_t zero_offset_800a_mA;
    uint32_t adc_vref_uV;
    uint32_t sensor_supply_uV;
    int16_t calibration_temp_deci_c;
    uint16_t uncertainty_50a_mA;
    uint16_t uncertainty_800a_mA;
    uint16_t reserved;
    uint32_t crc32;
} current_sensor_calibration_record_t;

typedef struct
{
    uint32_t calibration_id;
    uint32_t capture_time_s;
    int16_t calibration_temp_deci_c;
    uint16_t uncertainty_50a_mA;
    uint16_t uncertainty_800a_mA;
} current_sensor_calibration_metadata_t;

typedef struct
{
    float current;

    float voltage_high;
    float voltage_low;
    float sensor_voltage_high;
    float sensor_voltage_low;

    float current_high;
    float current_low;
    float current_50a;
    float current_800a;
    float current_50a_raw;
    float current_800a_raw;

    float current_50a_filtered;
    float current_800a_filtered;
    float current_filtered;
    bool filter_initialized;

    float zero_offset_50a;
    float zero_offset_800a;
    bool zero_calibrated;
    uint32_t zero_cal_count;

    bool calibration_loaded_from_record;
    uint32_t calibration_id;
    uint32_t calibration_capture_time_s;
    int16_t calibration_temp_deci_c;
    uint16_t calibration_uncertainty_50a_mA;
    uint16_t calibration_uncertainty_800a_mA;
    uint32_t calibration_restore_count;

    float adc_vref_v;
    float sensor_supply_v;

    uint16_t count_high;
    uint16_t count_low;
    bool count_high_fresh;
    bool count_low_fresh;

    bool last_read_ok;
    bool current_valid;
    current_sensor_range_t selected_range;
    current_sensor_reason_t reason;
} current_sensor_t;

void current_sensor_init(current_sensor_t *dev);

/*
 * Hardware-neutral equivalent of the v2.6.27 high-then-low ADC transaction.
 * begin() invalidates freshness; publish_high()/publish_low() mark individual
 * conversions fresh; finish() establishes last_read_ok only if both succeeded.
 * Numeric history is intentionally retained on failure.
 */
void current_sensor_adc_begin(current_sensor_t *dev);
void current_sensor_adc_publish_high(current_sensor_t *dev, uint16_t count);
void current_sensor_adc_publish_low(current_sensor_t *dev, uint16_t count);
bool current_sensor_adc_finish(current_sensor_t *dev);

float current_sensor_convert(current_sensor_t *dev);
void current_sensor_set_reference_voltages(current_sensor_t *dev,
                                           float adc_vref_v,
                                           float sensor_supply_v);
bool current_sensor_zero_calibrate(current_sensor_t *dev);
void current_sensor_zero_clear(current_sensor_t *dev);
bool current_sensor_calibration_record_create(
    const current_sensor_t *dev,
    const current_sensor_calibration_metadata_t *metadata,
    current_sensor_calibration_record_t *record);
bool current_sensor_calibration_record_valid(
    const current_sensor_calibration_record_t *record);
bool current_sensor_calibration_apply(
    current_sensor_t *dev,
    const current_sensor_calibration_record_t *record,
    bool zero_current_proven);
bool current_sensor_calibration_confident(const current_sensor_t *dev);
const char *current_sensor_reason_str(current_sensor_reason_t reason);
const char *current_sensor_range_str(current_sensor_range_t range);

#ifdef __cplusplus
}
#endif

#endif /* AMS_CORE_AMS_CURRENT_SENSOR_H_ */
