#include <ams_core/ams_fan_control.h>

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static unsigned checks;
static unsigned failures;

#define CHECK(x) do { checks++; if (!(x)) { failures++; \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); } } while (0)

static ams_fan_control_input_t valid_input(float temp)
{
    ams_fan_control_input_t in;
    memset(&in, 0, sizeof(in));
    in.max_temp = temp;
    in.temp_valid = true;
    in.temp_read_fault = false;
    in.temp_usable_sensor_count = 120U;
    in.temp_fault = false;
    in.temp_fan_max = false;
    in.state = 0U;
    in.fan_command_percent = 0.0f;
    in.fan_control_reason = AMS_FAN_CONTROL_REASON_OFF_COOL;
    return in;
}

static void expect(const ams_fan_control_input_t *in,
                   float expected,
                   uint8_t reason)
{
    uint8_t got_reason = 0xFFU;
    float got = ams_fan_percent_from_temp(in, &got_reason);
    if (fabsf(got - expected) >= 0.0005f || got_reason != reason) {
        fprintf(stderr, "expect temp=%f state=%u prev=%f prevreason=%u got=%f reason=%u expected=%f reason=%u\n",
                in ? in->max_temp : 0.0f, in ? in->state : 0U,
                in ? in->fan_command_percent : 0.0f, in ? in->fan_control_reason : 0U,
                got, got_reason, expected, reason);
    }
    CHECK(fabsf(got - expected) < 0.0005f);
    CHECK(got_reason == reason);
}

int main(void)
{
    CHECK(AMS_FAN_STATE_CHARGE == 2U);
    CHECK(AMS_FAN_CONTROL_REASON_OFF_COOL == 0);
    CHECK(AMS_FAN_CONTROL_REASON_RAMP == 1);
    CHECK(AMS_FAN_CONTROL_REASON_MIN_HYSTERESIS == 2);
    CHECK(AMS_FAN_CONTROL_REASON_CHARGE_WARM == 3);
    CHECK(AMS_FAN_CONTROL_REASON_MAX_TEMP == 4);
    CHECK(AMS_FAN_CONTROL_REASON_TEMP_INVALID == 5);
    CHECK(AMS_FAN_CONTROL_REASON_TEMP_FAULT == 6);
    CHECK(AMS_FAN_CONTROL_REASON_DRIVER_FAULT == 7);

    uint8_t reason = 0U;
    CHECK(ams_fan_percent_from_temp(NULL, &reason) == 100.0f);
    CHECK(reason == AMS_FAN_CONTROL_REASON_TEMP_INVALID);

    ams_fan_control_input_t in = valid_input(25.0f);
    in.temp_valid = false;
    expect(&in, 100.0f, AMS_FAN_CONTROL_REASON_TEMP_INVALID);
    in = valid_input(25.0f); in.temp_read_fault = true;
    expect(&in, 100.0f, AMS_FAN_CONTROL_REASON_TEMP_INVALID);
    in = valid_input(25.0f); in.temp_usable_sensor_count = 0U;
    expect(&in, 100.0f, AMS_FAN_CONTROL_REASON_TEMP_INVALID);
    in = valid_input(NAN);
    expect(&in, 100.0f, AMS_FAN_CONTROL_REASON_TEMP_INVALID);
    in = valid_input(25.0f); in.temp_fault = true;
    expect(&in, 100.0f, AMS_FAN_CONTROL_REASON_TEMP_FAULT);
    in = valid_input(25.0f); in.temp_fan_max = true;
    expect(&in, 100.0f, AMS_FAN_CONTROL_REASON_MAX_TEMP);

    in = valid_input(31.999f);
    expect(&in, 0.0f, AMS_FAN_CONTROL_REASON_OFF_COOL);
    in = valid_input(32.0f);
    expect(&in, 0.0f, AMS_FAN_CONTROL_REASON_OFF_COOL);
    in = valid_input(35.0f);
    expect(&in, 0.0f, AMS_FAN_CONTROL_REASON_OFF_COOL);

    in = valid_input(34.0f);
    in.fan_command_percent = 25.0f;
    in.fan_control_reason = AMS_FAN_CONTROL_REASON_RAMP;
    expect(&in, 25.0f, AMS_FAN_CONTROL_REASON_MIN_HYSTERESIS);

    in = valid_input(34.0f);
    in.fan_command_percent = 100.0f;
    in.fan_control_reason = AMS_FAN_CONTROL_REASON_TEMP_INVALID;
    expect(&in, 0.0f, AMS_FAN_CONTROL_REASON_OFF_COOL);

    in = valid_input(32.001f);
    in.state = AMS_FAN_STATE_CHARGE;
    expect(&in, 35.0f, AMS_FAN_CONTROL_REASON_CHARGE_WARM);
    in = valid_input(35.0f);
    in.state = AMS_FAN_STATE_CHARGE;
    expect(&in, 35.0f, AMS_FAN_CONTROL_REASON_CHARGE_WARM);

    in = valid_input(35.001f);
    expect(&in, 25.0f, AMS_FAN_CONTROL_REASON_RAMP);
    in = valid_input(40.0f);
    expect(&in, 33.333333f, AMS_FAN_CONTROL_REASON_RAMP);
    in = valid_input(40.0f); in.state = AMS_FAN_STATE_CHARGE;
    expect(&in, 35.0f, AMS_FAN_CONTROL_REASON_CHARGE_WARM);
    in = valid_input(42.5f);
    expect(&in, 50.0f, AMS_FAN_CONTROL_REASON_RAMP);
    in = valid_input(49.0f);
    expect(&in, 93.333333f, AMS_FAN_CONTROL_REASON_RAMP);
    in = valid_input(49.999f);
    CHECK(ams_fan_percent_from_temp(&in, &reason) < 100.0f);
    CHECK(reason == AMS_FAN_CONTROL_REASON_RAMP);
    in = valid_input(50.0f);
    expect(&in, 100.0f, AMS_FAN_CONTROL_REASON_MAX_TEMP);

    CHECK(strcmp(ams_fan_control_reason_str(AMS_FAN_CONTROL_REASON_OFF_COOL), "off_cool") == 0);
    CHECK(strcmp(ams_fan_control_reason_str(AMS_FAN_CONTROL_REASON_DRIVER_FAULT), "driver_fault") == 0);
    CHECK(strcmp(ams_fan_control_reason_str(0xFFU), "unknown") == 0);

    /* Stateful hysteresis sweep. */
    float prev = 0.0f;
    uint8_t prev_reason = AMS_FAN_CONTROL_REASON_OFF_COOL;
    for (int ti = 250; ti <= 520; ++ti) {
        float t = (float)ti / 10.0f;
        in = valid_input(t);
        in.fan_command_percent = prev;
        in.fan_control_reason = prev_reason;
        float pct = ams_fan_percent_from_temp(&in, &reason);
        CHECK(isfinite(pct));
        CHECK(pct >= 0.0f && pct <= 100.0f);
        if (t >= 50.0f) CHECK(pct == 100.0f);
        if (t <= 32.0f) CHECK(pct == 0.0f);
        prev = pct;
        prev_reason = reason;
    }

    if (failures != 0U) {
        fprintf(stderr, "fan control: FAIL (%u checks, %u failures)\n", checks, failures);
        return 1;
    }
    printf("fan control: PASS (%u checks)\n", checks);
    return 0;
}
