#include <ams_core/ams_current_sensor.h>

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static unsigned checks;
static unsigned failures;

#define CHECK(expr) do { \
    checks++; \
    if (!(expr)) { \
        failures++; \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); \
    } \
} while (0)

#define CHECK_NEAR(a,b,tol) do { \
    float _a=(a), _b=(b), _t=(tol); \
    checks++; \
    if (!isfinite(_a) || fabsf(_a-_b) > _t) { \
        failures++; \
        fprintf(stderr, "FAIL %s:%d: %.9g vs %.9g tol %.9g\n", \
                __FILE__, __LINE__, (double)_a, (double)_b, (double)_t); \
    } \
} while (0)

static uint16_t count_for_sensor_voltage(float sensor_v, float vref)
{
    float adc_v = sensor_v * 0.6f;
    long count = lroundf((adc_v / vref) * 4095.0f);
    if (count < 0) count = 0;
    if (count > 4095) count = 4095;
    return (uint16_t)count;
}

static void pair(current_sensor_t *s, uint16_t hi, uint16_t lo)
{
    current_sensor_adc_begin(s);
    current_sensor_adc_publish_high(s, hi);
    current_sensor_adc_publish_low(s, lo);
    CHECK(current_sensor_adc_finish(s));
}

static void test_zero_and_mapping(void)
{
    current_sensor_t s;
    current_sensor_init(&s);
    pair(&s, count_for_sensor_voltage(2.5f, 3.3f),
             count_for_sensor_voltage(2.5f, 3.3f));
    CHECK_NEAR(current_sensor_convert(&s), 0.0f, 0.05f);
    CHECK(s.current_valid);
    CHECK(s.reason == CURRENT_SENSOR_REASON_OK);
    CHECK(s.selected_range == CURRENT_SENSOR_RANGE_50A);

    current_sensor_init(&s);
    pair(&s, count_for_sensor_voltage(2.55f, 3.3f),  /* 20A on 800A */
             count_for_sensor_voltage(3.30f, 3.3f)); /* 20A on 50A */
    CHECK_NEAR(current_sensor_convert(&s), 20.0f, 0.30f);
    CHECK(s.selected_range == CURRENT_SENSOR_RANGE_50A);
    CHECK_NEAR(s.current_800a, 20.0f, 1.5f);

    current_sensor_init(&s);
    pair(&s, count_for_sensor_voltage(2.65f, 3.3f),
             count_for_sensor_voltage(4.75f, 3.3f));
    CHECK_NEAR(current_sensor_convert(&s), 60.0f, 1.5f);
    CHECK(s.selected_range == CURRENT_SENSOR_RANGE_800A);
}

static void test_freshness_and_numeric_retention(void)
{
    current_sensor_t s;
    current_sensor_init(&s);
    pair(&s, count_for_sensor_voltage(2.55f, 3.3f),
             count_for_sensor_voltage(3.30f, 3.3f));
    (void)current_sensor_convert(&s);
    float previous = s.current;
    CHECK(s.current_valid);

    current_sensor_adc_begin(&s);
    current_sensor_adc_publish_high(&s, count_for_sensor_voltage(2.55f, 3.3f));
    CHECK(!current_sensor_adc_finish(&s));
    CHECK(!s.current_valid);
    CHECK(s.reason == CURRENT_SENSOR_REASON_ADC_READ);
    CHECK(s.count_high_fresh);
    CHECK(!s.count_low_fresh);
    CHECK_NEAR(s.current, previous, 0.0001f);

    (void)current_sensor_convert(&s);
    CHECK(!s.current_valid);
    CHECK(s.reason == CURRENT_SENSOR_REASON_ADC_READ);
    CHECK_NEAR(s.current, previous, 0.0001f);
}

static void test_count_boundaries(void)
{
    current_sensor_t s;
    uint16_t zero = count_for_sensor_voltage(2.5f, 3.3f);

    current_sensor_init(&s);
    pair(&s, 99u, zero);
    (void)current_sensor_convert(&s);
    CHECK(!s.current_valid);
    CHECK(s.reason == CURRENT_SENSOR_REASON_ADC_IMPLAUSIBLE);

    current_sensor_init(&s);
    pair(&s, 100u, zero);
    (void)current_sensor_convert(&s);
    /* 100 counts can still fail sensor-voltage range, but not count check. */
    CHECK(s.reason == CURRENT_SENSOR_REASON_ADC_IMPLAUSIBLE);

    current_sensor_init(&s);
    pair(&s, 3801u, zero);
    (void)current_sensor_convert(&s);
    CHECK(!s.current_valid);
    CHECK(s.reason == CURRENT_SENSOR_REASON_ADC_IMPLAUSIBLE);

    current_sensor_init(&s);
    pair(&s, 3800u, zero);
    (void)current_sensor_convert(&s);
    CHECK(s.reason == CURRENT_SENSOR_REASON_ADC_IMPLAUSIBLE ||
          s.reason == CURRENT_SENSOR_REASON_SENSOR_SATURATION ||
          s.reason == CURRENT_SENSOR_REASON_CHANNEL_MISMATCH ||
          s.reason == CURRENT_SENSOR_REASON_OK);
}

static void test_hysteresis_and_mismatch(void)
{
    current_sensor_t s;
    current_sensor_init(&s);

    pair(&s, count_for_sensor_voltage(2.625f, 3.3f),
             count_for_sensor_voltage(4.50f, 3.3f)); /* ~50A */
    (void)current_sensor_convert(&s);
    CHECK(s.current_valid);
    CHECK(s.selected_range == CURRENT_SENSOR_RANGE_800A);

    pair(&s, count_for_sensor_voltage(2.600f, 3.3f),
             count_for_sensor_voltage(4.10f, 3.3f)); /* ~40A */
    (void)current_sensor_convert(&s);
    CHECK(s.current_valid);
    CHECK(s.selected_range == CURRENT_SENSOR_RANGE_800A);

    pair(&s, count_for_sensor_voltage(2.590f, 3.3f),
             count_for_sensor_voltage(3.94f, 3.3f)); /* ~36A */
    (void)current_sensor_convert(&s);
    CHECK(s.current_valid);
    CHECK(s.selected_range == CURRENT_SENSOR_RANGE_50A);

    current_sensor_init(&s);
    pair(&s, count_for_sensor_voltage(2.50f, 3.3f),
             count_for_sensor_voltage(3.30f, 3.3f));
    (void)current_sensor_convert(&s);
    CHECK(!s.current_valid);
    CHECK(s.reason == CURRENT_SENSOR_REASON_CHANNEL_MISMATCH);
}

static void test_deadband_filter_and_reference_change(void)
{
    current_sensor_t s;
    current_sensor_init(&s);

    pair(&s, count_for_sensor_voltage(2.5005f, 3.3f),
             count_for_sensor_voltage(2.508f, 3.3f));
    (void)current_sensor_convert(&s);
    CHECK(s.current_valid);
    CHECK_NEAR(s.current, 0.0f, 0.05f);
    CHECK(s.filter_initialized);

    pair(&s, count_for_sensor_voltage(2.55f, 3.3f),
             count_for_sensor_voltage(3.30f, 3.3f));
    (void)current_sensor_convert(&s);
    CHECK(s.current_valid);
    CHECK_NEAR(s.current_filtered, 5.0f, 0.6f);
    CHECK_NEAR(s.current, 20.0f, 0.4f);

    current_sensor_set_reference_voltages(&s, 3.2f, 4.9f);
    CHECK(!s.current_valid);
    CHECK(s.selected_range == CURRENT_SENSOR_RANGE_UNKNOWN);
    CHECK(s.reason == CURRENT_SENSOR_REASON_CALIBRATION_CHANGED);
    CHECK(!s.filter_initialized);
}

static void test_calibration_record(void)
{
    current_sensor_t source, restored, wrong;
    current_sensor_calibration_record_t rec, bad;
    current_sensor_calibration_metadata_t meta = {
        .calibration_id = 42u,
        .capture_time_s = 1784563200u,
        .calibration_temp_deci_c = 235,
        .uncertainty_50a_mA = 200u,
        .uncertainty_800a_mA = 2000u,
    };

    current_sensor_init(&source);
    pair(&source, count_for_sensor_voltage(2.505f, 3.3f),
                  count_for_sensor_voltage(2.540f, 3.3f));
    (void)current_sensor_convert(&source);
    CHECK(current_sensor_zero_calibrate(&source));
    CHECK(!current_sensor_calibration_confident(&source));
    CHECK(current_sensor_calibration_record_create(&source, &meta, &rec));
    CHECK(current_sensor_calibration_record_valid(&rec));
    CHECK(sizeof(rec) == 44u);

    bad = rec;
    bad.zero_offset_50a_mA++;
    CHECK(!current_sensor_calibration_record_valid(&bad));

    current_sensor_init(&restored);
    pair(&restored, source.count_high, source.count_low);
    (void)current_sensor_convert(&restored);
    CHECK(!current_sensor_calibration_apply(&restored, &rec, false));
    CHECK(current_sensor_calibration_apply(&restored, &rec, true));
    CHECK(restored.calibration_loaded_from_record);
    CHECK(restored.reason == CURRENT_SENSOR_REASON_CALIBRATION_CHANGED);
    CHECK(!restored.current_valid);
    CHECK(current_sensor_calibration_confident(&restored));

    restored.calibration_uncertainty_50a_mA = 501u;
    CHECK(!current_sensor_calibration_confident(&restored));
    restored.calibration_uncertainty_50a_mA = 500u;
    restored.calibration_uncertainty_800a_mA = 5000u;
    CHECK(current_sensor_calibration_confident(&restored));
    restored.calibration_uncertainty_800a_mA = 5001u;
    CHECK(!current_sensor_calibration_confident(&restored));

    current_sensor_init(&wrong);
    pair(&wrong, count_for_sensor_voltage(2.5f, 3.3f),
                 count_for_sensor_voltage(2.5f, 3.3f));
    (void)current_sensor_convert(&wrong);
    CHECK(!current_sensor_calibration_apply(&wrong, &rec, true));
}

int main(void)
{
    test_zero_and_mapping();
    test_freshness_and_numeric_retention();
    test_count_boundaries();
    test_hysteresis_and_mismatch();
    test_deadband_filter_and_reference_change();
    test_calibration_record();

    printf("current sensor: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
