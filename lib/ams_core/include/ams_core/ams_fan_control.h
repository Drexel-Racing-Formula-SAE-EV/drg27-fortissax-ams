#ifndef AMS_CORE_AMS_FAN_CONTROL_H_
#define AMS_CORE_AMS_FAN_CONTROL_H_

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Exact v2.6.27 thermal fan policy constants. */
#define AMS_FAN_RAMP_START_C        35.0f
#define AMS_FAN_MAX_C               50.0f
#define AMS_FAN_MIN_COMMAND_PERCENT 25.0f
#define AMS_FAN_CHARGE_WARM_PERCENT 35.0f
#define AMS_FAN_OFF_HYSTERESIS_C     3.0f

#define AMS_FAN_STATE_CHARGE 2U

typedef enum {
    AMS_FAN_CONTROL_REASON_OFF_COOL = 0,
    AMS_FAN_CONTROL_REASON_RAMP,
    AMS_FAN_CONTROL_REASON_MIN_HYSTERESIS,
    AMS_FAN_CONTROL_REASON_CHARGE_WARM,
    AMS_FAN_CONTROL_REASON_MAX_TEMP,
    AMS_FAN_CONTROL_REASON_TEMP_INVALID,
    AMS_FAN_CONTROL_REASON_TEMP_FAULT,
    AMS_FAN_CONTROL_REASON_DRIVER_FAULT
} ams_fan_control_reason_t;

typedef struct {
    float max_temp;
    bool temp_valid;
    bool temp_read_fault;
    uint8_t temp_usable_sensor_count;
    bool temp_fault;
    bool temp_fan_max;

    uint8_t state;

    /* Previous applied policy output. Required for exact hysteresis parity. */
    float fan_command_percent;
    uint8_t fan_control_reason;
} ams_fan_control_input_t;

float ams_fan_percent_from_temp(const ams_fan_control_input_t *data,
                                uint8_t *reason_out);

const char *ams_fan_control_reason_str(uint8_t reason);

#ifdef __cplusplus
}
#endif

#endif /* AMS_CORE_AMS_FAN_CONTROL_H_ */
