#include <ams_core/ams_adbms_monitor.h>

#include <limits.h>
#include <string.h>

#define DIAG_STICKY_STATUS   (1UL << 0)
#define DIAG_STICKY_REFERENCE (1UL << 1)
#define DIAG_STICKY_IDENTITY (1UL << 2)
#define DIAG_STICKY_CONFIG   (1UL << 3)
#define DIAG_STICKY_POST     (1UL << 4)

static const ams_adbms_command_t raw_commands[6] = {
    AMS_ADBMS_CMD_RDCVA, AMS_ADBMS_CMD_RDCVB, AMS_ADBMS_CMD_RDCVC,
    AMS_ADBMS_CMD_RDCVD, AMS_ADBMS_CMD_RDCVE, AMS_ADBMS_CMD_RDCVF
};
static const ams_adbms_command_t avg_commands[6] = {
    AMS_ADBMS_CMD_RDACA, AMS_ADBMS_CMD_RDACB, AMS_ADBMS_CMD_RDACC,
    AMS_ADBMS_CMD_RDACD, AMS_ADBMS_CMD_RDACE, AMS_ADBMS_CMD_RDACF
};
static const ams_adbms_command_t iir_commands[6] = {
    AMS_ADBMS_CMD_RDFCA, AMS_ADBMS_CMD_RDFCB, AMS_ADBMS_CMD_RDFCC,
    AMS_ADBMS_CMD_RDFCD, AMS_ADBMS_CMD_RDFCE, AMS_ADBMS_CMD_RDFCF
};

static void sat_inc(uint32_t *value)
{
    if ((value != NULL) && (*value != UINT32_MAX)) {
        (*value)++;
    }
}

static bool io_valid(const ams_adbms_monitor_io_t *io)
{
    return (io != NULL) && (io->now_us != NULL) && (io->delay_us != NULL) &&
           (io->wake_b != NULL) && (io->write_b != NULL) && (io->write_read_b != NULL);
}

static void note_result(ams_adbms_monitor_t *m, ams_adbms_result_t result)
{
    if ((m == NULL) || (result == AMS_ADBMS_RESULT_OK)) {
        return;
    }
    if (result == AMS_ADBMS_RESULT_TRANSPORT_TIMEOUT ||
        result == AMS_ADBMS_RESULT_TRANSPORT_IO ||
        result == AMS_ADBMS_RESULT_TRANSPORT_TERMINAL ||
        result == AMS_ADBMS_RESULT_COUNTER || result == AMS_ADBMS_RESULT_PEC ||
        result == AMS_ADBMS_RESULT_CLOCK) m->recovery.continuity_lost = true;
    switch (result) {
    case AMS_ADBMS_RESULT_TRANSPORT_TIMEOUT:
        sat_inc(&m->transport_timeout_count);
        break;
    case AMS_ADBMS_RESULT_TRANSPORT_IO:
        sat_inc(&m->transport_io_count);
        break;
    case AMS_ADBMS_RESULT_TRANSPORT_TERMINAL:
        sat_inc(&m->transport_terminal_count);
        break;
    case AMS_ADBMS_RESULT_PEC:
        sat_inc(&m->pec_fail_count);
        break;
    case AMS_ADBMS_RESULT_COUNTER:
        sat_inc(&m->counter_mismatch_count);
        break;
    case AMS_ADBMS_RESULT_CONFIG_MISMATCH:
        sat_inc(&m->config_mismatch_count);
        m->sticky_diag_faults |= DIAG_STICKY_CONFIG;
        break;
    case AMS_ADBMS_RESULT_CCTS:
        sat_inc(&m->ccts_fault_count);
        break;
    case AMS_ADBMS_RESULT_DIAGNOSTIC:
        sat_inc(&m->diagnostic_fail_count);
        m->sticky_diag_faults |= DIAG_STICKY_STATUS;
        break;
    case AMS_ADBMS_RESULT_IDENTITY:
        m->sticky_diag_faults |= DIAG_STICKY_IDENTITY;
        break;
    default:
        break;
    }
}

static ams_adbms_result_t timed_now(const ams_adbms_monitor_io_t *io, uint64_t *now)
{
    ams_adbms_result_t result = io->now_us(io->context, now);
    return result == AMS_ADBMS_RESULT_OK ? AMS_ADBMS_RESULT_OK : AMS_ADBMS_RESULT_CLOCK;
}

static ams_adbms_result_t monitor_delay(const ams_adbms_monitor_io_t *io, uint32_t delay_us)
{
    ams_adbms_result_t result = io->delay_us(io->context, delay_us);
    return result == AMS_ADBMS_RESULT_OK ? AMS_ADBMS_RESULT_OK : result;
}

static bool mutation_outcome_uncertain(ams_adbms_result_t result)
{
    return (result == AMS_ADBMS_RESULT_TRANSPORT_TIMEOUT) ||
           (result == AMS_ADBMS_RESULT_TRANSPORT_IO) ||
           (result == AMS_ADBMS_RESULT_TRANSPORT_TERMINAL) ||
           (result == AMS_ADBMS_RESULT_CLOCK);
}

static void invalidate_session(ams_adbms_monitor_t *m, bool counter_unknown)
{
    m->temperature.mux_valid_mask = 0U;
    m->session_active = false;
    m->snapshot_active = false;
    /* Do not clear snapshot_cleanup_required here. A transport/session loss
     * after a successful SNAP still requires a cleanup-only re-wake/UNSNAP. */
    m->session_last_activity_us = 0U;
    if (counter_unknown) {
        ams_adbms_counter_unknown(&m->counter);
    }
}

static ams_adbms_result_t wake_only(ams_adbms_monitor_t *m,
                                    const ams_adbms_monitor_io_t *io,
                                    bool cold)
{
    ams_adbms_result_t result = io->wake_b(io->context, cold);
    if (result != AMS_ADBMS_RESULT_OK) {
        note_result(m, result);
        invalidate_session(m, true);
        return result;
    }
    /* Wake establishes physical liveness, not command-counter knowledge. */
    m->session_active = false;
    m->snapshot_active = false;
    return AMS_ADBMS_RESULT_OK;
}

static ams_adbms_result_t wake_and_command(ams_adbms_monitor_t *m,
                                           const ams_adbms_monitor_io_t *io,
                                           ams_adbms_command_t command)
{
    uint8_t frame[AMS_ADBMS_COMMAND_FRAME_BYTES];
    ams_adbms_command_info_t info;
    ams_adbms_result_t result;

    if (!ams_adbms_build_command_frame(command, frame) ||
        !ams_adbms_command_info(command, &info) ||
        (info.kind != AMS_ADBMS_COMMAND_ONLY)) {
        return AMS_ADBMS_RESULT_INVALID;
    }
    result = wake_only(m, io, false);
    if (result != AMS_ADBMS_RESULT_OK) {
        return result;
    }
    result = io->write_b(io->context, frame, sizeof(frame));
    if (result == AMS_ADBMS_RESULT_OK) {
        ams_adbms_counter_note_success(&m->counter, info.counter_effect);
    } else {
        note_result(m, result);
        if (mutation_outcome_uncertain(result)) {
            ams_adbms_counter_unknown(&m->counter);
        }
    }
    return result;
}

static ams_adbms_result_t wake_and_write6(ams_adbms_monitor_t *m,
                                          const ams_adbms_monitor_io_t *io,
                                          ams_adbms_command_t command,
                                          const uint8_t data[6])
{
    uint8_t frame[AMS_ADBMS_WRITE_FRAME_BYTES];
    ams_adbms_command_info_t info;
    ams_adbms_result_t result;

    if (!ams_adbms_build_write_frame(command, data, frame) ||
        !ams_adbms_command_info(command, &info)) {
        return AMS_ADBMS_RESULT_INVALID;
    }
    result = wake_only(m, io, false);
    if (result != AMS_ADBMS_RESULT_OK) {
        return result;
    }
    result = io->write_b(io->context, frame, sizeof(frame));
    if (result == AMS_ADBMS_RESULT_OK) {
        ams_adbms_counter_note_success(&m->counter, info.counter_effect);
    } else {
        note_result(m, result);
        if (mutation_outcome_uncertain(result)) {
            ams_adbms_counter_unknown(&m->counter);
        }
    }
    return result;
}

static ams_adbms_result_t session_open(ams_adbms_monitor_t *m,
                                       const ams_adbms_monitor_io_t *io)
{
    uint64_t now;
    ams_adbms_result_t result;

    if (m->session_active) {
        return AMS_ADBMS_RESULT_INVALID;
    }
    result = wake_only(m, io, false);
    if (result != AMS_ADBMS_RESULT_OK) {
        return result;
    }
    result = timed_now(io, &now);
    if (result != AMS_ADBMS_RESULT_OK) {
        note_result(m, result);
        invalidate_session(m, true);
        return result;
    }
    m->session_active = true;
    m->snapshot_active = false;
    m->session_last_activity_us = now;
    return AMS_ADBMS_RESULT_OK;
}

static ams_adbms_result_t session_require(ams_adbms_monitor_t *m,
                                          const ams_adbms_monitor_io_t *io)
{
    uint64_t now;
    ams_adbms_result_t result;

    if (!m->session_active) {
        return AMS_ADBMS_RESULT_SESSION_EXPIRED;
    }
    result = timed_now(io, &now);
    if (result != AMS_ADBMS_RESULT_OK) {
        note_result(m, result);
        invalidate_session(m, true);
        return result;
    }
    if ((now < m->session_last_activity_us) ||
        ((now - m->session_last_activity_us) >= AMS_ADBMS_Z017_SESSION_GUARD_US)) {
        sat_inc(&m->session_expiry_count);
        m->session_active = false;
        m->snapshot_active = false;
        return AMS_ADBMS_RESULT_SESSION_EXPIRED;
    }
    return AMS_ADBMS_RESULT_OK;
}

static ams_adbms_result_t session_mark_activity(ams_adbms_monitor_t *m,
                                                const ams_adbms_monitor_io_t *io)
{
    uint64_t now;
    ams_adbms_result_t result = timed_now(io, &now);
    if (result != AMS_ADBMS_RESULT_OK) {
        note_result(m, result);
        invalidate_session(m, true);
        return result;
    }
    m->session_last_activity_us = now;
    return AMS_ADBMS_RESULT_OK;
}

static ams_adbms_result_t session_command(ams_adbms_monitor_t *m,
                                          const ams_adbms_monitor_io_t *io,
                                          ams_adbms_command_t command)
{
    uint8_t frame[4];
    ams_adbms_command_info_t info;
    ams_adbms_result_t result;

    result = session_require(m, io);
    if (result != AMS_ADBMS_RESULT_OK) {
        return result;
    }
    if (!ams_adbms_build_command_frame(command, frame) ||
        !ams_adbms_command_info(command, &info) ||
        (info.kind != AMS_ADBMS_COMMAND_ONLY)) {
        return AMS_ADBMS_RESULT_INVALID;
    }
    /* SNAP is a remote-state mutation. From the instant the transaction may
     * reach the wire, cleanup is conservatively owed until a positively
     * completed UNSNAP proves otherwise. */
    if (command == AMS_ADBMS_CMD_SNAP) {
        m->snapshot_cleanup_required = true;
    }

    result = io->write_b(io->context, frame, sizeof(frame));
    if (result != AMS_ADBMS_RESULT_OK) {
        note_result(m, result);
        if (mutation_outcome_uncertain(result)) {
            ams_adbms_counter_unknown(&m->counter);
        }
        invalidate_session(m, false);
        return result;
    }
    ams_adbms_counter_note_success(&m->counter, info.counter_effect);

    /* Record the known remote state before consulting the clock. A clock
     * failure may invalidate local session ownership but must not erase the
     * cleanup obligation (SNAP) or known-complete cleanup (UNSNAP). */
    if (command == AMS_ADBMS_CMD_SNAP) {
        m->snapshot_active = true;
    } else if (command == AMS_ADBMS_CMD_UNSNAP) {
        m->snapshot_active = false;
        m->snapshot_cleanup_required = false;
    }
    return session_mark_activity(m, io);
}

static ams_adbms_result_t session_read(ams_adbms_monitor_t *m,
                                       const ams_adbms_monitor_io_t *io,
                                       ams_adbms_command_t command,
                                       ams_adbms_packet_t *packet)
{
    uint8_t frame[4];
    uint8_t rx[8];
    ams_adbms_command_info_t info;
    ams_adbms_result_t result;

    if (packet == NULL) {
        return AMS_ADBMS_RESULT_INVALID;
    }
    memset(packet, 0, sizeof(*packet));
    result = session_require(m, io);
    if (result != AMS_ADBMS_RESULT_OK) {
        return result;
    }
    if (!ams_adbms_build_command_frame(command, frame) ||
        !ams_adbms_command_info(command, &info) ||
        (info.kind != AMS_ADBMS_COMMAND_READ)) {
        return AMS_ADBMS_RESULT_INVALID;
    }
    result = io->write_read_b(io->context, frame, sizeof(frame), rx, sizeof(rx));
    if (result != AMS_ADBMS_RESULT_OK) {
        note_result(m, result);
        if (mutation_outcome_uncertain(result)) {
            ams_adbms_counter_unknown(&m->counter);
        }
        invalidate_session(m, false);
        return result;
    }
    result = session_mark_activity(m, io);
    if (result != AMS_ADBMS_RESULT_OK) {
        return result;
    }
    result = ams_adbms_decode_packet(rx, &m->counter, packet);
    if (result != AMS_ADBMS_RESULT_OK) {
        note_result(m, result);
    }
    return result;
}

static ams_adbms_result_t standalone_read(ams_adbms_monitor_t *m,
                                          const ams_adbms_monitor_io_t *io,
                                          ams_adbms_command_t command,
                                          ams_adbms_packet_t *packet)
{
    ams_adbms_result_t result = session_open(m, io);
    if (result == AMS_ADBMS_RESULT_OK) {
        result = session_read(m, io, command, packet);
    }
    m->session_active = false;
    m->snapshot_active = false;
    return result;
}

static ams_adbms_result_t verify_config(ams_adbms_monitor_t *m,
                                        const ams_adbms_monitor_io_t *io)
{
    ams_adbms_packet_t cfga;
    ams_adbms_packet_t cfgb;
    ams_adbms_result_t result;

    m->config_verified = false;
    result = session_open(m, io);
    if (result == AMS_ADBMS_RESULT_OK) {
        result = session_read(m, io, AMS_ADBMS_CMD_RDCFGA, &cfga);
    }
    if ((result == AMS_ADBMS_RESULT_OK) &&
        !ams_adbms_z017_config_matches(AMS_ADBMS_CMD_RDCFGA, cfga.data)) {
        result = AMS_ADBMS_RESULT_CONFIG_MISMATCH;
        note_result(m, result);
    }
    if (result == AMS_ADBMS_RESULT_OK) {
        result = session_read(m, io, AMS_ADBMS_CMD_RDCFGB, &cfgb);
    }
    if ((result == AMS_ADBMS_RESULT_OK) &&
        !ams_adbms_z017_config_matches(AMS_ADBMS_CMD_RDCFGB, cfgb.data)) {
        result = AMS_ADBMS_RESULT_CONFIG_MISMATCH;
        note_result(m, result);
    }
    m->session_active = false;
    m->snapshot_active = false;
    if (result == AMS_ADBMS_RESULT_OK) {
        m->config_verified = true;
    }
    return result;
}

static bool status_code_to_mv(int16_t code, int16_t *mv)
{
    int32_t value;
    if ((mv == NULL) || (code == INT16_MIN) || (code == INT16_MAX) || (code == (int16_t)-1)) {
        return false;
    }
    value = 1500 + (((int32_t)code * 150) / 1000);
    if ((value < INT16_MIN) || (value > INT16_MAX)) {
        return false;
    }
    *mv = (int16_t)value;
    return true;
}

static bool status_temp_deci_c(int16_t code, int16_t *deci_c)
{
    int32_t value;
    if ((deci_c == NULL) || (code == INT16_MIN) || (code == INT16_MAX) || (code == (int16_t)-1)) {
        return false;
    }
    value = (((int32_t)code + 10000) / 5) - 2730;
    if ((value < INT16_MIN) || (value > INT16_MAX)) {
        return false;
    }
    *deci_c = (int16_t)value;
    return true;
}

static bool reference_image_ok(const uint8_t stata[6], const uint8_t statb[6])
{
    int16_t vref2_raw = ams_adbms_cell_code(stata[0], stata[1]);
    int16_t itmp_raw = ams_adbms_cell_code(stata[2], stata[3]);
    int16_t vd_raw = ams_adbms_cell_code(statb[0], statb[1]);
    int16_t va_raw = ams_adbms_cell_code(statb[2], statb[3]);
    int16_t vres_raw = ams_adbms_cell_code(statb[4], statb[5]);
    int16_t vref2, vd, va, vres, temp;
    int32_t delta;

    if (!status_code_to_mv(vref2_raw, &vref2) || !status_temp_deci_c(itmp_raw, &temp) ||
        !status_code_to_mv(vd_raw, &vd) || !status_code_to_mv(va_raw, &va) ||
        !status_code_to_mv(vres_raw, &vres)) {
        return false;
    }
    delta = (int32_t)vres - (int32_t)vref2;
    if (delta < 0) {
        delta = -delta;
    }
    return (vref2 >= 2980) && (vref2 <= 3020) &&
           (vd >= 2700) && (vd <= 3600) &&
           (va >= 4500) && (va <= 5500) &&
           (delta <= 50) && (temp >= -500) && (temp <= 1500);
}

static bool non_cs_status_clean(const ams_adbms_statc_t *c,
                                const ams_adbms_statd_t *d)
{
    if ((c == NULL) || (d == NULL) || !c->valid || !d->valid) {
        return false;
    }
    return !c->va_ov && !c->va_uv && !c->vd_ov && !c->vd_uv &&
           !c->ced && !c->cmed && !c->sed && !c->smed &&
           !c->vdel && !c->vde && !c->spiflt && !c->sleep &&
           !c->thsd && !c->tmodchk && !c->oscchk &&
           (d->osc_counter >= 52U) && (d->osc_counter <= 71U);
}

static ams_adbms_result_t read_diagnostic_image(ams_adbms_monitor_t *m,
                                                const ams_adbms_monitor_io_t *io,
                                                bool inject_spiflt,
                                                ams_adbms_statc_t *statc_out,
                                                ams_adbms_statd_t *statd_out,
                                                bool strict_reference)
{
    ams_adbms_packet_t a, b, c, d, e;
    ams_adbms_statc_t statc;
    ams_adbms_statd_t statd;
    ams_adbms_result_t result = session_open(m, io);

    memset(&statc, 0, sizeof(statc));
    memset(&statd, 0, sizeof(statd));
    if ((result == AMS_ADBMS_RESULT_OK) && strict_reference) {
        result = session_read(m, io, AMS_ADBMS_CMD_RDSTATA, &a);
        if (result == AMS_ADBMS_RESULT_OK) {
            result = session_read(m, io, AMS_ADBMS_CMD_RDSTATB, &b);
        }
    }
    if (result == AMS_ADBMS_RESULT_OK) {
        result = session_read(m, io,
                              inject_spiflt ? AMS_ADBMS_CMD_RDSTATCERR : AMS_ADBMS_CMD_RDSTATC,
                              &c);
    }
    if (result == AMS_ADBMS_RESULT_OK) {
        ams_adbms_parse_statc(c.data, &statc);
        result = session_read(m, io, AMS_ADBMS_CMD_RDSTATD, &d);
    }
    if (result == AMS_ADBMS_RESULT_OK) {
        ams_adbms_parse_statd(d.data, &statd);
        result = session_read(m, io, AMS_ADBMS_CMD_RDSTATE, &e);
    }
    m->session_active = false;
    m->snapshot_active = false;
    if (result != AMS_ADBMS_RESULT_OK) {
        return result;
    }
    if (strict_reference && !reference_image_ok(a.data, b.data)) {
        m->sticky_diag_faults |= DIAG_STICKY_REFERENCE;
        note_result(m, AMS_ADBMS_RESULT_DIAGNOSTIC);
        return AMS_ADBMS_RESULT_DIAGNOSTIC;
    }
    if (statc_out != NULL) {
        *statc_out = statc;
    }
    if (statd_out != NULL) {
        *statd_out = statd;
    }
    return AMS_ADBMS_RESULT_OK;
}

static ams_adbms_result_t establish_baseline(ams_adbms_monitor_t *m,
                                             const ams_adbms_monitor_io_t *io)
{
    uint8_t payload[6];
    ams_adbms_statc_t statc;
    ams_adbms_statd_t statd;
    ams_adbms_result_t result;

    ams_adbms_z017_clear_flag_payload(payload);
    result = wake_and_write6(m, io, AMS_ADBMS_CMD_CLRFLAG, payload);
    if (result == AMS_ADBMS_RESULT_OK) {
        ams_adbms_z017_clear_ovuv_payload(payload);
        result = wake_and_write6(m, io, AMS_ADBMS_CMD_CLOVUV, payload);
    }
    if (result == AMS_ADBMS_RESULT_OK) {
        result = wake_and_command(m, io, AMS_ADBMS_CMD_ADCV_BASELINE);
    }
    if (result == AMS_ADBMS_RESULT_OK) {
        result = monitor_delay(io, AMS_ADBMS_Z017_REDUNDANT_WAIT_US);
    }
    if (result == AMS_ADBMS_RESULT_OK) {
        result = wake_and_command(m, io, AMS_ADBMS_CMD_ADAX_ALL);
    }
    if (result == AMS_ADBMS_RESULT_OK) {
        result = monitor_delay(io, AMS_ADBMS_Z017_AUX_WAIT_US);
    }
    if (result == AMS_ADBMS_RESULT_OK) {
        result = read_diagnostic_image(m, io, false, &statc, &statd, true);
    }
    if ((result == AMS_ADBMS_RESULT_OK) && !non_cs_status_clean(&statc, &statd)) {
        result = AMS_ADBMS_RESULT_DIAGNOSTIC;
        note_result(m, result);
    }
    return result;
}

static bool post_stage_matches(ams_adbms_post_stage_t stage,
                               const ams_adbms_statc_t *d,
                               bool *unexpected)
{
    bool hit = false;
    bool extra = false;
    if (unexpected != NULL) {
        *unexpected = false;
    }
    if ((d == NULL) || !d->valid) {
        return false;
    }
    switch (stage) {
    case AMS_ADBMS_POST_OSC_FAST:
    case AMS_ADBMS_POST_OSC_SLOW:
        hit = d->oscchk;
        extra = d->thsd || d->tmodchk || d->cmed || d->smed;
        break;
    case AMS_ADBMS_POST_SUPPLY_UV:
        hit = d->va_uv || d->vd_uv;
        extra = d->thsd || d->tmodchk || d->cmed || d->smed;
        break;
    case AMS_ADBMS_POST_SUPPLY_OV:
        hit = d->va_ov || d->vd_ov || d->vde || d->vdel;
        extra = d->thsd || d->tmodchk || d->cmed || d->smed;
        break;
    case AMS_ADBMS_POST_THSD:
        hit = d->thsd;
        extra = d->tmodchk || d->cmed || d->smed;
        break;
    case AMS_ADBMS_POST_NVM_ED:
        hit = d->ced && d->sed;
        extra = d->cmed || d->smed || d->tmodchk;
        break;
    case AMS_ADBMS_POST_NVM_MED:
        hit = d->cmed && d->smed;
        extra = d->tmodchk;
        break;
    case AMS_ADBMS_POST_TMOD:
        hit = d->tmodchk;
        break;
    case AMS_ADBMS_POST_SPIFLT:
        hit = d->spiflt;
        extra = d->thsd || d->tmodchk || d->cmed || d->smed;
        break;
    default:
        return true;
    }
    if (unexpected != NULL) {
        *unexpected = extra;
    }
    return hit && !extra;
}

static ams_adbms_result_t post_once(ams_adbms_monitor_t *m,
                                    const ams_adbms_monitor_io_t *io)
{
    static const struct {
        ams_adbms_post_stage_t stage;
        uint8_t flag_d;
    } stages[] = {
        {AMS_ADBMS_POST_OSC_FAST, 0x01U},
        {AMS_ADBMS_POST_OSC_SLOW, 0x02U},
        {AMS_ADBMS_POST_SUPPLY_UV, 0x04U},
        {AMS_ADBMS_POST_SUPPLY_OV, 0x0CU},
        {AMS_ADBMS_POST_THSD, 0x10U},
        {AMS_ADBMS_POST_NVM_ED, 0x20U},
        {AMS_ADBMS_POST_NVM_MED, 0x40U},
        {AMS_ADBMS_POST_TMOD, 0x80U},
    };
    uint8_t cfga[6];
    uint8_t payload[6];
    ams_adbms_result_t status;
    ams_adbms_result_t restore;

    m->post_stage = AMS_ADBMS_POST_BASELINE;
    m->post_failed_stage_mask = 0U;
    m->post_unexpected_stage_mask = 0U;
    m->post_expected_flag_d = 0U;

    ams_adbms_z017_production_cfga(cfga);
    status = wake_and_write6(m, io, AMS_ADBMS_CMD_WRCFGA, cfga);
    if (status == AMS_ADBMS_RESULT_OK) {
        ams_adbms_z017_clear_flag_payload(payload);
        status = wake_and_write6(m, io, AMS_ADBMS_CMD_CLRFLAG, payload);
    }
    if (status == AMS_ADBMS_RESULT_OK) {
        status = establish_baseline(m, io);
    }

    for (size_t i = 0U; (status == AMS_ADBMS_RESULT_OK) && (i < sizeof(stages)/sizeof(stages[0])); ++i) {
        ams_adbms_statc_t statc;
        m->post_stage = stages[i].stage;
        m->post_expected_flag_d = stages[i].flag_d;
        ams_adbms_z017_production_cfga(cfga);
        cfga[1] = stages[i].flag_d;
        status = wake_and_write6(m, io, AMS_ADBMS_CMD_WRCFGA, cfga);
        if (status == AMS_ADBMS_RESULT_OK) {
            status = read_diagnostic_image(m, io, false, &statc, NULL, false);
        }
        if (status == AMS_ADBMS_RESULT_OK) {
            bool unexpected = false;
            if (!post_stage_matches(stages[i].stage, &statc, &unexpected)) {
                uint16_t bit = (uint16_t)(1U << i);
                m->post_failed_stage_mask |= bit;
                if (unexpected) {
                    m->post_unexpected_stage_mask |= bit;
                }
                status = AMS_ADBMS_RESULT_DIAGNOSTIC;
                break;
            }
        } else {
            m->post_failed_stage_mask |= (uint16_t)(1U << i);
            break;
        }
        ams_adbms_z017_clear_flag_payload(payload);
        status = wake_and_write6(m, io, AMS_ADBMS_CMD_CLRFLAG, payload);
    }

    if (status == AMS_ADBMS_RESULT_OK) {
        ams_adbms_statc_t statc;
        m->post_stage = AMS_ADBMS_POST_SPIFLT;
        m->post_expected_flag_d = 0U;
        status = read_diagnostic_image(m, io, true, &statc, NULL, false);
        if (status == AMS_ADBMS_RESULT_OK) {
            bool unexpected = false;
            if (!post_stage_matches(AMS_ADBMS_POST_SPIFLT, &statc, &unexpected)) {
                m->post_failed_stage_mask |= (uint16_t)(1U << 8U);
                if (unexpected) {
                    m->post_unexpected_stage_mask |= (uint16_t)(1U << 8U);
                }
                status = AMS_ADBMS_RESULT_DIAGNOSTIC;
            }
        } else {
            m->post_failed_stage_mask |= (uint16_t)(1U << 8U);
        }
        if (status == AMS_ADBMS_RESULT_OK) {
            ams_adbms_z017_clear_flag_payload(payload);
            status = wake_and_write6(m, io, AMS_ADBMS_CMD_CLRFLAG, payload);
        }
    }

    /* Mandatory production restoration even after injected-stage failure. */
    m->post_stage = AMS_ADBMS_POST_RESTORE;
    ams_adbms_z017_production_cfga(cfga);
    restore = wake_and_write6(m, io, AMS_ADBMS_CMD_WRCFGA, cfga);
    if (restore == AMS_ADBMS_RESULT_OK) {
        ams_adbms_z017_clear_flag_payload(payload);
        restore = wake_and_write6(m, io, AMS_ADBMS_CMD_CLRFLAG, payload);
    }
    if (restore != AMS_ADBMS_RESULT_OK) {
        /* A failed mandatory restoration makes the configuration state
         * unproven. Do not allow a later POST retry to erase that fact. */
        m->config_verified = false;
        return restore;
    }

    m->post_stage = AMS_ADBMS_POST_FINAL_BASELINE;
    if (status == AMS_ADBMS_RESULT_OK) {
        status = establish_baseline(m, io);
    }
    if (status == AMS_ADBMS_RESULT_OK) {
        status = verify_config(m, io);
    }
    return status;
}

static ams_adbms_result_t run_post(ams_adbms_monitor_t *m,
                                   const ams_adbms_monitor_io_t *io)
{
    ams_adbms_result_t result = AMS_ADBMS_RESULT_DIAGNOSTIC;
    uint32_t prior_sticky = m->sticky_diag_faults;
    uint16_t cumulative_failed = 0U;
    uint16_t cumulative_unexpected = 0U;

    sat_inc(&m->post_run_count);
    m->post_attempts = 0U;
    m->startup_post_passed = false;
    for (uint8_t attempt = 1U; attempt <= AMS_ADBMS_Z017_MAX_POST_ATTEMPTS; ++attempt) {
        m->post_attempts = attempt;
        result = post_once(m, io);
        cumulative_failed |= m->post_failed_stage_mask;
        cumulative_unexpected |= m->post_unexpected_stage_mask;
        if ((result == AMS_ADBMS_RESULT_OK) || !m->config_verified) {
            break;
        }
    }
    m->post_failed_stage_mask = cumulative_failed;
    m->post_unexpected_stage_mask = cumulative_unexpected;
    if (result == AMS_ADBMS_RESULT_OK) {
        m->sticky_diag_faults = prior_sticky;
        m->startup_post_passed = true;
        m->post_stage = AMS_ADBMS_POST_PASS;
    } else {
        m->sticky_diag_faults |= prior_sticky | DIAG_STICKY_POST;
        sat_inc(&m->post_fail_count);
        m->post_stage = AMS_ADBMS_POST_FAIL;
        note_result(m, result);
    }
    return result;
}

static bool data6_all_zero(const uint8_t data[AMS_ADBMS_PACKET_DATA_BYTES])
{
    static const uint8_t zero[AMS_ADBMS_PACKET_DATA_BYTES] = {0};
    return (data != NULL) && (memcmp(data, zero, sizeof(zero)) == 0);
}

static bool muted_cfga_matches_production(const uint8_t actual[AMS_ADBMS_PACKET_DATA_BYTES])
{
    uint8_t expected[AMS_ADBMS_PACKET_DATA_BYTES];

    if (actual == NULL) {
        return false;
    }
    ams_adbms_z017_production_cfga(expected);
    expected[5] |= 0x10U; /* MUTE_ST: CFGA byte 5 bit 4 on ADBMS6830B. */
    return memcmp(actual, expected, sizeof(expected)) == 0;
}

static void best_effort_balance_zero(ams_adbms_monitor_t *m,
                                     const ams_adbms_monitor_io_t *io)
{
    uint8_t cfgb[AMS_ADBMS_PACKET_DATA_BYTES];
    static const uint8_t zero[AMS_ADBMS_PACKET_DATA_BYTES] = {0};

    ams_adbms_z017_production_cfgb(cfgb);
    (void)wake_and_write6(m, io, AMS_ADBMS_CMD_WRCFGB, cfgb);
    (void)wake_and_write6(m, io, AMS_ADBMS_CMD_WRPWMA, zero);
    (void)wake_and_write6(m, io, AMS_ADBMS_CMD_WRPWMB, zero);
}

static ams_adbms_result_t startup_balance_inhibit(ams_adbms_monitor_t *m,
                                                  const ams_adbms_monitor_io_t *io)
{
    static const uint8_t zero[AMS_ADBMS_PACKET_DATA_BYTES] = {0};
    uint8_t cfgb[AMS_ADBMS_PACKET_DATA_BYTES];
    ams_adbms_packet_t packet;
    ams_adbms_result_t result;
    ams_adbms_result_t first_error = AMS_ADBMS_RESULT_OK;
    bool zero_writes_ok = true;

    sat_inc(&m->balance_inhibit_attempt_count);
    m->balance_mute_verified = false;
    m->balance_durable_zero_verified = false;

    /* Selected v2.6.27 accumulator_init() safety finalization: fastest ASIC
     * discharge inhibit, then physical MUTE_ST readback. No UNMUTE command is
     * admitted anywhere in Z017. */
    result = session_open(m, io);
    if (result == AMS_ADBMS_RESULT_OK) {
        result = session_command(m, io, AMS_ADBMS_CMD_MUTE);
    }
    if (result == AMS_ADBMS_RESULT_OK) {
        result = session_read(m, io, AMS_ADBMS_CMD_RDCFGA, &packet);
    }
    if ((result == AMS_ADBMS_RESULT_OK) && muted_cfga_matches_production(packet.data)) {
        m->balance_mute_verified = true;
    } else {
        if (result == AMS_ADBMS_RESULT_OK) {
            result = AMS_ADBMS_RESULT_BALANCE_INHIBIT;
        }
        first_error = result;
    }
    m->session_active = false;
    m->snapshot_active = false;

    /* Make the safe state durable: DCC/timer remain zero in production CFGB and
     * both PWM banks are explicitly written to zero. Attempt all safe clears
     * unless a terminal backend makes further traffic impossible. */
    ams_adbms_z017_production_cfgb(cfgb);
    result = wake_and_write6(m, io, AMS_ADBMS_CMD_WRCFGB, cfgb);
    if (result != AMS_ADBMS_RESULT_OK) {
        zero_writes_ok = false;
        if (first_error == AMS_ADBMS_RESULT_OK) first_error = result;
    }
    if (result != AMS_ADBMS_RESULT_TRANSPORT_TERMINAL) {
        result = wake_and_write6(m, io, AMS_ADBMS_CMD_WRPWMA, zero);
        if (result != AMS_ADBMS_RESULT_OK) {
            zero_writes_ok = false;
            if (first_error == AMS_ADBMS_RESULT_OK) first_error = result;
        }
    } else {
        zero_writes_ok = false;
    }
    if (result != AMS_ADBMS_RESULT_TRANSPORT_TERMINAL) {
        result = wake_and_write6(m, io, AMS_ADBMS_CMD_WRPWMB, zero);
        if (result != AMS_ADBMS_RESULT_OK) {
            zero_writes_ok = false;
            if (first_error == AMS_ADBMS_RESULT_OK) first_error = result;
        }
    } else {
        zero_writes_ok = false;
    }

    /* RDCFGB/RDPWMA/RDPWMB are one logical proof epoch. */
    if (zero_writes_ok) {
        result = session_open(m, io);
        if (result == AMS_ADBMS_RESULT_OK) {
            result = session_read(m, io, AMS_ADBMS_CMD_RDCFGB, &packet);
        }
        if ((result == AMS_ADBMS_RESULT_OK) &&
            !ams_adbms_z017_config_matches(AMS_ADBMS_CMD_RDCFGB, packet.data)) {
            result = AMS_ADBMS_RESULT_BALANCE_INHIBIT;
        }
        if (result == AMS_ADBMS_RESULT_OK) {
            result = session_read(m, io, AMS_ADBMS_CMD_RDPWMA, &packet);
        }
        if ((result == AMS_ADBMS_RESULT_OK) && !data6_all_zero(packet.data)) {
            result = AMS_ADBMS_RESULT_BALANCE_INHIBIT;
        }
        if (result == AMS_ADBMS_RESULT_OK) {
            result = session_read(m, io, AMS_ADBMS_CMD_RDPWMB, &packet);
        }
        if ((result == AMS_ADBMS_RESULT_OK) && !data6_all_zero(packet.data)) {
            result = AMS_ADBMS_RESULT_BALANCE_INHIBIT;
        }
        m->session_active = false;
        m->snapshot_active = false;
        if (result == AMS_ADBMS_RESULT_OK) {
            m->balance_durable_zero_verified = true;
        } else if (first_error == AMS_ADBMS_RESULT_OK) {
            first_error = result;
        }
    }

    if (!m->balance_durable_zero_verified) {
        best_effort_balance_zero(m, io);
    }

    if (!m->balance_mute_verified || !m->balance_durable_zero_verified) {
        sat_inc(&m->balance_inhibit_fail_count);
        if (first_error == AMS_ADBMS_RESULT_OK) {
            first_error = AMS_ADBMS_RESULT_BALANCE_INHIBIT;
        }
        note_result(m, first_error);
        return first_error;
    }
    return AMS_ADBMS_RESULT_OK;
}

void ams_adbms_monitor_reset(ams_adbms_monitor_t *monitor)
{
    if (monitor == NULL) {
        return;
    }
    memset(monitor, 0, sizeof(*monitor));
    monitor->state = AMS_ADBMS_MONITOR_UNINITIALIZED;
    monitor->first_error = AMS_ADBMS_RESULT_OK;
    monitor->cleanup_error = AMS_ADBMS_RESULT_OK;
    monitor->last_result = AMS_ADBMS_RESULT_OK;
    ams_adbms_counter_unknown(&monitor->counter);
    monitor->counter.unknown_count = 0U;
    ams_cell_image_init(&monitor->cells);
}

ams_adbms_result_t ams_adbms_monitor_initialize(ams_adbms_monitor_t *m,
                                                const ams_adbms_monitor_io_t *io)
{
    uint8_t cfga[6];
    uint8_t cfgb[6];
    ams_adbms_packet_t sid;
    ams_adbms_result_t result;

    if ((m == NULL) || !io_valid(io)) {
        return AMS_ADBMS_RESULT_INVALID;
    }
    ams_adbms_monitor_reset(m);
    m->state = AMS_ADBMS_MONITOR_INITIALIZING;
    sat_inc(&m->init_attempt_count);

    /* SRST wrapper performs the checked normal wake itself. Do not add a
     * second pre-reset wake: the frozen Z017 startup trace is wake -> SRST. */
    result = wake_and_command(m, io, AMS_ADBMS_CMD_SRST);
    if (result == AMS_ADBMS_RESULT_OK) {
        result = monitor_delay(io, AMS_ADBMS_Z017_RESET_SETTLE_US);
    }
    if (result == AMS_ADBMS_RESULT_OK) {
        result = standalone_read(m, io, AMS_ADBMS_CMD_RDSID, &sid);
    }
    if ((result == AMS_ADBMS_RESULT_OK) && !ams_adbms_sid_is_6830b(sid.data)) {
        result = AMS_ADBMS_RESULT_IDENTITY;
        note_result(m, result);
    }
    if (result == AMS_ADBMS_RESULT_OK) {
        memcpy(m->recovery.sid, sid.data, 6U);
        m->recovery.identity_valid = true;
        ams_adbms_z017_production_cfga(cfga);
        result = wake_and_write6(m, io, AMS_ADBMS_CMD_WRCFGA, cfga);
    }
    if (result == AMS_ADBMS_RESULT_OK) {
        ams_adbms_z017_production_cfgb(cfgb);
        result = wake_and_write6(m, io, AMS_ADBMS_CMD_WRCFGB, cfgb);
    }
    if (result == AMS_ADBMS_RESULT_OK) {
        result = verify_config(m, io);
    }
    if (result == AMS_ADBMS_RESULT_OK) {
        result = establish_baseline(m, io);
    }
    if (result == AMS_ADBMS_RESULT_OK) {
        result = run_post(m, io);
    }
    if (result == AMS_ADBMS_RESULT_OK) {
        result = verify_config(m, io);
    }
    if (result == AMS_ADBMS_RESULT_OK) {
        result = startup_balance_inhibit(m, io);
    }

    m->last_result = result;
    if (result == AMS_ADBMS_RESULT_OK) {
        m->initialized = true;
        m->acquisition_live = true;
        m->state = AMS_ADBMS_MONITOR_READY;
        m->recovery.continuity_lost = false;
    } else {
        m->initialized = false;
        m->acquisition_live = false;
        m->state = AMS_ADBMS_MONITOR_FAULTED;
        sat_inc(&m->init_fail_count);
        if (m->first_error == AMS_ADBMS_RESULT_OK) {
            m->first_error = result;
        }
    }
    return result;
}

static void clear_candidate(ams_adbms_monitor_t *m)
{
    memset(m->raw_codes, 0, sizeof(m->raw_codes));
    memset(m->avg8_codes, 0, sizeof(m->avg8_codes));
    memset(m->iir_codes, 0, sizeof(m->iir_codes));
    m->raw_fresh_mask = 0U;
    m->raw_bad_mask = 0U;
    m->avg8_fresh_mask = 0U;
    m->avg8_bad_mask = 0U;
    m->iir_fresh_mask = 0U;
    m->iir_bad_mask = 0U;
    m->ccts = 0U;
    m->ccts_valid = false;
    m->statd_valid = false;
    memset(&m->statd, 0, sizeof(m->statd));
}

static void parse_cell_group(const uint8_t data[6], uint8_t group,
                             int16_t codes[AMS_CELL_IMAGE_REGISTER_COUNT],
                             uint16_t *fresh_mask)
{
    uint8_t first = (uint8_t)(group * 3U);
    uint8_t count = group < 5U ? 3U : 1U;
    for (uint8_t i = 0U; i < count; ++i) {
        uint8_t cell = (uint8_t)(first + i);
        codes[cell] = ams_adbms_cell_code(data[i * 2U], data[i * 2U + 1U]);
        *fresh_mask |= (uint16_t)(1U << cell);
    }
}

static bool optional_failure_is_critical(ams_adbms_result_t result)
{
    return (result == AMS_ADBMS_RESULT_COUNTER) ||
           (result == AMS_ADBMS_RESULT_SESSION_EXPIRED) ||
           (result == AMS_ADBMS_RESULT_TRANSPORT_TIMEOUT) ||
           (result == AMS_ADBMS_RESULT_TRANSPORT_IO) ||
           (result == AMS_ADBMS_RESULT_TRANSPORT_TERMINAL) ||
           (result == AMS_ADBMS_RESULT_CLOCK) ||
           (result == AMS_ADBMS_RESULT_OWNER);
}

static ams_adbms_result_t cleanup_snapshot(ams_adbms_monitor_t *m,
                                           const ams_adbms_monitor_io_t *io)
{
    ams_adbms_result_t result;

    if (!m->snapshot_cleanup_required) {
        m->session_active = false;
        m->snapshot_active = false;
        return AMS_ADBMS_RESULT_OK;
    }

    /* If session ownership was lost after SNAP, establish a cleanup-only awake
     * session. Never continue the candidate epoch across that wake. */
    if (!m->session_active || !m->snapshot_active) {
        m->session_active = false;
        m->snapshot_active = false;
        result = session_open(m, io);
        if (result != AMS_ADBMS_RESULT_OK) {
            return result;
        }
    }

    result = session_command(m, io, AMS_ADBMS_CMD_UNSNAP);
    if (result == AMS_ADBMS_RESULT_SESSION_EXPIRED) {
        m->session_active = false;
        m->snapshot_active = false;
        result = session_open(m, io);
        if (result == AMS_ADBMS_RESULT_OK) {
            result = session_command(m, io, AMS_ADBMS_CMD_UNSNAP);
        }
    }
    m->session_active = false;
    m->snapshot_active = false;
    if (result == AMS_ADBMS_RESULT_OK) {
        m->snapshot_cleanup_required = false;
    }
    return result;
}

static uint16_t invalid_data_mask(const int16_t codes[AMS_CELL_IMAGE_REGISTER_COUNT],
                                  uint16_t fresh_mask)
{
    uint16_t bad = 0U;

    for (uint8_t cell = 0U; cell < AMS_CELL_IMAGE_REGISTER_COUNT; ++cell) {
        uint16_t bit = (uint16_t)(1U << cell);
        uint16_t mv = 0U;
        if (((fresh_mask & bit) != 0U) &&
            (!ams_adbms_cell_code_to_mv(codes[cell], &mv) ||
             (mv < AMS_CELL_IMAGE_VALID_MIN_MV) ||
             (mv > AMS_CELL_IMAGE_VALID_MAX_MV))) {
            bad |= bit;
        }
    }
    return bad;
}

static ams_adbms_result_t read_product_groups(ams_adbms_monitor_t *m,
                                              const ams_adbms_monitor_io_t *io,
                                              const ams_adbms_command_t commands[6],
                                              int16_t codes[16],
                                              uint16_t *fresh_mask,
                                              uint16_t *bad_mask)
{
    ams_adbms_packet_t packet;
    ams_adbms_result_t result = AMS_ADBMS_RESULT_OK;

    *fresh_mask = 0U;
    *bad_mask = 0U;
    for (uint8_t group = 0U; group < 6U; ++group) {
        result = session_read(m, io, commands[group], &packet);
        if (result != AMS_ADBMS_RESULT_OK) {
            uint16_t group_mask = group < 5U ? (uint16_t)(0x7U << (group * 3U)) : 0x8000U;
            *bad_mask |= group_mask;
            return result;
        }
        parse_cell_group(packet.data, group, codes, fresh_mask);
    }
    return AMS_ADBMS_RESULT_OK;
}

static ams_adbms_result_t read_epoch(ams_adbms_monitor_t *m,
                                     const ams_adbms_monitor_io_t *io,
                                     uint32_t now_ms)
{
    ams_adbms_packet_t packet;
    ams_adbms_statc_t statc;
    ams_adbms_result_t critical = AMS_ADBMS_RESULT_OK;
    ams_adbms_result_t result;
    ams_adbms_result_t cleanup;
    bool avg_complete = false;
    bool iir_complete = false;

    clear_candidate(m);
    if (m->snapshot_cleanup_required) {
        result = cleanup_snapshot(m, io);
        if (result != AMS_ADBMS_RESULT_OK) {
            return result;
        }
    }
    result = session_open(m, io);
    if (result != AMS_ADBMS_RESULT_OK) {
        return result;
    }
    result = session_command(m, io, AMS_ADBMS_CMD_SNAP);
    if (result != AMS_ADBMS_RESULT_OK) {
        critical = result;
        goto cleanup;
    }
    result = monitor_delay(io, AMS_ADBMS_Z017_SNAPSHOT_SETTLE_US);
    if (result != AMS_ADBMS_RESULT_OK) {
        critical = result;
        goto cleanup;
    }

    result = read_product_groups(m, io, raw_commands, m->raw_codes,
                                 &m->raw_fresh_mask, &m->raw_bad_mask);
    if (result != AMS_ADBMS_RESULT_OK) {
        critical = result;
        goto cleanup;
    }
    m->raw_bad_mask |= invalid_data_mask(m->raw_codes, m->raw_fresh_mask);

    result = session_read(m, io, AMS_ADBMS_CMD_RDSTATC, &packet);
    if (result != AMS_ADBMS_RESULT_OK) {
        critical = result;
        goto cleanup;
    }
    ams_adbms_parse_statc(packet.data, &statc);
    m->ccts = statc.ccts;
    m->ccts_valid = statc.valid && (statc.ccts != 0U);
    if (!m->ccts_valid) {
        note_result(m, AMS_ADBMS_RESULT_CCTS);
        critical = AMS_ADBMS_RESULT_CCTS;
        goto cleanup;
    }

    /* Status D is diagnostic degradation only if the coherent session remains
     * owned. Packet PEC failure withdraws Status-D but does not revoke clean C. */
    result = session_read(m, io, AMS_ADBMS_CMD_RDSTATD, &packet);
    if (result == AMS_ADBMS_RESULT_OK) {
        ams_adbms_parse_statd(packet.data, &m->statd);
        m->statd_valid = true;
    } else {
        sat_inc(&m->statd_fail_count);
        m->statd_valid = false;
        if (optional_failure_is_critical(result)) {
            critical = result;
            goto cleanup;
        }
    }

    result = read_product_groups(m, io, avg_commands, m->avg8_codes,
                                 &m->avg8_fresh_mask, &m->avg8_bad_mask);
    if (result == AMS_ADBMS_RESULT_OK) {
        m->avg8_bad_mask |= invalid_data_mask(m->avg8_codes, m->avg8_fresh_mask);
        avg_complete = true;
        sat_inc(&m->avg8_read_count);
    } else {
        sat_inc(&m->avg8_fail_count);
        m->avg8_fresh_mask = 0U;
        if (optional_failure_is_critical(result)) {
            critical = result;
            goto cleanup;
        }
    }

    result = read_product_groups(m, io, iir_commands, m->iir_codes,
                                 &m->iir_fresh_mask, &m->iir_bad_mask);
    if (result == AMS_ADBMS_RESULT_OK) {
        m->iir_bad_mask |= invalid_data_mask(m->iir_codes, m->iir_fresh_mask);
        iir_complete = true;
        sat_inc(&m->filtered_read_count);
    } else {
        sat_inc(&m->filtered_fail_count);
        m->iir_fresh_mask = 0U;
        ams_cell_image_invalidate_iir(&m->cells);
        if (optional_failure_is_critical(result)) {
            critical = result;
            goto cleanup;
        }
    }

cleanup:
    cleanup = cleanup_snapshot(m, io);
    m->cleanup_error = cleanup;
    if (cleanup != AMS_ADBMS_RESULT_OK) {
        note_result(m, cleanup);
        critical = critical == AMS_ADBMS_RESULT_OK ? AMS_ADBMS_RESULT_CLEANUP : critical;
    }
    if (critical != AMS_ADBMS_RESULT_OK) {
        return critical;
    }

    /* Candidate commits only after clean UNSNAP. */
    ams_cell_image_apply_raw(&m->cells, m->raw_codes,
                             m->raw_fresh_mask, m->raw_bad_mask, now_ms);
    if (avg_complete) {
        ams_cell_image_apply_avg8(&m->cells, m->avg8_codes,
                                  m->avg8_fresh_mask, m->avg8_bad_mask);
    } else {
        m->cells.avg8_usable_mask = 0U;
    }
    if (iir_complete) {
        ams_cell_image_apply_iir(&m->cells, m->iir_codes,
                                 m->iir_fresh_mask, m->iir_bad_mask,
                                 now_ms, true);
    }
    return AMS_ADBMS_RESULT_OK;
}

ams_adbms_result_t ams_adbms_monitor_acquire(ams_adbms_monitor_t *m,
                                             const ams_adbms_monitor_io_t *io,
                                             uint32_t now_ms)
{
    ams_adbms_result_t result;

    if ((m == NULL) || !io_valid(io)) {
        return AMS_ADBMS_RESULT_INVALID;
    }
    if ((m->state != AMS_ADBMS_MONITOR_READY) || !m->initialized ||
        !m->config_verified || !m->startup_post_passed) {
        return AMS_ADBMS_RESULT_INVALID;
    }

    sat_inc(&m->scan_attempt_count);
    m->attempted_ms = now_ms;
    m->first_error = AMS_ADBMS_RESULT_OK;
    m->cleanup_error = AMS_ADBMS_RESULT_OK;
    m->epoch_attempts = 0U;

    result = wake_only(m, io, false);
    if (result == AMS_ADBMS_RESULT_OK) {
        result = monitor_delay(io, AMS_ADBMS_Z017_REFERENCE_PREWAIT_US);
    }
    if (result == AMS_ADBMS_RESULT_OK) {
        /* Current Rev5 branch reissues ADCV every scan. The wrapper performs
         * its own normal wake, matching the frozen oracle. */
        result = wake_and_command(m, io, AMS_ADBMS_CMD_ADCV_Z017);
    }
    if (result == AMS_ADBMS_RESULT_OK) {
        result = monitor_delay(io, AMS_ADBMS_Z017_REDUNDANT_WAIT_US);
    }
    if (result != AMS_ADBMS_RESULT_OK) {
        m->first_error = result;
        m->last_result = result;
        sat_inc(&m->scan_fail_count);
        return result;
    }

    for (uint8_t attempt = 1U; attempt <= AMS_ADBMS_Z017_MAX_EPOCH_ATTEMPTS; ++attempt) {
        m->epoch_attempts = attempt;
        result = read_epoch(m, io, now_ms);
        if (result == AMS_ADBMS_RESULT_OK) {
            sat_inc(&m->scan_success_count);
            m->successful_ms = now_ms;
            m->last_result = result;
            return result;
        }
        if (m->first_error == AMS_ADBMS_RESULT_OK) {
            m->first_error = result;
        }
        if ((result == AMS_ADBMS_RESULT_TRANSPORT_TERMINAL) ||
            !m->config_verified) {
            break;
        }
        if (attempt == 1U) {
            sat_inc(&m->coherent_restart_count);
        }
    }

    sat_inc(&m->coherent_restart_fail_count);
    sat_inc(&m->scan_fail_count);
    m->last_result = result;
    return result;
}

void ams_adbms_monitor_snapshot(const ams_adbms_monitor_t *m,
                                ams_adbms_monitor_snapshot_t *s)
{
    if (s == NULL) {
        return;
    }
    memset(s, 0, sizeof(*s));
    if (m == NULL) {
        return;
    }
#define COPY_FIELD(name) s->name = m->name
    COPY_FIELD(state);
    COPY_FIELD(initialized);
    COPY_FIELD(config_verified);
    COPY_FIELD(startup_post_passed);
    COPY_FIELD(acquisition_live);
    COPY_FIELD(physical_validated);
    COPY_FIELD(init_attempt_count);
    COPY_FIELD(init_fail_count);
    COPY_FIELD(post_run_count);
    COPY_FIELD(post_fail_count);
    COPY_FIELD(post_attempts);
    COPY_FIELD(post_stage);
    COPY_FIELD(post_failed_stage_mask);
    COPY_FIELD(post_unexpected_stage_mask);
    COPY_FIELD(balance_inhibit_attempt_count);
    COPY_FIELD(balance_inhibit_fail_count);
    COPY_FIELD(balance_mute_verified);
    COPY_FIELD(balance_durable_zero_verified);
    COPY_FIELD(scan_attempt_count);
    COPY_FIELD(scan_success_count);
    COPY_FIELD(scan_fail_count);
    COPY_FIELD(coherent_restart_count);
    COPY_FIELD(coherent_restart_fail_count);
    COPY_FIELD(session_expiry_count);
    COPY_FIELD(first_error);
    COPY_FIELD(cleanup_error);
    COPY_FIELD(last_result);
    COPY_FIELD(attempted_ms);
    COPY_FIELD(successful_ms);
    COPY_FIELD(epoch_attempts);
    COPY_FIELD(snapshot_cleanup_required);
    COPY_FIELD(raw_fresh_mask);
    COPY_FIELD(raw_bad_mask);
    COPY_FIELD(avg8_fresh_mask);
    COPY_FIELD(avg8_bad_mask);
    COPY_FIELD(iir_fresh_mask);
    COPY_FIELD(iir_bad_mask);
    COPY_FIELD(ccts);
    COPY_FIELD(ccts_valid);
    COPY_FIELD(statd_valid);
    COPY_FIELD(statd);
    COPY_FIELD(cells);
    COPY_FIELD(temperature);
    COPY_FIELD(recovery);
    COPY_FIELD(counter);
    COPY_FIELD(sticky_diag_faults);
    COPY_FIELD(transport_timeout_count);
    COPY_FIELD(transport_io_count);
    COPY_FIELD(transport_terminal_count);
    COPY_FIELD(pec_fail_count);
    COPY_FIELD(counter_mismatch_count);
    COPY_FIELD(config_mismatch_count);
    COPY_FIELD(ccts_fault_count);
    COPY_FIELD(diagnostic_fail_count);
#undef COPY_FIELD
    memcpy(s->raw_codes, m->raw_codes, sizeof(s->raw_codes));
    memcpy(s->avg8_codes, m->avg8_codes, sizeof(s->avg8_codes));
    memcpy(s->iir_codes, m->iir_codes, sizeof(s->iir_codes));
}

/* Z018: private temperature transactions share the sole String-B owner and
 * counter tracker. No raw command or selectable string is exposed. */
static const ams_adbms_command_t temp_adax[3] = {
 AMS_ADBMS_CMD_ADAX_GPIO1, AMS_ADBMS_CMD_ADAX_GPIO2, AMS_ADBMS_CMD_ADAX_GPIO3};
static const ams_adbms_command_t temp_adax2[3] = {
 AMS_ADBMS_CMD_ADAX2_GPIO1, AMS_ADBMS_CMD_ADAX2_GPIO2, AMS_ADBMS_CMD_ADAX2_GPIO3};
static const ams_adbms_command_t temp_down[3] = {
 AMS_ADBMS_CMD_ADAX_OW_DOWN_GPIO1, AMS_ADBMS_CMD_ADAX_OW_DOWN_GPIO2, AMS_ADBMS_CMD_ADAX_OW_DOWN_GPIO3};
static const ams_adbms_command_t temp_up[3] = {
 AMS_ADBMS_CMD_ADAX_OW_UP_GPIO1, AMS_ADBMS_CMD_ADAX_OW_UP_GPIO2, AMS_ADBMS_CMD_ADAX_OW_UP_GPIO3};

static ams_adbms_result_t temp_prepare(ams_adbms_monitor_t *m,
 const ams_adbms_monitor_io_t *io)
{
 if (!m || !io_valid(io)) return AMS_ADBMS_RESULT_INVALID;
 if (m->state!=AMS_ADBMS_MONITOR_READY || !m->initialized ||
     !m->config_verified || m->temperature.config_cleanup_required)
  return AMS_ADBMS_RESULT_CONFIG_MISMATCH;
 /* Temperature conversions must never run behind an unresolved SNAP. */
 return cleanup_snapshot(m,io);
}

static ams_adbms_result_t temp_select(ams_adbms_monitor_t *m,
 const ams_adbms_monitor_io_t *io, uint8_t sensor)
{
 uint8_t mux=sensor/8U, position=sensor%8U;
 uint8_t address=(uint8_t)((0x4CU+mux)<<1U), data=(uint8_t)(1U<<position);
 uint8_t comm[6]={0x68U,address,0x08U,data,0x19U,0xFFU};
 uint8_t frame[13]={0};
 ams_adbms_packet_t packet;
 ams_adbms_result_t result;
 m->temperature.mux_valid_mask&=(uint8_t)~(1U<<mux);
 result=wake_and_write6(m,io,AMS_ADBMS_CMD_WRCOMM,comm);
 if(result==AMS_ADBMS_RESULT_OK) result=standalone_read(m,io,AMS_ADBMS_CMD_RDCOMM,&packet);
 if(result==AMS_ADBMS_RESULT_OK && memcmp(packet.data,comm,6U)!=0)
  result=AMS_ADBMS_RESULT_CONFIG_MISMATCH;
 if(result==AMS_ADBMS_RESULT_OK) {
  (void)ams_adbms_build_command_frame(AMS_ADBMS_CMD_STCOMM,frame);
  result=wake_only(m,io,false);
  if(result==AMS_ADBMS_RESULT_OK) {
   /* All nine clock bytes are required; a four-byte command is insufficient. */
   result=io->write_b(io->context,frame,sizeof(frame));
   if(result==AMS_ADBMS_RESULT_OK)
    ams_adbms_counter_note_success(&m->counter,AMS_ADBMS_COUNTER_INCREMENT);
   else invalidate_session(m,true);
  }
 }
 if(result==AMS_ADBMS_RESULT_OK) result=standalone_read(m,io,AMS_ADBMS_CMD_RDCOMM,&packet);
 if(result==AMS_ADBMS_RESULT_OK &&
    (packet.data[0]!=0x67U || (packet.data[2]!=0x07U && packet.data[2]!=0x77U)))
  result=AMS_ADBMS_RESULT_DIAGNOSTIC;
 if(result==AMS_ADBMS_RESULT_OK) {
  m->temperature.selected[mux]=position;
  m->temperature.mux_valid_mask|=(uint8_t)(1U<<mux);
 } else m->temperature.mux_valid_mask=0U;
 return result;
}

static ams_adbms_result_t temp_capture(ams_adbms_monitor_t *m,
 const ams_adbms_monitor_io_t *io, uint8_t sensor,
 ams_adbms_command_t conversion, uint32_t wait_us,
 ams_adbms_command_t read_command, int16_t *raw)
{
 unsigned mux=sensor/8U;
 ams_adbms_packet_t packet;
 if(!(m->temperature.mux_valid_mask&(1U<<mux)) ||
     m->temperature.selected[mux]!=sensor%8U) return AMS_ADBMS_RESULT_INVALID;
 ams_adbms_result_t result=wake_and_command(m,io,conversion);
 if(result==AMS_ADBMS_RESULT_OK) result=monitor_delay(io,wait_us);
 if(result==AMS_ADBMS_RESULT_OK) result=standalone_read(m,io,read_command,&packet);
 if(result==AMS_ADBMS_RESULT_OK) {
  *raw=ams_adbms_cell_code(packet.data[2U*mux],packet.data[2U*mux+1U]);
  if(*raw==INT16_MIN || *raw==(int16_t)-1) result=AMS_ADBMS_RESULT_INVALID;
 }
 if(result!=AMS_ADBMS_RESULT_OK) m->temperature.mux_valid_mask=0U;
 return result;
}

ams_adbms_result_t ams_adbms_monitor_temperature(ams_adbms_monitor_t *m,
 const ams_adbms_monitor_io_t *io, uint32_t now_ms)
{
 int16_t raw[AMS_TEMP_COUNT]={0};
 uint32_t captured[AMS_TEMP_COUNT]={0}, received=0U, finish_ms=now_ms;
 uint64_t start=0, now=0;
 if(!m || !io_valid(io)) return AMS_ADBMS_RESULT_INVALID;
 sat_inc(&m->temperature.attempts);
 m->temperature.received_mask=0U;m->temperature.attempted_mask=0U;
 ams_adbms_result_t result=temp_prepare(m,io);
 bool clock_started=false;
 if(result==AMS_ADBMS_RESULT_OK) {result=timed_now(io,&start);clock_started=result==AMS_ADBMS_RESULT_OK;}
 uint8_t position=m->temperature.next_position;
 for(uint8_t mux=0U; mux<3U && result==AMS_ADBMS_RESULT_OK; ++mux) {
  uint8_t sensor=(uint8_t)(mux*8U+position);
  m->temperature.attempted_mask|=1UL<<sensor;
  result=temp_select(m,io,sensor);
  if(result==AMS_ADBMS_RESULT_OK) result=monitor_delay(io,3000U);
  if(result==AMS_ADBMS_RESULT_OK)
   result=temp_capture(m,io,sensor,temp_adax[mux],4000U,AMS_ADBMS_CMD_RDAUXA,&raw[sensor]);
  if(result==AMS_ADBMS_RESULT_OK) result=timed_now(io,&now);
  if(result==AMS_ADBMS_RESULT_OK && (now<start || now-start>UINT32_MAX)) result=AMS_ADBMS_RESULT_CLOCK;
  if(result==AMS_ADBMS_RESULT_OK) {
   captured[sensor]=now_ms+(uint32_t)((now-start)/1000U);
   finish_ms=captured[sensor]; received|=1UL<<sensor;
   result=monitor_delay(io,1000U);
  }
 }
 /* Preserve earlier successful samples even if a later mux/guard fails.
  * Each release advances once; failures never trigger an implicit rescan. */
 m->temperature.next_position=(uint8_t)((position+1U)%8U);
 if(clock_started) {
  ams_adbms_result_t clock_result=timed_now(io,&now);
  if(clock_result==AMS_ADBMS_RESULT_OK && now>=start && now-start<=UINT32_MAX)
   finish_ms=now_ms+(uint32_t)((now-start)/1000U);
  else if(result==AMS_ADBMS_RESULT_OK) result=AMS_ADBMS_RESULT_CLOCK;
 }
 ams_temp_image_apply(&m->temperature.image,raw,received,captured,finish_ms);
 m->temperature.received_mask=received;
 if(result!=AMS_ADBMS_RESULT_OK) {
  sat_inc(&m->temperature.failures); m->temperature.mux_valid_mask=0U;
 }
 m->temperature.result=(int32_t)result;
 return result;
}

static int16_t temp_delta_mv(int16_t a,int16_t b)
{
 int32_t d=(int32_t)a-b; if(d<0) d=-d;
 return (int16_t)((d*150+500)/1000);
}

ams_adbms_result_t ams_adbms_monitor_aux2(ams_adbms_monitor_t *m,
 const ams_adbms_monitor_io_t *io, uint8_t sensor)
{
 if(!m || !io_valid(io) || sensor>=AMS_TEMP_COUNT) return AMS_ADBMS_RESULT_INVALID;
 ams_temp_diagnostic_t *d=&m->temperature.aux2;
 d->valid=false; d->suspect=false; d->sensor=sensor; sat_inc(&d->attempts);
 ams_adbms_result_t result=temp_prepare(m,io);
 if(result==AMS_ADBMS_RESULT_OK) result=temp_select(m,io,sensor);
 if(result==AMS_ADBMS_RESULT_OK) result=monitor_delay(io,3000U);
 if(result==AMS_ADBMS_RESULT_OK) result=temp_capture(m,io,sensor,temp_adax[sensor/8U],4000U,AMS_ADBMS_CMD_RDAUXA,&d->baseline);
 if(result==AMS_ADBMS_RESULT_OK) result=temp_capture(m,io,sensor,temp_adax2[sensor/8U],9000U,AMS_ADBMS_CMD_RDRAXA,&d->secondary);
 if(result==AMS_ADBMS_RESULT_OK) {
  d->valid=true; d->delta_mv=temp_delta_mv(d->baseline,d->secondary);
  d->suspect=d->delta_mv>20;
  if(d->suspect) result=AMS_ADBMS_RESULT_DIAGNOSTIC;
 }
 if(result!=AMS_ADBMS_RESULT_OK) {sat_inc(&d->failures);m->temperature.mux_valid_mask=0U;}
 d->result=(int32_t)result;
 return result;
}

ams_adbms_result_t ams_adbms_monitor_therm_ow(ams_adbms_monitor_t *m,
 const ams_adbms_monitor_io_t *io, uint8_t sensor)
{
 uint8_t production[6], temporary[6];
 if(!m || !io_valid(io) || sensor>=AMS_TEMP_COUNT) return AMS_ADBMS_RESULT_INVALID;
 ams_temp_diagnostic_t *d=&m->temperature.open_wire;
 d->valid=false; d->suspect=false; d->sensor=sensor; sat_inc(&d->attempts);
 d->restore_result=AMS_ADBMS_RESULT_OK;
 ams_adbms_result_t result=temp_prepare(m,io),restore=AMS_ADBMS_RESULT_OK;
 if(result==AMS_ADBMS_RESULT_OK) result=temp_select(m,io,sensor);
 if(result==AMS_ADBMS_RESULT_OK) result=monitor_delay(io,3000U);
 if(result==AMS_ADBMS_RESULT_OK) result=temp_capture(m,io,sensor,temp_adax[sensor/8U],4000U,AMS_ADBMS_CMD_RDAUXA,&d->baseline);
 if(result==AMS_ADBMS_RESULT_OK) {
  ams_adbms_z017_production_cfga(production);memcpy(temporary,production,6U);
  temporary[2]=0xB8U;
  /* Restore debt precedes the uncertain write, including errors after remote acceptance. */
  m->temperature.config_cleanup_required=true; m->config_verified=false;
  result=wake_and_write6(m,io,AMS_ADBMS_CMD_WRCFGA,temporary);
  if(result==AMS_ADBMS_RESULT_OK) result=temp_capture(m,io,sensor,temp_down[sensor/8U],6000U,AMS_ADBMS_CMD_RDAUXA,&d->secondary);
  if(result==AMS_ADBMS_RESULT_OK) result=temp_capture(m,io,sensor,temp_up[sensor/8U],6000U,AMS_ADBMS_CMD_RDAUXA,&d->pullup);
  restore=wake_and_write6(m,io,AMS_ADBMS_CMD_WRCFGA,production);
  if(restore==AMS_ADBMS_RESULT_OK) {
   ams_adbms_packet_t packet;
   restore=standalone_read(m,io,AMS_ADBMS_CMD_RDCFGA,&packet);
   if(restore==AMS_ADBMS_RESULT_OK && !muted_cfga_matches_production(packet.data))
    restore=AMS_ADBMS_RESULT_CONFIG_MISMATCH;
   if(restore==AMS_ADBMS_RESULT_OK) restore=standalone_read(m,io,AMS_ADBMS_CMD_RDCFGB,&packet);
   if(restore==AMS_ADBMS_RESULT_OK && !ams_adbms_z017_config_matches(AMS_ADBMS_CMD_RDCFGB,packet.data))
    restore=AMS_ADBMS_RESULT_CONFIG_MISMATCH;
   if(restore==AMS_ADBMS_RESULT_OK) m->config_verified=true;
  }
  d->restore_result=(int32_t)restore;
  if(restore==AMS_ADBMS_RESULT_OK) m->temperature.config_cleanup_required=false;
  else {
   sat_inc(&d->restore_failures); m->state=AMS_ADBMS_MONITOR_FAULTED;
   m->initialized=false; m->acquisition_live=false; m->config_verified=false;
  }
  if(restore==AMS_ADBMS_RESULT_OK) {
   /* Re-establish routing after any uncertainty; recovery stays diagnostic. */
   ams_adbms_result_t recovery=temp_select(m,io,sensor);
   if(recovery==AMS_ADBMS_RESULT_OK) recovery=monitor_delay(io,3000U);
   if(recovery==AMS_ADBMS_RESULT_OK) recovery=temp_capture(m,io,sensor,temp_adax[sensor/8U],4000U,AMS_ADBMS_CMD_RDAUXA,&d->recovery);
   if(result==AMS_ADBMS_RESULT_OK) result=recovery;
  } else if(result==AMS_ADBMS_RESULT_OK) result=restore;
 }
 if(result==AMS_ADBMS_RESULT_OK && restore==AMS_ADBMS_RESULT_OK) {
  d->valid=true; d->delta_mv=temp_delta_mv(d->secondary,d->baseline);
  d->pullup_delta_mv=temp_delta_mv(d->pullup,d->baseline);
  d->recovery_delta_mv=temp_delta_mv(d->recovery,d->baseline);
  d->suspect=d->delta_mv<10 || d->pullup_delta_mv<10 || d->recovery_delta_mv>20;
  if(d->suspect) result=AMS_ADBMS_RESULT_DIAGNOSTIC;
 }
 if(result!=AMS_ADBMS_RESULT_OK) {sat_inc(&d->failures);m->temperature.mux_valid_mask=0U;}
 d->result=(int32_t)result;
 return result;
}

/* Z019 fingerprints are diagnostics. Exact readback, PEC and counter checks
 * remain authoritative; a hash match never substitutes for byte comparison. */
static uint32_t config_fingerprint(const uint8_t a[6], const uint8_t b[6])
{
    uint32_t hash = 2166136261U;
    hash = (hash ^ 0U) * 16777619U; /* frozen oracle IC index */
    for (unsigned i = 0U; i < 6U; ++i) hash = (hash ^ a[i]) * 16777619U;
    for (unsigned i = 0U; i < 6U; ++i) hash = (hash ^ b[i]) * 16777619U;
    return hash;
}

static void withdraw_images(ams_adbms_monitor_t *m)
{
    ams_cell_image_init(&m->cells);
    memset(&m->temperature.image, 0, sizeof(m->temperature.image));
    m->temperature.image.stale_mask = AMS_TEMP_ALL_MASK;
    m->temperature.mux_valid_mask = 0U;
    m->temperature.next_position = 0U;
    m->temperature.aux2.valid = false;
    m->temperature.open_wire.valid = false;
    m->raw_fresh_mask = m->avg8_fresh_mask = m->iir_fresh_mask = 0U;
    m->raw_bad_mask = m->avg8_bad_mask = m->iir_bad_mask = 0U;
    m->ccts_valid = m->statd_valid = false;
    m->statd.valid = false;
    m->acquisition_live = false;
    m->config_verified = false;
    m->balance_mute_verified = false;
    m->balance_durable_zero_verified = false;
    /* Keep raw numbers and old timestamps for diagnostics, but no validity.
     * Session invalidation deliberately preserves remote UNSNAP debt. */
    invalidate_session(m, true);
}

void ams_adbms_monitor_interrupt(ams_adbms_monitor_t *m, ams_adbms_result_t reason)
{
    if (m == NULL || reason == AMS_ADBMS_RESULT_OK || m->recovery.terminal) return;
    if (!m->recovery.pending) {
        sat_inc(&m->recovery.interruption_count);
        m->recovery.reason = (int32_t)reason;
    }
    m->recovery.pending = true;
    m->recovery.result = (int32_t)reason;
    if (reason == AMS_ADBMS_RESULT_TRANSPORT_TERMINAL ||
        reason == AMS_ADBMS_RESULT_OWNER || reason == AMS_ADBMS_RESULT_IDENTITY) {
        m->recovery.terminal = true;
        m->recovery.pending = false;
    }
    withdraw_images(m);
    m->state = AMS_ADBMS_MONITOR_FAULTED;
}

static ams_adbms_result_t audit_remote(ams_adbms_monitor_t *m,
                                       const ams_adbms_monitor_io_t *io)
{
    ams_adbms_packet_t sid, a, b, pwm;
    uint8_t expected_a[6], expected_b[6];
    static const uint8_t zero[6] = {0};
    ams_adbms_result_t result;
    sat_inc(&m->recovery.audit_count);
    if (!m->counter.known || m->snapshot_cleanup_required ||
        m->temperature.config_cleanup_required || !m->config_verified)
        return AMS_ADBMS_RESULT_CONFIG_MISMATCH;
    result = standalone_read(m, io, AMS_ADBMS_CMD_RDSID, &sid);
    if (result == AMS_ADBMS_RESULT_OK &&
        (!m->recovery.identity_valid || memcmp(sid.data, m->recovery.sid, 6U) != 0))
        result = AMS_ADBMS_RESULT_IDENTITY;
    if (result == AMS_ADBMS_RESULT_OK) result = standalone_read(m, io, AMS_ADBMS_CMD_RDCFGA, &a);
    if (result == AMS_ADBMS_RESULT_OK) result = standalone_read(m, io, AMS_ADBMS_CMD_RDCFGB, &b);
    if (result == AMS_ADBMS_RESULT_OK) {
        ams_adbms_z017_production_cfga(expected_a);
        ams_adbms_z017_production_cfgb(expected_b);
        expected_a[5] |= 0x10U; /* required MUTE_ST */
        m->recovery.expected_fingerprint = config_fingerprint(expected_a, expected_b);
        m->recovery.observed_fingerprint = config_fingerprint(a.data, b.data);
        if (memcmp(a.data, expected_a, 6U) != 0 || memcmp(b.data, expected_b, 6U) != 0)
            result = AMS_ADBMS_RESULT_CONFIG_MISMATCH;
    }
    if (result == AMS_ADBMS_RESULT_OK) result = standalone_read(m, io, AMS_ADBMS_CMD_RDPWMA, &pwm);
    if (result == AMS_ADBMS_RESULT_OK && memcmp(pwm.data, zero, 6U) != 0)
        result = AMS_ADBMS_RESULT_BALANCE_INHIBIT;
    if (result == AMS_ADBMS_RESULT_OK) result = standalone_read(m, io, AMS_ADBMS_CMD_RDPWMB, &pwm);
    if (result == AMS_ADBMS_RESULT_OK && memcmp(pwm.data, zero, 6U) != 0)
        result = AMS_ADBMS_RESULT_BALANCE_INHIBIT;
    return result;
}

static uint32_t sat_add(uint32_t a, uint32_t b)
{
    return b > UINT32_MAX - a ? UINT32_MAX : a + b;
}

static void preserve_recovery_history(ams_adbms_monitor_t *next,
                                      const ams_adbms_monitor_t *old)
{
#define PRESERVE(field) next->field = sat_add(next->field, old->field)
    PRESERVE(init_attempt_count);
    PRESERVE(init_fail_count);
    PRESERVE(post_run_count);
    PRESERVE(post_fail_count);
    PRESERVE(balance_inhibit_attempt_count);
    PRESERVE(balance_inhibit_fail_count);
    PRESERVE(scan_attempt_count);
    PRESERVE(scan_success_count);
    PRESERVE(scan_fail_count);
    PRESERVE(coherent_restart_count);
    PRESERVE(coherent_restart_fail_count);
    PRESERVE(session_expiry_count);
    PRESERVE(avg8_read_count);
    PRESERVE(avg8_fail_count);
    PRESERVE(filtered_read_count);
    PRESERVE(filtered_fail_count);
    PRESERVE(statd_fail_count);
    PRESERVE(transport_timeout_count);
    PRESERVE(transport_io_count);
    PRESERVE(transport_terminal_count);
    PRESERVE(pec_fail_count);
    PRESERVE(counter_mismatch_count);
    PRESERVE(config_mismatch_count);
    PRESERVE(ccts_fault_count);
    PRESERVE(diagnostic_fail_count);
    PRESERVE(counter.mismatch_count);
    PRESERVE(counter.unexpected_reset_count);
    PRESERVE(counter.unknown_count);
    PRESERVE(temperature.attempts);
    PRESERVE(temperature.failures);
    PRESERVE(temperature.aux2.attempts);
    PRESERVE(temperature.aux2.failures);
    PRESERVE(temperature.aux2.restore_failures);
    PRESERVE(temperature.open_wire.attempts);
    PRESERVE(temperature.open_wire.failures);
    PRESERVE(temperature.open_wire.restore_failures);
#undef PRESERVE
    next->sticky_diag_faults |= old->sticky_diag_faults;
    next->post_failed_stage_mask |= old->post_failed_stage_mask;
    next->post_unexpected_stage_mask |= old->post_unexpected_stage_mask;
}

ams_adbms_result_t ams_adbms_monitor_recovery_step(ams_adbms_monitor_t *m,
 const ams_adbms_monitor_io_t *io, uint32_t now_ms)
{
    ams_adbms_result_t result;
    if (m == NULL || !io_valid(io)) return AMS_ADBMS_RESULT_INVALID;
    if (m->recovery.terminal) return (ams_adbms_result_t)m->recovery.result;
    if (!m->recovery.pending) {
        if (!m->initialized || m->state != AMS_ADBMS_MONITOR_READY)
            return AMS_ADBMS_RESULT_INVALID;
        if (m->recovery.continuity_lost) {
            ams_adbms_monitor_interrupt(m, AMS_ADBMS_RESULT_COUNTER);
            return AMS_ADBMS_RESULT_COUNTER;
        }
        result = audit_remote(m, io);
        if (result != AMS_ADBMS_RESULT_OK) ams_adbms_monitor_interrupt(m, result);
        return result;
    }

    /* One repair attempt on the next owner release; no background reinit loop.
     * Clean the remote snapshot before resetting any local initialization state. */
    sat_inc(&m->recovery.attempt_count);
    result = cleanup_snapshot(m, io);
    if (result == AMS_ADBMS_RESULT_OK) {
        ams_adbms_packet_t sid;
        result = standalone_read(m, io, AMS_ADBMS_CMD_RDSID, &sid);
        if (result == AMS_ADBMS_RESULT_OK &&
            (!m->recovery.identity_valid || memcmp(sid.data, m->recovery.sid, 6U) != 0))
            result = AMS_ADBMS_RESULT_IDENTITY;
    }
    if (result == AMS_ADBMS_RESULT_OK) {
        ams_adbms_monitor_t next;
        result = ams_adbms_monitor_initialize(&next, io);
        if (result == AMS_ADBMS_RESULT_OK &&
            (!m->recovery.identity_valid || memcmp(next.recovery.sid, m->recovery.sid, 6U) != 0))
            result = AMS_ADBMS_RESULT_IDENTITY;
        if (result == AMS_ADBMS_RESULT_OK) result = ams_adbms_monitor_acquire(&next, io, now_ms);
        if (result == AMS_ADBMS_RESULT_OK &&
            (next.cells.updated_mask != AMS_ADBMS_Z017_MONITORED_CELL_MASK ||
             next.cells.usable_mask != AMS_ADBMS_Z017_MONITORED_CELL_MASK))
            result = AMS_ADBMS_RESULT_INVALID;
        if (result == AMS_ADBMS_RESULT_OK) result = audit_remote(&next, io);
        preserve_recovery_history(&next, m);
        uint32_t expected_fingerprint = next.recovery.expected_fingerprint;
        uint32_t observed_fingerprint = next.recovery.observed_fingerprint;
        uint32_t audit_count = next.recovery.audit_count;
        next.recovery = m->recovery;
        next.recovery.audit_count = sat_add(next.recovery.audit_count, audit_count);
        if (result == AMS_ADBMS_RESULT_OK) {
            next.recovery.continuity_lost = false;
            next.recovery.expected_fingerprint = expected_fingerprint;
            next.recovery.observed_fingerprint = observed_fingerprint;
            next.recovery.pending = false;
            next.recovery.result = AMS_ADBMS_RESULT_OK;
            sat_inc(&next.recovery.success_count);
            sat_inc(&next.recovery.generation);
            *m = next;
            return AMS_ADBMS_RESULT_OK;
        }
        /* Uncertain reset/config/SNAP must remain visible even when the repair
         * failed. No candidate readings can survive this failed transaction. */
        next.snapshot_cleanup_required |= m->snapshot_cleanup_required;
        next.temperature.config_cleanup_required |= m->temperature.config_cleanup_required;
        *m = next;
    }
    sat_inc(&m->recovery.failure_count);
    m->recovery.pending = false;
    m->recovery.terminal = true;
    m->recovery.result = (int32_t)result;
    withdraw_images(m);
    m->initialized = false;
    m->state = AMS_ADBMS_MONITOR_FAULTED;
    return result;
}
