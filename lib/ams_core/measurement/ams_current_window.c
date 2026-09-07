#include <ams_core/ams_current_window.h>

#include <math.h>
#include <string.h>

static uint32_t saturating_increment_u32(uint32_t value)
{
    return (value == UINT32_MAX) ? UINT32_MAX : (value + 1U);
}

static ams_sequence_t sequence_increment(ams_sequence_t value)
{
    value++;
    return (value == 0U) ? 1U : value;
}

static void current_record_invalid(ams_current_window_accumulator_t *acc)
{
    if(acc == NULL)
    {
        return;
    }

    acc->active.invalid_sample_count =
        saturating_increment_u32(acc->active.invalid_sample_count);
    acc->total_invalid_sample_count =
        saturating_increment_u32(acc->total_invalid_sample_count);
}

static bool current_value_valid(float current_A)
{
    return isfinite(current_A) && (fabsf(current_A) <= 1500.0f);
}

static void current_merge_calibration_provenance(
    ams_current_window_accumulator_t *acc,
    bool calibration_record_confident,
    uint32_t calibration_id)
{
    if(acc == NULL)
    {
        return;
    }

    calibration_record_confident = calibration_record_confident &&
                                    (calibration_id != 0U);
    if(!calibration_record_confident)
    {
        calibration_id = 0U;
    }

    if(!acc->active_calibration_provenance_initialized)
    {
        acc->active.calibration_record_confident =
            calibration_record_confident;
        acc->active.calibration_id = calibration_id;
        acc->active_calibration_provenance_initialized = true;
        return;
    }

    if(!acc->active.calibration_record_confident ||
       !calibration_record_confident ||
       (acc->active.calibration_id != calibration_id))
    {
        /*
         * A voltage epoch spanning uncalibrated current or two calibration
         * records remains valid for current integration, but cannot be used
         * as a calibrated resistance/SoH observation.
         */
        acc->active.calibration_record_confident = false;
        acc->active.calibration_id = 0U;
    }
}

static void current_integrate(ams_current_window_accumulator_t *acc,
                              float from_A,
                              float to_A,
                              uint32_t dt_ms)
{
    if((acc == NULL) || (dt_ms == 0U))
    {
        return;
    }

    double dt_s = (double)dt_ms / 1000.0;
    double from = (double)from_A;
    double to = (double)to_A;
    double delta_charge = 0.5 * (from + to) * dt_s;
    double delta_abs_charge = 0.5 * (fabs(from) + fabs(to)) * dt_s;
    double delta_squared = 0.5 * ((from * from) + (to * to)) * dt_s;

    acc->active.charge_As += delta_charge;
    acc->active.absolute_charge_As += delta_abs_charge;
    acc->active_current_squared_A2s += delta_squared;
    acc->total_charge_As += delta_charge;
    acc->total_absolute_charge_As += delta_abs_charge;
}

void ams_current_window_init(ams_current_window_accumulator_t *acc,
                             ams_time_ms_t now)
{
    if(acc == NULL)
    {
        return;
    }

    memset(acc, 0, sizeof(*acc));
    acc->active.start_tick = now;
    acc->active.end_tick = now;
    acc->last_uncertainty_mA = AMS_CURRENT_UNCERTAINTY_UNKNOWN;
    acc->sample_uncertainty_mA = AMS_CURRENT_UNCERTAINTY_UNKNOWN;
    acc->initialized = true;
}

void ams_current_window_update(ams_current_window_accumulator_t *acc,
                               ams_time_ms_t now,
                               float current_A,
                               float filtered_A,
                               bool valid,
                               bool calibration_record_confident,
                               uint32_t calibration_id)
{
    if(acc == NULL)
    {
        return;
    }
    if(!acc->initialized)
    {
        ams_current_window_init(acc, now);
    }

    /*
     * Timestamps share one externally serialized timeline. A late caller must
     * not rewind it or subsequently expose a partially integrated window as
     * valid. Unsigned subtraction accepts ordinary uint32_t tick wrap.
     */
    if((uint32_t)(now - acc->active.end_tick) > INT32_MAX)
    {
        current_record_invalid(acc);
        return;
    }

    valid = valid && current_value_valid(current_A);
    if(!valid)
    {
        current_record_invalid(acc);
        acc->active.end_tick = now;
        acc->last_sample_valid = false;
        return;
    }

    if(!isfinite(filtered_A))
    {
        filtered_A = current_A;
    }

    current_merge_calibration_provenance(acc,
                                         calibration_record_confident,
                                         calibration_id);

    if(acc->last_sample_valid)
    {
        uint32_t dt_ms = (uint32_t)(now - acc->integration_tick);
        if((dt_ms <= AMS_CURRENT_WINDOW_MAX_INTEGRATION_GAP_MS) &&
           ((uint32_t)(now - acc->last_sample_tick) <=
            AMS_CURRENT_WINDOW_MAX_INTEGRATION_GAP_MS))
        {
            current_integrate(acc, acc->last_current_A, current_A, dt_ms);
        }
        else
        {
            current_record_invalid(acc);
        }
    }
    else if(acc->active.sample_count == 0U)
    {
        /*
         * Cover the interval from the voltage boundary (or initialization) to
         * the first sample with the first observed value. This bounded
         * zero-order hold avoids silently losing charge at every boundary
         * while still rejecting a late first sample.
         */
        uint32_t initial_gap_ms = (uint32_t)(now - acc->active.start_tick);
        if(initial_gap_ms <= AMS_CURRENT_WINDOW_MAX_INTEGRATION_GAP_MS)
        {
            current_integrate(acc, current_A, current_A, initial_gap_ms);
        }
        else
        {
            current_record_invalid(acc);
        }
    }

    if((acc->active.sample_count == 0U) && !acc->last_sample_valid)
    {
        acc->active.min_A = current_A;
        acc->active.max_A = current_A;
    }
    else
    {
        if(current_A < acc->active.min_A)
        {
            acc->active.min_A = current_A;
        }
        if(current_A > acc->active.max_A)
        {
            acc->active.max_A = current_A;
        }
    }

    acc->active.sample_count =
        saturating_increment_u32(acc->active.sample_count);
    acc->active.end_tick = now;
    acc->active.latest_sample_tick = now;
    acc->active.latest_A = current_A;
    acc->active.filtered_A = filtered_A;
    acc->last_sample_tick = now;
    acc->integration_tick = now;
    acc->last_current_A = current_A;
    acc->last_filtered_A = filtered_A;
    acc->last_uncertainty_mA = acc->sample_uncertainty_mA;
    acc->last_selected_range = acc->sample_selected_range;
    acc->last_calibration_record_confident =
        calibration_record_confident && (calibration_id != 0U);
    acc->last_calibration_id = acc->last_calibration_record_confident ?
                               calibration_id : 0U;
    acc->last_sample_valid = true;
}

void ams_current_window_set_sensor_metadata(
    ams_current_window_accumulator_t *acc,
    ams_current_uncertainty_t uncertainty_mA,
    uint8_t selected_range)
{
    if((acc == NULL) || !acc->initialized)
    {
        return;
    }

    if(uncertainty_mA > acc->active.uncertainty_mA)
    {
        acc->active.uncertainty_mA = uncertainty_mA;
    }

    acc->sample_uncertainty_mA = uncertainty_mA;
    acc->sample_selected_range = selected_range;

    if(!acc->active_sensor_metadata_initialized)
    {
        acc->active.selected_range = selected_range;
        acc->active_sensor_metadata_initialized = true;
    }
    else if(acc->active.selected_range != selected_range)
    {
        /* Mixed/unknown is sticky for the remainder of this epoch. */
        acc->active.selected_range = 0U;
    }
}

bool ams_current_window_rotate(ams_current_window_accumulator_t *acc,
                               ams_time_ms_t boundary_tick,
                               ams_current_window_t *completed)
{
    if((acc == NULL) || (completed == NULL))
    {
        return false;
    }
    if(!acc->initialized)
    {
        ams_current_window_init(acc, boundary_tick);
    }

    if((uint32_t)(boundary_tick - acc->active.end_tick) > INT32_MAX)
    {
        /*
         * Reject the old boundary without resetting integration/provenance.
         * The active window stays tainted until a correctly ordered rotation.
         */
        current_record_invalid(acc);
        *completed = acc->active;
        completed->valid = false;
        completed->total_charge_As = acc->total_charge_As;
        completed->total_absolute_charge_As = acc->total_absolute_charge_As;
        completed->total_invalid_sample_count = acc->total_invalid_sample_count;
        return false;
    }

    uint32_t latest_age_ms = UINT32_MAX;
    if(acc->last_sample_valid)
    {
        latest_age_ms = (uint32_t)(boundary_tick - acc->last_sample_tick);
        uint32_t pending_ms =
            (uint32_t)(boundary_tick - acc->integration_tick);

        if((latest_age_ms <= AMS_CURRENT_WINDOW_MAX_SAMPLE_AGE_MS) &&
           (pending_ms <= AMS_CURRENT_WINDOW_MAX_INTEGRATION_GAP_MS))
        {
            current_integrate(acc,
                              acc->last_current_A,
                              acc->last_current_A,
                              pending_ms);
            acc->integration_tick = boundary_tick;
        }
        else
        {
            current_record_invalid(acc);
            /*
             * A stale tail cannot be carried into the next epoch: its
             * integration cursor may still precede the new boundary.
             */
            acc->last_sample_valid = false;
        }
    }

    acc->active.end_tick = boundary_tick;
    acc->active.sequence = sequence_increment(acc->next_sequence);
    acc->next_sequence = acc->active.sequence;
    acc->active.total_charge_As = acc->total_charge_As;
    acc->active.total_absolute_charge_As = acc->total_absolute_charge_As;
    acc->active.total_invalid_sample_count = acc->total_invalid_sample_count;

    uint32_t duration_ms =
        (uint32_t)(boundary_tick - acc->active.start_tick);

    if(duration_ms > 0U)
    {
        double duration_s = (double)duration_ms / 1000.0;
        acc->active.average_A =
            (float)(acc->active.charge_As / duration_s);
        double mean_square =
            acc->active_current_squared_A2s / duration_s;
        acc->active.rms_A =
            (float)sqrt((mean_square > 0.0) ? mean_square : 0.0);
    }
    else if(acc->active.sample_count > 0U)
    {
        acc->active.average_A = acc->active.latest_A;
        acc->active.rms_A = fabsf(acc->active.latest_A);
    }

    acc->active.valid =
        (acc->active.sample_count > 0U) &&
        (acc->active.invalid_sample_count == 0U) &&
        acc->last_sample_valid &&
        (latest_age_ms <= AMS_CURRENT_WINDOW_MAX_SAMPLE_AGE_MS);

    *completed = acc->active;

    memset(&acc->active, 0, sizeof(acc->active));
    acc->active.start_tick = boundary_tick;
    acc->active.end_tick = boundary_tick;
    acc->active_calibration_provenance_initialized = false;
    acc->active_sensor_metadata_initialized = false;

    if(acc->last_sample_valid)
    {
        acc->active.latest_sample_tick = acc->last_sample_tick;
        acc->active.latest_A = acc->last_current_A;
        acc->active.filtered_A = acc->last_filtered_A;
        acc->active.min_A = acc->last_current_A;
        acc->active.max_A = acc->last_current_A;

        ams_current_window_set_sensor_metadata(acc,
                                               acc->last_uncertainty_mA,
                                               acc->last_selected_range);

        current_merge_calibration_provenance(
            acc,
            acc->last_calibration_record_confident,
            acc->last_calibration_id);
    }

    acc->active_current_squared_A2s = 0.0;
    return completed->valid;
}
