#include <ams_core/ams_estimator_lut.h>
#include <ams_core/ams_soc_ekf.h>

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static unsigned int checks_run;
static unsigned int checks_failed;

#define CHECK(condition)                                                      \
    do {                                                                      \
        checks_run++;                                                         \
        if (!(condition)) {                                                   \
            checks_failed++;                                                  \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__,         \
                    #condition);                                               \
        }                                                                     \
    } while (0)

static bool closef_local(float a, float b, float tol)
{
    return isfinite(a) && isfinite(b) && (fabsf(a - b) <= tol);
}

static void test_lut_golden_points(void)
{
    CHECK(closef_local(ams_p42a_ocv_v(0.0f, 25.0f), 2.5f, 1.0e-6f));
    CHECK(closef_local(ams_p42a_ocv_v(1.0f, 25.0f), 4.2f, 1.0e-6f));
    CHECK(closef_local(ams_p42a_ocv_v(0.5f, 25.0f),
                       3.7530849f, 1.0e-6f));

    CHECK(closef_local(ams_p42a_r0_ohm(0.5f, 25.0f),
                       0.0135501334f, 1.0e-8f));
    CHECK(closef_local(ams_p42a_inv_c1(0.5f, 25.0f),
                       0.000403347425f, 1.0e-10f));
    CHECK(closef_local(ams_p42a_neg_inv_tau1(0.5f, 25.0f),
                       -0.0944410637f, 1.0e-8f));

    CHECK(ams_p42a_ocv_v(-1.0f, 25.0f) ==
          ams_p42a_ocv_v(0.0f, 25.0f));
    CHECK(ams_p42a_ocv_v(2.0f, 25.0f) ==
          ams_p42a_ocv_v(1.0f, 25.0f));
}

static void test_config_topology(void)
{
    ams_ekf_config_t cfg;

    memset(&cfg, 0, sizeof(cfg));
    ams_ekf_make_pack_config(&cfg);

    CHECK(cfg.enabled == 1U);
    CHECK(cfg.first_series_group == 0U);
    CHECK(cfg.series_group_count == 75U);
    CHECK(closef_local(cfg.parallel_cell_count, 6.0f, 0.0f));
    CHECK(closef_local(cfg.cell_capacity_Ah, 4.2f, 0.0f));
    CHECK(closef_local(cfg.sample_time_s, 0.1f, 0.0f));
    CHECK(closef_local(cfg.soc_init, 1.0f, 0.0f));
    CHECK(closef_local(cfg.r0_init_ohm, 0.0147f, 1.0e-8f));

    ams_ekf_make_segment_config(&cfg, 3U);
    CHECK(cfg.first_series_group == 45U);
    CHECK(cfg.series_group_count == 15U);

    ams_ekf_make_group_range_config(&cfg, 7U, 8U);
    CHECK(cfg.first_series_group == 7U);
    CHECK(cfg.series_group_count == 8U);
}

static void test_instance_init_and_step(void)
{
    ams_ekf_config_t cfg;
    ams_ekf_instance_t ekf;
    ams_ekf_r0_update_result_t r0_result =
        AMS_EKF_R0_UPDATE_REJECT_NUMERIC;

    ams_ekf_make_pack_config(&cfg);
    ams_ekf_init(&ekf, &cfg);

    CHECK(ekf.fault_flags == AMS_EKF_FAULT_NONE);
    CHECK(ekf.valid == 0U);
    CHECK(closef_local(ekf.soc, 1.0f, 0.0f));
    CHECK(closef_local(ekf.r0_ohm, 0.0147f, 1.0e-8f));
    CHECK(closef_local(ekf.t_core_C, 25.0f, 0.0f));
    CHECK(closef_local(ekf.r_meas_V2, 0.25f, 1.0e-7f));

    CHECK(ams_ekf_step_gated(&ekf,
                             10.0f,
                             278.0f,
                             25.0f,
                             0.1f,
                             false,
                             &r0_result));

    CHECK(r0_result == AMS_EKF_R0_UPDATE_NOT_REQUESTED);
    CHECK(ekf.valid == 1U);
    CHECK(ekf.fault_flags == AMS_EKF_FAULT_NONE);
    CHECK(ekf.step_count == 1U);
    CHECK(isfinite(ekf.soc));
    CHECK(isfinite(ekf.vp1_V));
    CHECK(isfinite(ekf.vp2_V));
    CHECK(isfinite(ekf.v_pred_V));
    CHECK(isfinite(ekf.innovation_V));
    CHECK(isfinite(ekf.innovation_variance_V2));
}

static void test_bad_input_and_dt_clamp(void)
{
    ams_ekf_config_t cfg;
    ams_ekf_instance_t ekf;
    ams_ekf_r0_update_result_t result;

    ams_ekf_make_pack_config(&cfg);
    ams_ekf_init(&ekf, &cfg);

    CHECK(!ams_ekf_step_gated(&ekf,
                              NAN,
                              300.0f,
                              25.0f,
                              0.1f,
                              false,
                              &result));
    CHECK((ekf.fault_flags & AMS_EKF_FAULT_BAD_INPUT) != 0U);

    ams_ekf_init(&ekf, &cfg);
    CHECK(ams_ekf_step_gated(&ekf,
                             0.0f,
                             300.0f,
                             25.0f,
                             NAN,
                             false,
                             &result));
    CHECK((ekf.fault_flags & AMS_EKF_FAULT_DT_CLAMPED) != 0U);
    CHECK(ekf.dt_clamp_count == 1U);
}

static void test_acquisition_state_machine_entry(void)
{
    ams_ekf_config_t cfg;
    ams_ekf_instance_t ekf;

    ams_ekf_make_pack_config(&cfg);
    ams_ekf_init(&ekf, &cfg);

    CHECK(ekf.acquisition.state == AMS_EKF_ACQ_WAITING);
    CHECK(ekf.acquisition.sample_count == 0U);

    ams_ekf_acquisition_observe(&ekf,
                                0.0f,
                                0.10f,
                                280.0f,
                                25.0f,
                                0U,
                                true,
                                true);

    CHECK(ekf.acquisition.state == AMS_EKF_ACQ_COLLECTING);
    CHECK(ekf.acquisition.reason ==
          AMS_EKF_ACQ_REASON_COLLECTING_RELAXATION);
    CHECK(ekf.acquisition.sample_count == 1U);

    ams_ekf_acquisition_observe(&ekf,
                                2.0f,
                                0.10f,
                                280.0f,
                                25.0f,
                                1000U,
                                true,
                                true);

    CHECK(ekf.acquisition.state == AMS_EKF_ACQ_WAITING);
    CHECK(ekf.acquisition.reason ==
          AMS_EKF_ACQ_REASON_RELAXATION_INTERRUPTED_RETRY);
}

static void test_estimator_topology_and_cc(void)
{
    ams_estimator_t est;

    memset(&est, 0, sizeof(est));
    ams_estimator_init_default(&est);

    CHECK(est.enabled == 1U);
    CHECK(est.instance_count == 1U);
    CHECK(est.inst[0].cfg.series_group_count == 75U);
    CHECK(est.cc_valid == 1U);
    CHECK(closef_local(est.cc_soc, 1.0f, 0.0f));

    CHECK(ams_estimator_cc_step(&est, 25.2f, 1.0f));
    CHECK(est.cc_step_count == 1U);
    CHECK(closef_local(est.cc_soc,
                       1.0f - (25.2f / (6.0f * 3600.0f * 4.2f)),
                       1.0e-7f));

    CHECK(ams_estimator_configure_segments(&est));
    CHECK(est.instance_count == 5U);
    for (uint8_t i = 0U; i < 5U; i++) {
        CHECK(est.inst[i].cfg.first_series_group ==
              (uint16_t)(15U * i));
        CHECK(est.inst[i].cfg.series_group_count == 15U);
    }

    CHECK(ams_estimator_configure_even_split(&est, 7U));
    CHECK(est.instance_count == 7U);

    uint16_t total = 0U;
    for (uint8_t i = 0U; i < est.instance_count; i++) {
        CHECK(est.inst[i].cfg.first_series_group == total);
        total = (uint16_t)(total + est.inst[i].cfg.series_group_count);
    }
    CHECK(total == 75U);
}

static void test_summary_and_status(void)
{
    ams_estimator_t est;

    memset(&est, 0, sizeof(est));
    CHECK(ams_estimator_configure_pack(&est));

    est.inst[0].valid = 1U;
    est.inst[0].soc = 0.50f;
    est.inst[0].r0_ohm = 0.015f;
    est.inst[0].t_core_C = 26.0f;
    est.inst[0].v_pred_V = 280.0f;
    est.inst[0].innovation_V = 0.5f;

    ams_estimator_refresh_summary(&est,
                                  AMS_ESTIMATOR_INPUT_HARDWARE,
                                  1234U);

    CHECK(closef_local(est.pack_soc, 0.50f, 1.0e-7f));
    CHECK(closef_local(est.representative_cell_r0_ohm,
                       0.015f, 1.0e-8f));
    CHECK(closef_local(est.estimated_pack_r0_ohm,
                       0.015f * (75.0f / 6.0f), 1.0e-6f));
    CHECK(closef_local(est.pack_t_core_C, 26.0f, 1.0e-7f));
    CHECK(est.input_source == AMS_ESTIMATOR_INPUT_HARDWARE);
    CHECK((ams_estimator_status_flags(&est) & AMS_EKF_FLAG_VALID) != 0U);
}

static void test_resistance_gate_basics(void)
{
    ams_ekf_config_t cfg;
    ams_ekf_instance_t ekf;

    ams_ekf_make_pack_config(&cfg);
    ams_ekf_init(&ekf, &cfg);

    uint32_t reject = ams_resistance_soh_gate(&ekf,
                                              0.0f,
                                              true,
                                              true,
                                              true);

    CHECK((reject & AMS_SOH_REJECT_LOW_CURRENT) != 0U);
    CHECK((reject & AMS_SOH_REJECT_LOW_CURRENT_STEP) != 0U);

    reject = ams_resistance_soh_gate(NULL,
                                     30.0f,
                                     true,
                                     true,
                                     true);
    CHECK((reject & AMS_SOH_REJECT_ESTIMATOR) != 0U);
}

int main(void)
{
    test_lut_golden_points();
    test_config_topology();
    test_instance_init_and_step();
    test_bad_input_and_dt_clamp();
    test_acquisition_state_machine_entry();
    test_estimator_topology_and_cc();
    test_summary_and_status();
    test_resistance_gate_basics();

    if (checks_failed != 0U) {
        fprintf(stderr,
                "FAIL: %u/%u estimator checks failed\n",
                checks_failed,
                checks_run);
        return 1;
    }

    printf("PASS: %u v2.6.27 estimator-core checks\n", checks_run);
    return 0;
}
