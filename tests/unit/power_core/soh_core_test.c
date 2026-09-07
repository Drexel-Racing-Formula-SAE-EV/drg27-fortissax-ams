#include <ams_core/ams_soh.h>

#include <assert.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static unsigned int checks_run;
static unsigned int checks_failed;

#define CHECK(expr) do { \
    checks_run++; \
    if(!(expr)) { \
        checks_failed++; \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        return false; \
    } \
} while(0)

static ams_soh_input_t nominal_input(void)
{
    ams_soh_input_t input;

    memset(&input, 0, sizeof(input));
    input.measurement_sequence = 1u;
    input.measurement_timestamp_ms = 1u;
    input.now_ms = 1u;
    input.elapsed_s = 1.0f;
    input.pack_current_uncertainty_a = 0.05f;
    input.pack_soc = 0.90f;
    input.average_cell_temp_c = 25.0f;
    input.cell_voltage_spread_v = 0.020f;
    input.maximum_soc_sigma = 0.005f;
    input.maximum_abs_innovation_v_per_cell = 0.005f;
    input.maximum_abs_polarization_v = 0.005f;
    input.measurement_valid = 1u;
    input.estimator_valid = 1u;
    input.current_calibrated = 1u;
    input.current_polarity_validated = 1u;
    input.balance_recovered = 1u;

    for(uint8_t segment = 0u; segment < AMS_SOH_SEGMENTS; segment++)
    {
        input.segment_resistance_growth_ratio[segment] = 1.05f;
        input.segment_resistance_confidence_pct[segment] = 80u;
        input.segment_resistance_valid[segment] = 1u;
    }

    return input;
}

static bool advance(ams_soh_estimator_t *estimator,
                    const ams_soh_config_t *cfg,
                    ams_soh_input_t *input,
                    uint32_t seconds)
{
    for(uint32_t n = 0u; n < seconds; n++)
    {
        input->now_ms += 1000u;
        input->measurement_timestamp_ms = input->now_ms;
        input->measurement_sequence++;
        CHECK(ams_soh_update(estimator, cfg, input));
    }

    return true;
}

static bool close_resistance_episode(ams_soh_estimator_t *estimator,
                                     const ams_soh_config_t *cfg,
                                     ams_soh_input_t *input)
{
    uint8_t valid[AMS_SOH_SEGMENTS];

    memcpy(valid, input->segment_resistance_valid, sizeof(valid));
    memset(input->segment_resistance_valid, 0,
           sizeof(input->segment_resistance_valid));

    const uint32_t gap_s =
        (AMS_SOH_RESISTANCE_EPISODE_GAP_MS + 999u) / 1000u;

    CHECK(advance(estimator, cfg, input, gap_s));

    memcpy(input->segment_resistance_valid, valid, sizeof(valid));
    return true;
}

static bool test_defaults_and_rejection(void)
{
    ams_soh_config_t cfg;
    ams_soh_estimator_t estimator;
    ams_soh_input_t input;

    ams_soh_default_config(&cfg);
    CHECK(ams_soh_config_valid(&cfg));
    CHECK(fabsf(cfg.nominal_pack_capacity_ah - 25.2f) < 1.0e-6f);
    CHECK(cfg.maximum_measurement_age_ms == 250u);
    CHECK(cfg.maximum_temperature_age_ms == 1000u);
    CHECK(cfg.minimum_capacity_observations == 2u);

    ams_soh_init(&estimator, &cfg);
    input = nominal_input();
    input.current_calibrated = 0u;

    CHECK(!ams_soh_update(&estimator, &cfg, &input));
    CHECK((estimator.result.last_reason_flags &
           AMS_SOH_REASON_CURRENT_CALIBRATION) != 0u);
    CHECK(estimator.result.capacity_soh_lower ==
          cfg.prior_capacity_soh_lower);

    input = nominal_input();
    input.max_cell_age_ms = cfg.maximum_measurement_age_ms + 1u;
    CHECK(!ams_soh_update(&estimator, &cfg, &input));
    CHECK((estimator.result.last_reason_flags &
           AMS_SOH_REASON_STALE) != 0u);

    return true;
}

static bool test_capacity_observability(void)
{
    ams_soh_config_t cfg;
    ams_soh_estimator_t estimator;
    ams_soh_input_t input;

    ams_soh_default_config(&cfg);
    ams_soh_init(&estimator, &cfg);
    input = nominal_input();

    CHECK(advance(&estimator, &cfg, &input, 60u));
    CHECK(estimator.anchor_valid != 0u);
    CHECK(estimator.result.accepted_capacity_windows == 0u);

    input.pack_current_a = 20.0f;
    input.pack_soc = 0.70f;
    input.total_charge_as = 5.04 * 3600.0;
    input.now_ms += 1000u;
    input.measurement_timestamp_ms = input.now_ms;
    input.measurement_sequence++;
    CHECK(ams_soh_update(&estimator, &cfg, &input));

    input.pack_current_a = 0.0f;
    CHECK(advance(&estimator, &cfg, &input, 60u));
    CHECK(estimator.result.accepted_capacity_windows == 1u);
    CHECK(fabsf(estimator.result.capacity_ah - 25.2f) < 0.05f);
    CHECK(estimator.result.capacity_valid == 0u);

    input.pack_current_a = -20.0f;
    input.pack_soc = 0.90f;
    input.total_charge_as = 0.0;
    input.now_ms += 1000u;
    input.measurement_timestamp_ms = input.now_ms;
    input.measurement_sequence++;
    CHECK(ams_soh_update(&estimator, &cfg, &input));

    input.pack_current_a = 0.0f;
    CHECK(advance(&estimator, &cfg, &input, 60u));
    CHECK(estimator.result.accepted_capacity_windows == 2u);
    CHECK(estimator.result.capacity_valid != 0u);
    CHECK(estimator.result.capacity_confidence_pct == 50u);
    CHECK(estimator.result.capacity_soh_lower > 0.90f);

    return true;
}

static bool test_persistence_crc_and_generation(void)
{
    ams_soh_config_t cfg;
    ams_soh_estimator_t estimator;
    ams_soh_estimator_t restored;
    ams_soh_persist_record_t older;
    ams_soh_persist_record_t newer;
    ams_soh_persist_record_t selected;

    ams_soh_default_config(&cfg);
    ams_soh_init(&estimator, &cfg);

    estimator.result.accepted_capacity_windows = 2u;
    estimator.capacity_mean_ah = 24.0;
    estimator.capacity_m2_ah2 = 0.1;
    estimator.result.capacity_valid = 1u;
    estimator.segment_resistance_valid_mask = AMS_SOH_ALL_SEGMENTS_MASK;

    for(uint8_t segment = 0u; segment < AMS_SOH_SEGMENTS; segment++)
    {
        estimator.segment_resistance_growth_ratio[segment] = 1.10f;
        estimator.segment_resistance_growth_upper[segment] = 1.20f;
        estimator.segment_resistance_confidence_pct[segment] = 80u;
    }

    CHECK(sizeof(ams_soh_persist_record_t) == 64u);
    CHECK(ams_soh_export_record(&estimator, 7u, &older));
    CHECK(older.magic == AMS_SOH_PERSIST_MAGIC);
    CHECK(older.schema == AMS_SOH_PERSIST_SCHEMA);
    CHECK(older.crc32 == ams_soh_record_crc32(&older));
    CHECK(ams_soh_import_record(&restored, &cfg, &older));
    CHECK(restored.result.persistence_valid != 0u);

    CHECK(ams_soh_export_record(&estimator, 8u, &newer));
    CHECK(ams_soh_select_newest_record(&cfg, &older, &newer, &selected));
    CHECK(selected.generation == 8u);

    older.capacity_mean_ah += 1.0;
    memset(&restored, 0xA5, sizeof(restored));
    CHECK(!ams_soh_import_record(&restored, &cfg, &older));
    CHECK(restored.result.last_reason_flags == AMS_SOH_REASON_PERSISTENCE);
    CHECK(ams_soh_select_newest_record(&cfg, &older, &newer, &selected));
    CHECK(selected.generation == 8u);

    return true;
}

static bool test_resistance_episode_filtering(void)
{
    ams_soh_config_t cfg;
    ams_soh_estimator_t estimator;
    ams_soh_input_t input;

    ams_soh_default_config(&cfg);
    ams_soh_init(&estimator, &cfg);
    input = nominal_input();

    for(uint8_t n = 0u;
        n < AMS_SOH_RESISTANCE_EPISODE_MIN_OBSERVATIONS;
        n++)
    {
        const float ratio = (n == 4u) ? 1.60f : 1.02f;

        for(uint8_t segment = 0u; segment < AMS_SOH_SEGMENTS; segment++)
        {
            input.segment_resistance_growth_ratio[segment] = ratio;
            input.segment_resistance_confidence_pct[segment] = 100u;
            input.segment_resistance_valid[segment] = 1u;
        }

        input.now_ms += 100u;
        input.measurement_timestamp_ms = input.now_ms;
        input.measurement_sequence++;
        CHECK(ams_soh_update(&estimator, &cfg, &input));
    }

    CHECK(estimator.result.resistance_valid == 0u);
    CHECK(close_resistance_episode(&estimator, &cfg, &input));
    CHECK(estimator.result.resistance_valid != 0u);
    CHECK(estimator.result.resistance_growth_ratio < 1.05f);
    CHECK(estimator.result.resistance_growth_upper < 1.10f);

    return true;
}

int main(void)
{
    bool ok = true;

    ok = test_defaults_and_rejection() && ok;
    ok = test_capacity_observability() && ok;
    ok = test_persistence_crc_and_generation() && ok;
    ok = test_resistance_episode_filtering() && ok;

    if(!ok || (checks_failed != 0u))
    {
        fprintf(stderr,
                "Z-010 SoH tests: FAIL (%u checks, %u failures)\n",
                checks_run,
                checks_failed);
        return 1;
    }

    printf("Z-010 SoH tests: PASS (%u checks)\n", checks_run);
    return 0;
}
