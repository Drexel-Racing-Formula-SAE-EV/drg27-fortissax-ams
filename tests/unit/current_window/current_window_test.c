#include <ams_core/ams_current_window.h>

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CHECK(condition)                                                       \
    do                                                                         \
    {                                                                          \
        if(!(condition))                                                       \
        {                                                                      \
            fprintf(stderr, "FAIL %s:%d: %s\n",                              \
                    __FILE__, __LINE__, #condition);                           \
            return false;                                                      \
        }                                                                      \
    } while(0)

#define NEAR(actual, expected, tolerance)                                      \
    CHECK(fabs((double)(actual) - (double)(expected)) <= (double)(tolerance))

static void sample(ams_current_window_accumulator_t *acc,
                   uint32_t tick,
                   float current_A,
                   uint16_t uncertainty_mA,
                   uint8_t range,
                   bool calibration_confident,
                   uint32_t calibration_id)
{
    ams_current_window_set_sensor_metadata(acc, uncertainty_mA, range);
    ams_current_window_update(acc,
                              tick,
                              current_A,
                              current_A,
                              true,
                              calibration_confident,
                              calibration_id);
}

static bool test_late_boundary_rejection_and_recovery(void)
{
    ams_current_window_accumulator_t acc;
    ams_current_window_t window;

    ams_current_window_init(&acc, 80U);
    sample(&acc, 90U, 10.0f, 100U, 1U, true, 42U);
    sample(&acc, 110U, 10.0f, 100U, 1U, true, 42U);

    /* Simulate a boundary timestamp captured before the newer sample. */
    CHECK(!ams_current_window_rotate(&acc, 100U, &window));
    CHECK(acc.active.start_tick == 80U);
    CHECK(acc.active.end_tick == 110U);
    CHECK(acc.active.invalid_sample_count == 1U);

    sample(&acc, 130U, 10.0f, 100U, 1U, true, 42U);
    CHECK(!ams_current_window_rotate(&acc, 190U, &window));
    NEAR(window.charge_As, 1.1, 0.00001);
    NEAR(window.average_A, 10.0, 0.00001);

    sample(&acc, 210U, 10.0f, 100U, 1U, true, 42U);
    CHECK(ams_current_window_rotate(&acc, 230U, &window));
    NEAR(window.average_A, 10.0, 0.00001);

    return true;
}

static bool test_carried_metadata_extrema_and_mixed_range(void)
{
    ams_current_window_accumulator_t acc;
    ams_current_window_t window;

    ams_current_window_init(&acc, 0U);

    sample(&acc, 10U, 30.0f, 500U, 1U, true, 42U);
    CHECK(ams_current_window_rotate(&acc, 20U, &window));

    sample(&acc, 30U, 10.0f, 100U, 2U, true, 42U);
    sample(&acc, 40U, 10.0f, 100U, 2U, true, 42U);
    CHECK(ams_current_window_rotate(&acc, 50U, &window));

    CHECK(window.uncertainty_mA == 500U);
    CHECK(window.selected_range == 0U);
    NEAR(window.min_A, 10.0, 0.00001);
    NEAR(window.max_A, 30.0, 0.00001);
    NEAR(window.charge_As, 0.4, 0.00001);

    sample(&acc, 60U, 10.0f, 100U, 2U, true, 42U);
    CHECK(ams_current_window_rotate(&acc, 70U, &window));
    CHECK(window.uncertainty_mA == 100U);
    CHECK(window.selected_range == 2U);

    return true;
}

static bool test_unknown_uncertainty_and_late_sample(void)
{
    ams_current_window_accumulator_t acc;
    ams_current_window_t window;

    ams_current_window_init(&acc, 0U);

    sample(&acc,
           10U,
           10.0f,
           AMS_CURRENT_UNCERTAINTY_UNKNOWN,
           1U,
           true,
           42U);
    CHECK(ams_current_window_rotate(&acc, 20U, &window));

    sample(&acc, 30U, 10.0f, 100U, 1U, true, 42U);
    CHECK(ams_current_window_rotate(&acc, 40U, &window));
    CHECK(window.uncertainty_mA == AMS_CURRENT_UNCERTAINTY_UNKNOWN);

    sample(&acc, 35U, 100.0f, 500U, 2U, true, 42U);
    CHECK(acc.last_sample_tick == 30U);
    CHECK(!ams_current_window_rotate(&acc, 50U, &window));

    return true;
}

static bool test_tick_wrap_and_gap_rejection(void)
{
    ams_current_window_accumulator_t acc;
    ams_current_window_t window;

    ams_current_window_init(&acc, UINT32_MAX - 20U);
    sample(&acc, UINT32_MAX - 10U, 4.0f, 100U, 1U, true, 42U);
    sample(&acc, 5U, 4.0f, 100U, 1U, true, 42U);

    CHECK(ams_current_window_rotate(&acc, 20U, &window));
    NEAR(window.charge_As, 0.164, 0.00001);

    sample(&acc, 150U, 4.0f, 100U, 1U, true, 42U);
    CHECK(!ams_current_window_rotate(&acc, 160U, &window));

    return true;
}

static bool test_rotation_cannot_hide_real_sample_gap(void)
{
    ams_current_window_accumulator_t acc;
    ams_current_window_t window;

    ams_current_window_init(&acc, 0U);
    sample(&acc, 10U, 10.0f, 100U, 1U, true, 42U);
    CHECK(ams_current_window_rotate(&acc, 90U, &window));

    sample(&acc, 150U, 10.0f, 100U, 1U, true, 42U);
    CHECK(!ams_current_window_rotate(&acc, 160U, &window));

    return true;
}

static bool test_stale_rotation_cannot_charge_next_window(void)
{
    ams_current_window_accumulator_t acc;
    ams_current_window_t window;

    ams_current_window_init(&acc, 50U);
    sample(&acc, 60U, 10.0f, 100U, 1U, true, 42U);
    CHECK(ams_current_window_rotate(&acc, 150U, &window));

    CHECK(!ams_current_window_rotate(&acc, 170U, &window));
    CHECK(!acc.last_sample_valid);

    sample(&acc, 180U, 10.0f, 100U, 1U, true, 42U);
    CHECK(ams_current_window_rotate(&acc, 190U, &window));
    NEAR(window.charge_As, 0.2, 0.00001);
    NEAR(window.average_A, 10.0, 0.00001);

    return true;
}

static bool test_calibration_provenance_carry_and_recovery(void)
{
    ams_current_window_accumulator_t acc;
    ams_current_window_t window;

    ams_current_window_init(&acc, 0U);
    sample(&acc, 10U, 10.0f, 100U, 1U, true, 42U);
    CHECK(ams_current_window_rotate(&acc, 20U, &window));
    CHECK(window.calibration_record_confident);
    CHECK(window.calibration_id == 42U);

    sample(&acc, 30U, 10.0f, 100U, 1U, true, 42U);
    CHECK(ams_current_window_rotate(&acc, 40U, &window));
    CHECK(window.calibration_record_confident);
    CHECK(window.calibration_id == 42U);

    /* The carried 42 record plus a new 43 record makes this epoch mixed. */
    sample(&acc, 50U, 10.0f, 100U, 1U, true, 43U);
    CHECK(ams_current_window_rotate(&acc, 60U, &window));
    CHECK(!window.calibration_record_confident);
    CHECK(window.calibration_id == 0U);

    /* The next epoch carries the latest real sample (43) and can recover. */
    sample(&acc, 70U, 10.0f, 100U, 1U, true, 43U);
    CHECK(ams_current_window_rotate(&acc, 80U, &window));
    CHECK(window.calibration_record_confident);
    CHECK(window.calibration_id == 43U);

    return true;
}

static bool test_invalid_values_and_filtered_fallback(void)
{
    ams_current_window_accumulator_t acc;
    ams_current_window_t window;

    ams_current_window_init(&acc, 0U);
    ams_current_window_set_sensor_metadata(&acc, 100U, 1U);
    ams_current_window_update(&acc,
                              10U,
                              10.0f,
                              NAN,
                              true,
                              true,
                              42U);
    CHECK(acc.last_sample_valid);
    CHECK(acc.active.filtered_A == 10.0f);

    ams_current_window_set_sensor_metadata(&acc, 100U, 1U);
    ams_current_window_update(&acc,
                              20U,
                              INFINITY,
                              0.0f,
                              true,
                              true,
                              42U);
    CHECK(!acc.last_sample_valid);
    CHECK(acc.active.invalid_sample_count == 1U);
    CHECK(!ams_current_window_rotate(&acc, 30U, &window));

    ams_current_window_init(&acc, 0U);
    ams_current_window_set_sensor_metadata(&acc, 100U, 1U);
    ams_current_window_update(&acc,
                              10U,
                              1500.01f,
                              0.0f,
                              true,
                              true,
                              42U);
    CHECK(!acc.last_sample_valid);
    CHECK(acc.active.invalid_sample_count == 1U);

    return true;
}

static bool test_sequence_wrap_skips_zero(void)
{
    ams_current_window_accumulator_t acc;
    ams_current_window_t window;

    ams_current_window_init(&acc, 0U);
    acc.next_sequence = UINT32_MAX;
    sample(&acc, 10U, 5.0f, 100U, 1U, true, 42U);
    CHECK(ams_current_window_rotate(&acc, 20U, &window));
    CHECK(window.sequence == 1U);

    return true;
}

int main(void)
{
    struct test_case
    {
        const char *name;
        bool (*run)(void);
    } tests[] = {
        {"late boundary rejection and clean recovery",
         test_late_boundary_rejection_and_recovery},
        {"carried uncertainty/extrema/sticky mixed range",
         test_carried_metadata_extrema_and_mixed_range},
        {"unknown uncertainty and late sample rejection",
         test_unknown_uncertainty_and_late_sample},
        {"tick wrap and excessive gap rejection",
         test_tick_wrap_and_gap_rejection},
        {"rotation cannot hide a real-sample gap",
         test_rotation_cannot_hide_real_sample_gap},
        {"stale tail cannot charge next window",
         test_stale_rotation_cannot_charge_next_window},
        {"calibration provenance carry/recovery",
         test_calibration_provenance_carry_and_recovery},
        {"nonfinite/range validation and filtered fallback",
         test_invalid_values_and_filtered_fallback},
        {"sequence wrap skips zero",
         test_sequence_wrap_skips_zero},
    };

    const size_t count = sizeof(tests) / sizeof(tests[0]);

    for(size_t i = 0U; i < count; ++i)
    {
        if(!tests[i].run())
        {
            return 1;
        }
        printf("PASS: %s\n", tests[i].name);
    }

    printf("PASS: %zu current-window regressions\n", count);
    return 0;
}
