#include <ams_core/ams_fan_control.h>

#include <math.h>
#include <stddef.h>

static float fan_temp_for_control(const ams_fan_control_input_t *data)
{
    if(data == NULL)
    {
        return NAN;
    }

    /* Use the live validated max for fan actuation. Filtered temperature is
     * exported for display/logging, not for delaying cooling response. */
    return data->max_temp;
}

float ams_fan_percent_from_temp(const ams_fan_control_input_t *data, uint8_t *reason_out)
{
    float span;
    float max_temp;
    float percent;
    bool was_on;

    if(reason_out != NULL)
    {
        *reason_out = AMS_FAN_CONTROL_REASON_OFF_COOL;
    }

    if(data == NULL)
    {
        if(reason_out != NULL) *reason_out = AMS_FAN_CONTROL_REASON_TEMP_INVALID;
        return 100.0f;
    }

    if(!data->temp_valid ||
       data->temp_read_fault ||
       (data->temp_usable_sensor_count == 0u) ||
       !isfinite(data->max_temp))
    {
        if(reason_out != NULL) *reason_out = AMS_FAN_CONTROL_REASON_TEMP_INVALID;
        return 100.0f;
    }

    if(data->temp_fault)
    {
        if(reason_out != NULL) *reason_out = AMS_FAN_CONTROL_REASON_TEMP_FAULT;
        return 100.0f;
    }

    max_temp = fan_temp_for_control(data);
    if(!isfinite(max_temp))
    {
        if(reason_out != NULL) *reason_out = AMS_FAN_CONTROL_REASON_TEMP_INVALID;
        return 100.0f;
    }

    if(data->temp_fan_max || (max_temp >= AMS_FAN_MAX_C))
    {
        if(reason_out != NULL) *reason_out = AMS_FAN_CONTROL_REASON_MAX_TEMP;
        return 100.0f;
    }

    span = (float)(AMS_FAN_MAX_C - AMS_FAN_RAMP_START_C);
    if(span <= 0.0f)
    {
        if(reason_out != NULL) *reason_out = AMS_FAN_CONTROL_REASON_MAX_TEMP;
        return 100.0f;
    }

    was_on = isfinite(data->fan_command_percent) &&
             (data->fan_command_percent > 0.5f) &&
             (data->fan_control_reason != AMS_FAN_CONTROL_REASON_MAX_TEMP) &&
             (data->fan_control_reason != AMS_FAN_CONTROL_REASON_TEMP_INVALID) &&
             (data->fan_control_reason != AMS_FAN_CONTROL_REASON_TEMP_FAULT) &&
             (data->fan_control_reason != AMS_FAN_CONTROL_REASON_DRIVER_FAULT);

    if(max_temp <= (AMS_FAN_RAMP_START_C - AMS_FAN_OFF_HYSTERESIS_C))
    {
        if(reason_out != NULL) *reason_out = AMS_FAN_CONTROL_REASON_OFF_COOL;
        return 0.0f;
    }

    if(max_temp <= AMS_FAN_RAMP_START_C)
    {
        if(was_on)
        {
            if(reason_out != NULL) *reason_out = AMS_FAN_CONTROL_REASON_MIN_HYSTERESIS;
            return AMS_FAN_MIN_COMMAND_PERCENT;
        }

        if((data->state == AMS_FAN_STATE_CHARGE) && (max_temp >= (AMS_FAN_RAMP_START_C - AMS_FAN_OFF_HYSTERESIS_C)))
        {
            if(reason_out != NULL) *reason_out = AMS_FAN_CONTROL_REASON_CHARGE_WARM;
            return AMS_FAN_CHARGE_WARM_PERCENT;
        }

        if(reason_out != NULL) *reason_out = AMS_FAN_CONTROL_REASON_OFF_COOL;
        return 0.0f;
    }

    percent = ((max_temp - (float)AMS_FAN_RAMP_START_C) * 100.0f) / span;
    if(percent < AMS_FAN_MIN_COMMAND_PERCENT)
    {
        percent = AMS_FAN_MIN_COMMAND_PERCENT;
    }

    if((data->state == AMS_FAN_STATE_CHARGE) && (percent < AMS_FAN_CHARGE_WARM_PERCENT))
    {
        percent = AMS_FAN_CHARGE_WARM_PERCENT;
        if(reason_out != NULL) *reason_out = AMS_FAN_CONTROL_REASON_CHARGE_WARM;
        return percent;
    }

    if(reason_out != NULL) *reason_out = AMS_FAN_CONTROL_REASON_RAMP;
    return percent;
}

const char *ams_fan_control_reason_str(uint8_t reason)
{
    switch((ams_fan_control_reason_t)reason)
    {
    case AMS_FAN_CONTROL_REASON_OFF_COOL:       return "off_cool";
    case AMS_FAN_CONTROL_REASON_RAMP:           return "ramp";
    case AMS_FAN_CONTROL_REASON_MIN_HYSTERESIS: return "min_hysteresis";
    case AMS_FAN_CONTROL_REASON_CHARGE_WARM:    return "charge_warm";
    case AMS_FAN_CONTROL_REASON_MAX_TEMP:       return "max_temp";
    case AMS_FAN_CONTROL_REASON_TEMP_INVALID:   return "temp_invalid";
    case AMS_FAN_CONTROL_REASON_TEMP_FAULT:     return "temp_fault";
    case AMS_FAN_CONTROL_REASON_DRIVER_FAULT:   return "driver_fault";
    default:                                return "unknown";
    }
}
