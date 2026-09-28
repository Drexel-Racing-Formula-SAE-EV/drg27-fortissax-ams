#include <ams_core/ams_adbms_protocol.h>

#include <limits.h>
#include <string.h>

static const ams_adbms_command_info_t command_table[AMS_ADBMS_CMD_COUNT] = {
    [AMS_ADBMS_CMD_SRST]         = {0x00U, 0x27U, AMS_ADBMS_COMMAND_ONLY, AMS_ADBMS_COUNTER_RESET},
    [AMS_ADBMS_CMD_RDSID]        = {0x00U, 0x2CU, AMS_ADBMS_COMMAND_READ, AMS_ADBMS_COUNTER_NONE},
    [AMS_ADBMS_CMD_WRCFGA]       = {0x00U, 0x01U, AMS_ADBMS_COMMAND_WRITE6, AMS_ADBMS_COUNTER_INCREMENT},
    [AMS_ADBMS_CMD_WRCFGB]       = {0x00U, 0x24U, AMS_ADBMS_COMMAND_WRITE6, AMS_ADBMS_COUNTER_INCREMENT},
    [AMS_ADBMS_CMD_RDCFGA]       = {0x00U, 0x02U, AMS_ADBMS_COMMAND_READ, AMS_ADBMS_COUNTER_NONE},
    [AMS_ADBMS_CMD_RDCFGB]       = {0x00U, 0x26U, AMS_ADBMS_COMMAND_READ, AMS_ADBMS_COUNTER_NONE},
    [AMS_ADBMS_CMD_CLRFLAG]      = {0x07U, 0x17U, AMS_ADBMS_COMMAND_WRITE6, AMS_ADBMS_COUNTER_INCREMENT},
    [AMS_ADBMS_CMD_CLOVUV]       = {0x07U, 0x15U, AMS_ADBMS_COMMAND_WRITE6, AMS_ADBMS_COUNTER_INCREMENT},
    [AMS_ADBMS_CMD_ADAX_ALL]     = {0x04U, 0x10U, AMS_ADBMS_COMMAND_ONLY, AMS_ADBMS_COUNTER_INCREMENT},
    /* RD_ON | CONTINUOUS | DCP_OFF | RSTF_OFF | OW_OFF + documented 0x60 base. */
    [AMS_ADBMS_CMD_ADCV_Z017]    = {0x03U, 0xE0U, AMS_ADBMS_COMMAND_ONLY, AMS_ADBMS_COUNTER_INCREMENT},
    /* Startup baseline: RD_ON | SINGLE | DCP_OFF | RSTF_ON | OW_OFF. */
    [AMS_ADBMS_CMD_ADCV_BASELINE]= {0x03U, 0x64U, AMS_ADBMS_COMMAND_ONLY, AMS_ADBMS_COUNTER_INCREMENT},
    [AMS_ADBMS_CMD_SNAP]         = {0x00U, 0x2DU, AMS_ADBMS_COMMAND_ONLY, AMS_ADBMS_COUNTER_INCREMENT},
    [AMS_ADBMS_CMD_UNSNAP]       = {0x00U, 0x2FU, AMS_ADBMS_COMMAND_ONLY, AMS_ADBMS_COUNTER_INCREMENT},
    [AMS_ADBMS_CMD_MUTE]         = {0x00U, 0x28U, AMS_ADBMS_COMMAND_ONLY, AMS_ADBMS_COUNTER_INCREMENT},
    [AMS_ADBMS_CMD_WRPWMA]       = {0x00U, 0x20U, AMS_ADBMS_COMMAND_WRITE6, AMS_ADBMS_COUNTER_INCREMENT},
    [AMS_ADBMS_CMD_WRPWMB]       = {0x00U, 0x21U, AMS_ADBMS_COMMAND_WRITE6, AMS_ADBMS_COUNTER_INCREMENT},
    [AMS_ADBMS_CMD_RDPWMA]       = {0x00U, 0x22U, AMS_ADBMS_COMMAND_READ, AMS_ADBMS_COUNTER_NONE},
    [AMS_ADBMS_CMD_RDPWMB]       = {0x00U, 0x23U, AMS_ADBMS_COMMAND_READ, AMS_ADBMS_COUNTER_NONE},
    [AMS_ADBMS_CMD_RDSTATA]      = {0x00U, 0x30U, AMS_ADBMS_COMMAND_READ, AMS_ADBMS_COUNTER_NONE},
    [AMS_ADBMS_CMD_RDSTATB]      = {0x00U, 0x31U, AMS_ADBMS_COMMAND_READ, AMS_ADBMS_COUNTER_NONE},
    [AMS_ADBMS_CMD_RDSTATC]      = {0x00U, 0x32U, AMS_ADBMS_COMMAND_READ, AMS_ADBMS_COUNTER_NONE},
    [AMS_ADBMS_CMD_RDSTATCERR]   = {0x00U, 0x72U, AMS_ADBMS_COMMAND_READ, AMS_ADBMS_COUNTER_NONE},
    [AMS_ADBMS_CMD_RDSTATD]      = {0x00U, 0x33U, AMS_ADBMS_COMMAND_READ, AMS_ADBMS_COUNTER_NONE},
    [AMS_ADBMS_CMD_RDSTATE]      = {0x00U, 0x34U, AMS_ADBMS_COMMAND_READ, AMS_ADBMS_COUNTER_NONE},
    [AMS_ADBMS_CMD_RDCVA]        = {0x00U, 0x04U, AMS_ADBMS_COMMAND_READ, AMS_ADBMS_COUNTER_NONE},
    [AMS_ADBMS_CMD_RDCVB]        = {0x00U, 0x06U, AMS_ADBMS_COMMAND_READ, AMS_ADBMS_COUNTER_NONE},
    [AMS_ADBMS_CMD_RDCVC]        = {0x00U, 0x08U, AMS_ADBMS_COMMAND_READ, AMS_ADBMS_COUNTER_NONE},
    [AMS_ADBMS_CMD_RDCVD]        = {0x00U, 0x0AU, AMS_ADBMS_COMMAND_READ, AMS_ADBMS_COUNTER_NONE},
    [AMS_ADBMS_CMD_RDCVE]        = {0x00U, 0x09U, AMS_ADBMS_COMMAND_READ, AMS_ADBMS_COUNTER_NONE},
    [AMS_ADBMS_CMD_RDCVF]        = {0x00U, 0x0BU, AMS_ADBMS_COMMAND_READ, AMS_ADBMS_COUNTER_NONE},
    [AMS_ADBMS_CMD_RDACA]        = {0x00U, 0x44U, AMS_ADBMS_COMMAND_READ, AMS_ADBMS_COUNTER_NONE},
    [AMS_ADBMS_CMD_RDACB]        = {0x00U, 0x46U, AMS_ADBMS_COMMAND_READ, AMS_ADBMS_COUNTER_NONE},
    [AMS_ADBMS_CMD_RDACC]        = {0x00U, 0x48U, AMS_ADBMS_COMMAND_READ, AMS_ADBMS_COUNTER_NONE},
    [AMS_ADBMS_CMD_RDACD]        = {0x00U, 0x4AU, AMS_ADBMS_COMMAND_READ, AMS_ADBMS_COUNTER_NONE},
    [AMS_ADBMS_CMD_RDACE]        = {0x00U, 0x49U, AMS_ADBMS_COMMAND_READ, AMS_ADBMS_COUNTER_NONE},
    [AMS_ADBMS_CMD_RDACF]        = {0x00U, 0x4BU, AMS_ADBMS_COMMAND_READ, AMS_ADBMS_COUNTER_NONE},
    [AMS_ADBMS_CMD_RDFCA]        = {0x00U, 0x12U, AMS_ADBMS_COMMAND_READ, AMS_ADBMS_COUNTER_NONE},
    [AMS_ADBMS_CMD_RDFCB]        = {0x00U, 0x13U, AMS_ADBMS_COMMAND_READ, AMS_ADBMS_COUNTER_NONE},
    [AMS_ADBMS_CMD_RDFCC]        = {0x00U, 0x14U, AMS_ADBMS_COMMAND_READ, AMS_ADBMS_COUNTER_NONE},
    [AMS_ADBMS_CMD_RDFCD]        = {0x00U, 0x15U, AMS_ADBMS_COMMAND_READ, AMS_ADBMS_COUNTER_NONE},
    [AMS_ADBMS_CMD_RDFCE]        = {0x00U, 0x16U, AMS_ADBMS_COMMAND_READ, AMS_ADBMS_COUNTER_NONE},
    [AMS_ADBMS_CMD_RDFCF]        = {0x00U, 0x17U, AMS_ADBMS_COMMAND_READ, AMS_ADBMS_COUNTER_NONE},
    [AMS_ADBMS_CMD_WRCOMM] = {0x07U, 0x21U, AMS_ADBMS_COMMAND_WRITE6, AMS_ADBMS_COUNTER_INCREMENT},
    [AMS_ADBMS_CMD_RDCOMM] = {0x07U, 0x22U, AMS_ADBMS_COMMAND_READ, AMS_ADBMS_COUNTER_NONE},
    [AMS_ADBMS_CMD_STCOMM] = {0x07U, 0x23U, AMS_ADBMS_COMMAND_STCOMM, AMS_ADBMS_COUNTER_INCREMENT},
    [AMS_ADBMS_CMD_RDAUXA] = {0x00U, 0x19U, AMS_ADBMS_COMMAND_READ, AMS_ADBMS_COUNTER_NONE},
    [AMS_ADBMS_CMD_RDRAXA] = {0x00U, 0x1CU, AMS_ADBMS_COMMAND_READ, AMS_ADBMS_COUNTER_NONE},
    [AMS_ADBMS_CMD_ADAX_GPIO1] = {0x04U, 0x11U, AMS_ADBMS_COMMAND_ONLY, AMS_ADBMS_COUNTER_INCREMENT},
    [AMS_ADBMS_CMD_ADAX_GPIO2] = {0x04U, 0x12U, AMS_ADBMS_COMMAND_ONLY, AMS_ADBMS_COUNTER_INCREMENT},
    [AMS_ADBMS_CMD_ADAX_GPIO3] = {0x04U, 0x13U, AMS_ADBMS_COMMAND_ONLY, AMS_ADBMS_COUNTER_INCREMENT},
    [AMS_ADBMS_CMD_ADAX2_GPIO1] = {0x04U, 0x01U, AMS_ADBMS_COMMAND_ONLY, AMS_ADBMS_COUNTER_INCREMENT},
    [AMS_ADBMS_CMD_ADAX2_GPIO2] = {0x04U, 0x02U, AMS_ADBMS_COMMAND_ONLY, AMS_ADBMS_COUNTER_INCREMENT},
    [AMS_ADBMS_CMD_ADAX2_GPIO3] = {0x04U, 0x03U, AMS_ADBMS_COMMAND_ONLY, AMS_ADBMS_COUNTER_INCREMENT},
    [AMS_ADBMS_CMD_ADAX_OW_DOWN_GPIO1] = {0x05U, 0x11U, AMS_ADBMS_COMMAND_ONLY, AMS_ADBMS_COUNTER_INCREMENT},
    [AMS_ADBMS_CMD_ADAX_OW_DOWN_GPIO2] = {0x05U, 0x12U, AMS_ADBMS_COMMAND_ONLY, AMS_ADBMS_COUNTER_INCREMENT},
    [AMS_ADBMS_CMD_ADAX_OW_DOWN_GPIO3] = {0x05U, 0x13U, AMS_ADBMS_COMMAND_ONLY, AMS_ADBMS_COUNTER_INCREMENT},
    [AMS_ADBMS_CMD_ADAX_OW_UP_GPIO1] = {0x05U, 0x91U, AMS_ADBMS_COMMAND_ONLY, AMS_ADBMS_COUNTER_INCREMENT},
    [AMS_ADBMS_CMD_ADAX_OW_UP_GPIO2] = {0x05U, 0x92U, AMS_ADBMS_COMMAND_ONLY, AMS_ADBMS_COUNTER_INCREMENT},
    [AMS_ADBMS_CMD_ADAX_OW_UP_GPIO3] = {0x05U, 0x93U, AMS_ADBMS_COMMAND_ONLY, AMS_ADBMS_COUNTER_INCREMENT},
};

static void increment_u32_sat(uint32_t *value)
{
    if ((value != NULL) && (*value != UINT32_MAX)) {
        (*value)++;
    }
}

uint16_t ams_adbms_pec15(const uint8_t *data, size_t length)
{
    uint16_t remainder = 16U;

    if ((data == NULL) && (length != 0U)) {
        return 0U;
    }
    for (size_t i = 0U; i < length; ++i) {
        remainder ^= (uint16_t)((uint16_t)data[i] << 7U);
        for (uint8_t bit = 0U; bit < 8U; ++bit) {
            remainder = (uint16_t)((remainder << 1U) ^
                ((remainder & 0x4000U) != 0U ? 0x4599U : 0U));
        }
    }
    return (uint16_t)(remainder << 1U);
}

uint16_t ams_adbms_pec10(const uint8_t data[AMS_ADBMS_PACKET_DATA_BYTES], uint8_t counter)
{
    uint16_t remainder = 16U;

    if (data == NULL) {
        return 0U;
    }
    for (uint8_t i = 0U; i < AMS_ADBMS_PACKET_DATA_BYTES; ++i) {
        remainder ^= (uint16_t)((uint16_t)data[i] << 2U);
        for (uint8_t bit = 0U; bit < 8U; ++bit) {
            remainder = (uint16_t)((remainder << 1U) ^
                ((remainder & 0x0200U) != 0U ? 0x008FU : 0U));
        }
    }
    remainder ^= (uint16_t)((uint16_t)(counter & 0x3FU) << 4U);
    for (uint8_t bit = 0U; bit < 6U; ++bit) {
        remainder = (uint16_t)((remainder << 1U) ^
            ((remainder & 0x0200U) != 0U ? 0x008FU : 0U));
    }
    return (uint16_t)(remainder & 0x03FFU);
}

bool ams_adbms_command_info(ams_adbms_command_t command, ams_adbms_command_info_t *info)
{
    if ((info == NULL) || ((unsigned)command >= (unsigned)AMS_ADBMS_CMD_COUNT)) {
        return false;
    }
    *info = command_table[(unsigned)command];
    return true;
}

bool ams_adbms_build_command_frame(ams_adbms_command_t command,
                                   uint8_t frame[AMS_ADBMS_COMMAND_FRAME_BYTES])
{
    ams_adbms_command_info_t info;
    uint16_t pec;

    if ((frame == NULL) || !ams_adbms_command_info(command, &info)) {
        return false;
    }
    frame[0] = info.byte0;
    frame[1] = info.byte1;
    pec = ams_adbms_pec15(frame, AMS_ADBMS_COMMAND_BYTES);
    frame[2] = (uint8_t)(pec >> 8U);
    frame[3] = (uint8_t)pec;
    return true;
}

bool ams_adbms_build_write_frame(ams_adbms_command_t command,
                                 const uint8_t data[AMS_ADBMS_PACKET_DATA_BYTES],
                                 uint8_t frame[AMS_ADBMS_WRITE_FRAME_BYTES])
{
    ams_adbms_command_info_t info;
    uint16_t data_pec;

    if ((frame == NULL) || (data == NULL) ||
        !ams_adbms_command_info(command, &info) ||
        (info.kind != AMS_ADBMS_COMMAND_WRITE6)) {
        return false;
    }
    if (!ams_adbms_build_command_frame(command, frame)) {
        return false;
    }
    memcpy(&frame[4], data, AMS_ADBMS_PACKET_DATA_BYTES);
    /* Oracle write packets use PEC10 with command-counter bits forced to zero. */
    data_pec = ams_adbms_pec10(data, 0U);
    frame[10] = (uint8_t)(data_pec >> 8U);
    frame[11] = (uint8_t)data_pec;
    return true;
}

uint8_t ams_adbms_counter_next(uint8_t current)
{
    current &= 0x3FU;
    if ((current == 0U) || (current >= AMS_ADBMS_COMMAND_COUNTER_MAX)) {
        return 1U;
    }
    return (uint8_t)(current + 1U);
}

void ams_adbms_counter_reset(ams_adbms_counter_tracker_t *tracker)
{
    if (tracker == NULL) {
        return;
    }
    memset(tracker, 0, sizeof(*tracker));
    tracker->known = true;
    tracker->expected = 0U;
}

void ams_adbms_counter_unknown(ams_adbms_counter_tracker_t *tracker)
{
    if (tracker == NULL) {
        return;
    }
    tracker->known = false;
    tracker->expected = 0U;
    increment_u32_sat(&tracker->unknown_count);
}

void ams_adbms_counter_note_success(ams_adbms_counter_tracker_t *tracker,
                                    ams_adbms_counter_effect_t effect)
{
    if (tracker == NULL) {
        return;
    }
    if (effect == AMS_ADBMS_COUNTER_RESET) {
        tracker->known = true;
        tracker->expected = 0U;
    } else if ((effect == AMS_ADBMS_COUNTER_INCREMENT) && tracker->known) {
        tracker->expected = ams_adbms_counter_next(tracker->expected);
    }
}

ams_adbms_result_t ams_adbms_counter_observe(ams_adbms_counter_tracker_t *tracker,
                                             uint8_t observed,
                                             bool pec_valid)
{
    observed &= 0x3FU;
    if ((tracker == NULL) || !pec_valid) {
        return AMS_ADBMS_RESULT_INVALID;
    }
    if (!tracker->known) {
        tracker->known = true;
        tracker->expected = observed;
        return AMS_ADBMS_RESULT_OK;
    }
    if ((tracker->expected != 0U) && (observed == 0U)) {
        increment_u32_sat(&tracker->unexpected_reset_count);
    }
    if (tracker->expected != observed) {
        increment_u32_sat(&tracker->mismatch_count);
        tracker->expected = observed;
        return AMS_ADBMS_RESULT_COUNTER;
    }
    return AMS_ADBMS_RESULT_OK;
}

ams_adbms_result_t ams_adbms_decode_packet(
    const uint8_t response[AMS_ADBMS_PACKET_BYTES],
    ams_adbms_counter_tracker_t *tracker,
    ams_adbms_packet_t *packet)
{
    uint8_t counter;
    uint16_t received;
    uint16_t calculated;
    ams_adbms_result_t result;

    if ((response == NULL) || (tracker == NULL) || (packet == NULL)) {
        return AMS_ADBMS_RESULT_INVALID;
    }
    memset(packet, 0, sizeof(*packet));
    counter = (uint8_t)(response[6] >> 2U);
    received = (uint16_t)(((uint16_t)(response[6] & 0x03U) << 8U) | response[7]);
    calculated = ams_adbms_pec10(response, counter);
    packet->counter = counter;
    if (received != calculated) {
        return AMS_ADBMS_RESULT_PEC;
    }
    packet->pec_valid = true;
    result = ams_adbms_counter_observe(tracker, counter, true);
    if (result != AMS_ADBMS_RESULT_OK) {
        return result;
    }
    packet->counter_valid = true;
    memcpy(packet->data, response, AMS_ADBMS_PACKET_DATA_BYTES);
    return AMS_ADBMS_RESULT_OK;
}

void ams_adbms_z017_production_cfga(uint8_t data[AMS_ADBMS_PACKET_DATA_BYTES])
{
    static const uint8_t image[AMS_ADBMS_PACKET_DATA_BYTES] = {
        AMS_ADBMS_Z017_CFGA0, AMS_ADBMS_Z017_CFGA1, AMS_ADBMS_Z017_CFGA2,
        AMS_ADBMS_Z017_CFGA3, AMS_ADBMS_Z017_CFGA4, AMS_ADBMS_Z017_CFGA5
    };
    if (data != NULL) {
        memcpy(data, image, sizeof(image));
    }
}

void ams_adbms_z017_production_cfgb(uint8_t data[AMS_ADBMS_PACKET_DATA_BYTES])
{
    static const uint8_t image[AMS_ADBMS_PACKET_DATA_BYTES] = {
        AMS_ADBMS_Z017_CFGB0, AMS_ADBMS_Z017_CFGB1, AMS_ADBMS_Z017_CFGB2,
        AMS_ADBMS_Z017_CFGB3, AMS_ADBMS_Z017_CFGB4, AMS_ADBMS_Z017_CFGB5
    };
    if (data != NULL) {
        memcpy(data, image, sizeof(image));
    }
}

void ams_adbms_z017_clear_flag_payload(uint8_t data[AMS_ADBMS_PACKET_DATA_BYTES])
{
    static const uint8_t image[AMS_ADBMS_PACKET_DATA_BYTES] = {
        0xFFU, 0xFFU, 0x00U, 0x00U, 0xFFU, 0xDFU
    };
    if (data != NULL) {
        memcpy(data, image, sizeof(image));
    }
}

void ams_adbms_z017_clear_ovuv_payload(uint8_t data[AMS_ADBMS_PACKET_DATA_BYTES])
{
    static const uint8_t image[AMS_ADBMS_PACKET_DATA_BYTES] = {
        0xFFU, 0xFFU, 0xFFU, 0xFFU, 0x00U, 0x00U
    };
    if (data != NULL) {
        memcpy(data, image, sizeof(image));
    }
}

bool ams_adbms_z017_config_matches(ams_adbms_command_t command,
                                   const uint8_t actual[AMS_ADBMS_PACKET_DATA_BYTES])
{
    uint8_t expected[AMS_ADBMS_PACKET_DATA_BYTES];

    if (actual == NULL) {
        return false;
    }
    if (command == AMS_ADBMS_CMD_RDCFGA) {
        ams_adbms_z017_production_cfga(expected);
    } else if (command == AMS_ADBMS_CMD_RDCFGB) {
        ams_adbms_z017_production_cfgb(expected);
    } else {
        return false;
    }
    return memcmp(expected, actual, sizeof(expected)) == 0;
}

bool ams_adbms_sid_is_6830b(const uint8_t sid[AMS_ADBMS_PACKET_DATA_BYTES])
{
    return (sid != NULL) && (((sid[1] >> 1U) & 0x3FU) == 3U);
}

void ams_adbms_parse_statc(const uint8_t data[AMS_ADBMS_PACKET_DATA_BYTES],
                           ams_adbms_statc_t *status)
{
    uint16_t ct;
    if (status == NULL) {
        return;
    }
    memset(status, 0, sizeof(*status));
    if (data == NULL) {
        return;
    }
    status->valid = true;
    status->cs_fault_mask = (uint16_t)data[0] | ((uint16_t)data[1] << 8U);
    ct = (uint16_t)(((uint16_t)(data[2] & 0x1FU) << 6U) |
                    ((uint16_t)(data[3] & 0xFCU) >> 2U));
    status->ccts = (uint16_t)(((ct << 2U) | (uint16_t)(data[3] & 0x03U)) & 0x1FFFU);
    status->va_ov = ((data[4] >> 7U) & 1U) != 0U;
    status->va_uv = ((data[4] >> 6U) & 1U) != 0U;
    status->vd_ov = ((data[4] >> 5U) & 1U) != 0U;
    status->vd_uv = ((data[4] >> 4U) & 1U) != 0U;
    status->ced = ((data[4] >> 3U) & 1U) != 0U;
    status->cmed = ((data[4] >> 2U) & 1U) != 0U;
    status->sed = ((data[4] >> 1U) & 1U) != 0U;
    status->smed = (data[4] & 1U) != 0U;
    status->vdel = ((data[5] >> 7U) & 1U) != 0U;
    status->vde = ((data[5] >> 6U) & 1U) != 0U;
    status->comp = ((data[5] >> 5U) & 1U) != 0U;
    status->spiflt = ((data[5] >> 4U) & 1U) != 0U;
    status->sleep = ((data[5] >> 3U) & 1U) != 0U;
    status->thsd = ((data[5] >> 2U) & 1U) != 0U;
    status->tmodchk = ((data[5] >> 1U) & 1U) != 0U;
    status->oscchk = (data[5] & 1U) != 0U;
}

void ams_adbms_parse_statd(const uint8_t data[AMS_ADBMS_PACKET_DATA_BYTES],
                           ams_adbms_statd_t *status)
{
    if (status == NULL) {
        return;
    }
    memset(status, 0, sizeof(*status));
    if (data == NULL) {
        return;
    }
    status->valid = true;
    for (uint8_t cell = 0U; cell < 16U; ++cell) {
        uint8_t byte = data[cell / 4U];
        uint8_t shift = (uint8_t)((cell % 4U) * 2U);
        if (((byte >> shift) & 1U) != 0U) {
            status->cell_uv_mask |= (uint16_t)(1U << cell);
        }
        if (((byte >> (shift + 1U)) & 1U) != 0U) {
            status->cell_ov_mask |= (uint16_t)(1U << cell);
        }
    }
    status->osc_counter = data[5];
}

int16_t ams_adbms_cell_code(const uint8_t lo, const uint8_t hi)
{
    return (int16_t)((uint16_t)lo | ((uint16_t)hi << 8U));
}

bool ams_adbms_cell_code_to_mv(int16_t code, uint16_t *mv)
{
    int32_t microvolts;
    int32_t millivolts;

    if ((mv == NULL) || (code == INT16_MIN) || (code == INT16_MAX) || (code == (int16_t)-1)) {
        return false;
    }
    /* Exact oracle transfer: V = (code + 10000) * 150 uV, rounded to mV. */
    microvolts = ((int32_t)code + 10000) * 150;
    if (microvolts < 0) {
        return false;
    }
    millivolts = (microvolts + 500) / 1000;
    if ((millivolts < 0) || (millivolts > (int32_t)UINT16_MAX)) {
        return false;
    }
    *mv = (uint16_t)millivolts;
    return true;
}
