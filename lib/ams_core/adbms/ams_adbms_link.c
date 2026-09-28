#include <ams_core/ams_adbms_link.h>

#include <string.h>

uint16_t ams_link_pec15(const uint8_t *data, size_t length)
{
    return ams_adbms_pec15(data, length);
}

uint16_t ams_link_pec10(const uint8_t data[6], uint8_t counter)
{
    return ams_adbms_pec10(data, counter);
}

void ams_link_invalidate(ams_link_t *link)
{
    if (link != NULL) {
        memset(link, 0, sizeof(*link));
        ams_adbms_counter_unknown(&link->counter);
        /* Invalidation itself is not an observed protocol event. */
        link->counter.unknown_count = 0U;
    }
}

static bool io_valid(const ams_link_io_t *io)
{
    return (io != NULL) && (io->now_us != NULL) &&
           (io->wake != NULL) && (io->read != NULL);
}

ams_link_result_t ams_link_wake(ams_link_t *link, const ams_link_io_t *io, bool cold)
{
    ams_adbms_result_t result;

    if ((link == NULL) || !io_valid(io)) {
        return AMS_LINK_INVALID;
    }
    /* Wake can traverse sleep/reset; keep awake state and counter knowledge
     * separate. A wake never fabricates counter proof. */
    link->session_valid = false;
    ams_adbms_counter_unknown(&link->counter);
    result = io->wake(io->context, cold);
    if (result != AMS_ADBMS_RESULT_OK) {
        return result;
    }
    result = io->now_us(io->context, &link->last_us);
    if (result != AMS_ADBMS_RESULT_OK) {
        return result == AMS_ADBMS_RESULT_CLOCK ? result : AMS_LINK_CLOCK;
    }
    /* Cold settle exceeds isoSPI's minimum idle timeout; a following
     * standalone read must issue a normal wake. */
    link->session_valid = !cold;
    return AMS_LINK_OK;
}

ams_link_result_t ams_link_read(ams_link_t *link, const ams_link_io_t *io,
                                ams_link_read_t kind, bool guarded,
                                ams_link_sample_t *out)
{
    uint64_t now;
    uint8_t cmd[4] = {0U};
    uint8_t rx[8] = {0U};
    ams_adbms_packet_t packet;
    ams_adbms_result_t result;
    ams_adbms_command_t command;

    if (out == NULL) {
        return AMS_LINK_INVALID;
    }
    memset(out, 0, sizeof(*out));
    if ((link == NULL) || !io_valid(io) ||
        ((kind != AMS_LINK_SID) && (kind != AMS_LINK_CFGA))) {
        return AMS_LINK_INVALID;
    }
    result = io->now_us(io->context, &now);
    if (result != AMS_ADBMS_RESULT_OK) {
        link->session_valid = false;
        ams_adbms_counter_unknown(&link->counter);
        return result == AMS_ADBMS_RESULT_CLOCK ? result : AMS_LINK_CLOCK;
    }
    if (!link->session_valid || (now < link->last_us) ||
        ((now - link->last_us) >= AMS_ADBMS_LINK_GUARD_US)) {
        link->session_valid = false;
        if (guarded) {
            return AMS_LINK_SESSION_EXPIRED;
        }
        result = ams_link_wake(link, io, false);
        if (result != AMS_LINK_OK) {
            return result;
        }
    }

    command = (kind == AMS_LINK_SID) ? AMS_ADBMS_CMD_RDSID : AMS_ADBMS_CMD_RDCFGA;
    if (!ams_adbms_build_command_frame(command, cmd)) {
        return AMS_LINK_INVALID;
    }
    result = io->read(io->context, cmd, rx);
    if (result != AMS_ADBMS_RESULT_OK) {
        link->session_valid = false;
        ams_adbms_counter_unknown(&link->counter);
        return result;
    }
    result = io->now_us(io->context, &now);
    if ((result != AMS_ADBMS_RESULT_OK) || (now < link->last_us)) {
        link->session_valid = false;
        ams_adbms_counter_unknown(&link->counter);
        return result == AMS_ADBMS_RESULT_CLOCK ? result : AMS_LINK_CLOCK;
    }
    link->last_us = now;
    result = ams_adbms_decode_packet(rx, &link->counter, &packet);
    if (result != AMS_ADBMS_RESULT_OK) {
        if (result == AMS_ADBMS_RESULT_PEC) {
            /* A corrupt packet cannot establish whether the remote state
             * remained in the same awake epoch. */
            link->session_valid = false;
            ams_adbms_counter_unknown(&link->counter);
        }
        return result;
    }
    if ((kind == AMS_LINK_SID) && !ams_adbms_sid_is_6830b(packet.data)) {
        return AMS_LINK_IDENTITY;
    }
    memcpy(out->data, packet.data, sizeof(out->data));
    out->counter = packet.counter;
    out->valid = true;
    return AMS_LINK_OK;
}
