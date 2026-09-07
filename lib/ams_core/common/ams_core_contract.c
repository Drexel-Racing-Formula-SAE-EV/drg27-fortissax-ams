#include <stddef.h>
#include <stdint.h>

#include <ams_core/ams_core_config.h>
#include <ams_core/ams_core_contract.h>
#include <ams_core/ams_core_time.h>
#include <ams_core/ams_current_window.h>
#include <ams_core/ams_current_sensor.h>
#include <ams_core/ams_current_fault.h>
#include <ams_core/ams_measurement.h>
#include <ams_core/ams_estimator_lut.h>
#include <ams_core/ams_soc_ekf.h>
#include <ams_core/ams_soh.h>
#include <ams_core/ams_sop.h>
#include <ams_core/ams_fuse_observer.h>
#include <ams_core/ams_core_types.h>
#include <ams_core/ams_fan_control.h>
#include <ams_core/ams_imd.h>

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

    /*
     * Z-008 link/runtime smoke for the frozen current-window producer.
     * The deeper parity corpus remains host-side; this keeps the target build
     * from garbage-collecting the portable producer implementation.
     */
    ams_current_window_accumulator_t current_acc;
    ams_current_window_t current_window;

    ams_current_window_init(&current_acc, 0U);
    ams_current_window_set_sensor_metadata(&current_acc, 100U, 1U);
    ams_current_window_update(&current_acc,
                              10U,
                              10.0f,
                              10.0f,
                              true,
                              true,
                              42U);

    if (!ams_current_window_rotate(&current_acc, 20U, &current_window)) {
        return -11;
    }

    if ((current_window.sequence != 1U) ||
        (current_window.uncertainty_mA != 100U) ||
        (current_window.selected_range != 1U) ||
        !current_window.calibration_record_confident ||
        (current_window.calibration_id != 42U) ||
        (current_window.charge_As < 0.19999) ||
        (current_window.charge_As > 0.20001)) {
        return -12;
    }

    /*
     * Z-009 target-link smoke for the exact v2.6.27 estimator core.
     *
     * Do not execute a full EKF update from the startup contract: the normal
     * estimator thread owns that stack budget later. Volatile function-pointer
     * anchors keep the init/update entry points in the target link while the
     * lightweight checks below verify the topology config and LUT objects.
     */
    typedef void (*ams_ekf_init_fn_t)(ams_ekf_instance_t *,
                                      const ams_ekf_config_t *);
    typedef bool (*ams_ekf_step_gated_fn_t)(
        ams_ekf_instance_t *,
        float,
        float,
        float,
        float,
        bool,
        ams_ekf_r0_update_result_t *);

    volatile ams_ekf_init_fn_t estimator_init_anchor = ams_ekf_init;
    volatile ams_ekf_step_gated_fn_t estimator_step_anchor =
        ams_ekf_step_gated;
    ams_ekf_config_t estimator_cfg;

    if ((estimator_init_anchor == NULL) ||
        (estimator_step_anchor == NULL)) {
        return -13;
    }

    ams_ekf_make_pack_config(&estimator_cfg);

    if ((estimator_cfg.enabled == 0U) ||
        (estimator_cfg.first_series_group != 0U) ||
        (estimator_cfg.series_group_count != AMS_EKF_PACK_SERIES_GROUPS) ||
        (estimator_cfg.parallel_cell_count != AMS_EKF_PACK_PARALLEL_CELLS) ||
        (estimator_cfg.cell_capacity_Ah != AMS_EKF_CELL_CAPACITY_AH)) {
        return -14;
    }

    if ((ams_p42a_ocv_v(0.5f, 25.0f) < 3.7530f) ||
        (ams_p42a_ocv_v(0.5f, 25.0f) > 3.7532f) ||
        (ams_p42a_r0_ohm(0.5f, 25.0f) < 0.0135f) ||
        (ams_p42a_r0_ohm(0.5f, 25.0f) > 0.0136f)) {
        return -15;
    }


    /*
     * Z-010 target-link/configuration smoke.
     *
     * Full SoP, SoH, and fuse trajectories remain host-side. These lightweight
     * checks keep the exact v2.6.27 entry points and defaults in the target
     * image without moving algorithm execution into startup.
     */
    typedef ams_sop_status_t (*ams_sop_solve_fn_t)(
        const ams_sop_input_t *,
        const ams_sop_config_t *,
        ams_sop_result_t *);
    typedef bool (*ams_soh_update_fn_t)(
        ams_soh_estimator_t *,
        const ams_soh_config_t *,
        const ams_soh_input_t *);
    typedef bool (*ams_fuse_update_fn_t)(
        ams_fuse_observer_t *,
        const ams_fuse_observer_config_t *,
        const ams_sop_config_t *,
        const ams_fuse_observer_input_t *,
        ams_fuse_observer_result_t *);

    volatile ams_sop_solve_fn_t sop_solve_anchor = ams_sop_solve;
    volatile ams_soh_update_fn_t soh_update_anchor = ams_soh_update;
    volatile ams_fuse_update_fn_t fuse_update_anchor =
        ams_fuse_observer_update;

    ams_sop_config_t sop_cfg;
    ams_soh_config_t soh_cfg;
    ams_fuse_observer_config_t fuse_cfg;

    if ((sop_solve_anchor == NULL) ||
        (soh_update_anchor == NULL) ||
        (fuse_update_anchor == NULL)) {
        return -16;
    }

    ams_sop_default_config(&sop_cfg);
    ams_soh_default_config(&soh_cfg);
    ams_fuse_observer_default_config(&fuse_cfg);

    if (!ams_sop_config_valid(&sop_cfg) ||
        !ams_soh_config_valid(&soh_cfg) ||
        !ams_fuse_observer_config_valid(&fuse_cfg)) {
        return -17;
    }

    if ((AMS_SOP_SEGMENTS != 5U) ||
        (AMS_SOP_TOTAL_CELLS != 75U) ||
        (AMS_SOP_HORIZONS != 4U) ||
        (AMS_SOH_SEGMENTS != 5U) ||
        (AMS_FUSE_EAC14_80_RATED_CURRENT_A != 80.0f)) {
        return -18;
    }

    if ((sop_cfg.parallel_cells != 6.0f) ||
        (soh_cfg.nominal_pack_capacity_ah != 25.2f) ||
        (fuse_cfg.rated_current_a != 80.0f)) {
        return -19;
    }

    /*
     * Z-011 portable DHAB/current-fault smoke. Hardware ADC acquisition is
     * intentionally not started here; the adapter owns that platform path.
     */
    current_sensor_t current_sensor;
    current_fault_state_t current_fault;

    current_sensor_init(&current_sensor);
    current_sensor_adc_begin(&current_sensor);
    current_sensor_adc_publish_high(&current_sensor, 1861U);
    current_sensor_adc_publish_low(&current_sensor, 1861U);

    if (!current_sensor_adc_finish(&current_sensor)) {
        return -20;
    }

    (void)current_sensor_convert(&current_sensor);
    if (!current_sensor.current_valid ||
        (current_sensor.selected_range != CURRENT_SENSOR_RANGE_50A) ||
        (current_sensor.reason != CURRENT_SENSOR_REASON_OK) ||
        (sizeof(current_sensor_calibration_record_t) !=
         CURRENT_SENSOR_CALIBRATION_RECORD_SIZE)) {
        return -21;
    }

    current_fault_init(&current_fault);
    current_fault_update(&current_fault,
                         CURRENT_FAULT_MODE_IDLE,
                         current_sensor.current,
                         current_sensor.current_valid,
                         current_sensor.reason,
                         20U);

    if (current_fault.sensor_fault ||
        current_fault.confirmed ||
        current_fault.latched) {
        return -22;
    }

    /* Z-012 portable fan-policy smoke. Missing temperature evidence must fail
     * conservative to maximum cooling exactly as v2.6.27. */
    ams_fan_control_input_t fan_input = {0};
    uint8_t fan_reason = AMS_FAN_CONTROL_REASON_OFF_COOL;
    float fan_percent;

    fan_input.temp_valid = false;
    fan_input.temp_read_fault = true;
    fan_input.temp_usable_sensor_count = 0U;
    fan_percent = ams_fan_percent_from_temp(&fan_input, &fan_reason);

    if ((fan_percent != 100.0f) ||
        (fan_reason != AMS_FAN_CONTROL_REASON_TEMP_INVALID)) {
        return -23;
    }

    /* Z-013 portable IMD smoke: one coherent 10 Hz / 50% tuple with OK_HS
     * high must decode to NORMAL and remain fresh at the exact 250 ms edge. */
    ams_imd_t imd;

    ams_imd_init(&imd, 108000000U, true);
    ams_imd_capture_publish(&imd, 5400000U, 10800000U, 100U);

    if ((ams_imd_read_at(&imd, true, true, 350U) != 0) ||
        !ams_imd_is_ok(&imd) ||
        (imd.status != AMS_IMD_NORMAL) ||
        (imd.duty_percent != 50.0f) ||
        (imd.frequency_hz != 10.0f)) {
        return -24;
    }

    return 0;
}