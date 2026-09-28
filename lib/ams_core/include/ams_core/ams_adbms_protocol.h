#ifndef AMS_CORE_ADBMS_PROTOCOL_H_
#define AMS_CORE_ADBMS_PROTOCOL_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AMS_ADBMS_PACKET_DATA_BYTES 6U
#define AMS_ADBMS_PACKET_BYTES 8U
#define AMS_ADBMS_COMMAND_BYTES 2U
#define AMS_ADBMS_COMMAND_FRAME_BYTES 4U
#define AMS_ADBMS_WRITE_FRAME_BYTES 12U
#define AMS_ADBMS_COMMAND_COUNTER_MAX 63U

/* Z-017 is intentionally one ADBMS6830B on String B. */
#define AMS_ADBMS_Z017_STRING_B 1U
#define AMS_ADBMS_Z017_IC_COUNT 1U
#define AMS_ADBMS_Z017_MONITORED_CELL_COUNT 15U
#define AMS_ADBMS_Z017_MONITORED_CELL_MASK 0x7FFFU
#define AMS_ADBMS_Z017_SESSION_GUARD_US 3000U

/* Exact v2.6.27 production images for BENCH Validation 1-SMB. */
#define AMS_ADBMS_Z017_CFGA0 0x81U
#define AMS_ADBMS_Z017_CFGA1 0x00U
#define AMS_ADBMS_Z017_CFGA2 0x00U
#define AMS_ADBMS_Z017_CFGA3 0xFFU
#define AMS_ADBMS_Z017_CFGA4 0x03U
#define AMS_ADBMS_Z017_CFGA5 0x03U
#define AMS_ADBMS_Z017_CFGB0 0x71U
#define AMS_ADBMS_Z017_CFGB1 0x52U
#define AMS_ADBMS_Z017_CFGB2 0x46U
#define AMS_ADBMS_Z017_CFGB3 0x00U
#define AMS_ADBMS_Z017_CFGB4 0x00U
#define AMS_ADBMS_Z017_CFGB5 0x00U

typedef enum {
    AMS_ADBMS_RESULT_OK = 0,
    AMS_ADBMS_RESULT_INVALID,
    AMS_ADBMS_RESULT_TRANSPORT_TIMEOUT,
    AMS_ADBMS_RESULT_TRANSPORT_IO,
    AMS_ADBMS_RESULT_TRANSPORT_TERMINAL,
    AMS_ADBMS_RESULT_CLOCK,
    AMS_ADBMS_RESULT_OWNER,
    AMS_ADBMS_RESULT_PEC,
    AMS_ADBMS_RESULT_COUNTER,
    AMS_ADBMS_RESULT_IDENTITY,
    AMS_ADBMS_RESULT_SESSION_EXPIRED,
    AMS_ADBMS_RESULT_CONFIG_MISMATCH,
    AMS_ADBMS_RESULT_DIAGNOSTIC,
    AMS_ADBMS_RESULT_CCTS,
    AMS_ADBMS_RESULT_DATA,
    AMS_ADBMS_RESULT_CLEANUP,
    AMS_ADBMS_RESULT_BALANCE_INHIBIT,
} ams_adbms_result_t;

typedef enum {
    AMS_ADBMS_COUNTER_NONE = 0,
    AMS_ADBMS_COUNTER_INCREMENT,
    AMS_ADBMS_COUNTER_RESET,
} ams_adbms_counter_effect_t;

typedef enum {
    AMS_ADBMS_CMD_SRST = 0,
    AMS_ADBMS_CMD_RDSID,
    AMS_ADBMS_CMD_WRCFGA,
    AMS_ADBMS_CMD_WRCFGB,
    AMS_ADBMS_CMD_RDCFGA,
    AMS_ADBMS_CMD_RDCFGB,
    AMS_ADBMS_CMD_CLRFLAG,
    AMS_ADBMS_CMD_CLOVUV,
    AMS_ADBMS_CMD_ADAX_ALL,
    AMS_ADBMS_CMD_ADCV_Z017,
    AMS_ADBMS_CMD_ADCV_BASELINE,
    AMS_ADBMS_CMD_SNAP,
    AMS_ADBMS_CMD_UNSNAP,
    /* Z017 startup-safe balancing subset. UNMUTE is deliberately not admitted. */
    AMS_ADBMS_CMD_MUTE,
    AMS_ADBMS_CMD_WRPWMA,
    AMS_ADBMS_CMD_WRPWMB,
    AMS_ADBMS_CMD_RDPWMA,
    AMS_ADBMS_CMD_RDPWMB,
    AMS_ADBMS_CMD_RDSTATA,
    AMS_ADBMS_CMD_RDSTATB,
    AMS_ADBMS_CMD_RDSTATC,
    AMS_ADBMS_CMD_RDSTATCERR,
    AMS_ADBMS_CMD_RDSTATD,
    AMS_ADBMS_CMD_RDSTATE,
    AMS_ADBMS_CMD_RDCVA,
    AMS_ADBMS_CMD_RDCVB,
    AMS_ADBMS_CMD_RDCVC,
    AMS_ADBMS_CMD_RDCVD,
    AMS_ADBMS_CMD_RDCVE,
    AMS_ADBMS_CMD_RDCVF,
    AMS_ADBMS_CMD_RDACA,
    AMS_ADBMS_CMD_RDACB,
    AMS_ADBMS_CMD_RDACC,
    AMS_ADBMS_CMD_RDACD,
    AMS_ADBMS_CMD_RDACE,
    AMS_ADBMS_CMD_RDACF,
    AMS_ADBMS_CMD_RDFCA,
    AMS_ADBMS_CMD_RDFCB,
    AMS_ADBMS_CMD_RDFCC,
    AMS_ADBMS_CMD_RDFCD,
    AMS_ADBMS_CMD_RDFCE,
    AMS_ADBMS_CMD_RDFCF,
    AMS_ADBMS_CMD_WRCOMM,
    AMS_ADBMS_CMD_RDCOMM,
    AMS_ADBMS_CMD_STCOMM,
    AMS_ADBMS_CMD_RDAUXA,
    AMS_ADBMS_CMD_RDRAXA,
    AMS_ADBMS_CMD_ADAX_GPIO1,
    AMS_ADBMS_CMD_ADAX_GPIO2,
    AMS_ADBMS_CMD_ADAX_GPIO3,
    AMS_ADBMS_CMD_ADAX2_GPIO1,
    AMS_ADBMS_CMD_ADAX2_GPIO2,
    AMS_ADBMS_CMD_ADAX2_GPIO3,
    AMS_ADBMS_CMD_ADAX_OW_DOWN_GPIO1,
    AMS_ADBMS_CMD_ADAX_OW_DOWN_GPIO2,
    AMS_ADBMS_CMD_ADAX_OW_DOWN_GPIO3,
    AMS_ADBMS_CMD_ADAX_OW_UP_GPIO1,
    AMS_ADBMS_CMD_ADAX_OW_UP_GPIO2,
    AMS_ADBMS_CMD_ADAX_OW_UP_GPIO3,
    AMS_ADBMS_CMD_COUNT
} ams_adbms_command_t;

typedef enum {
    AMS_ADBMS_COMMAND_ONLY = 0,
    AMS_ADBMS_COMMAND_READ,
    AMS_ADBMS_COMMAND_WRITE6,
    AMS_ADBMS_COMMAND_STCOMM,
} ams_adbms_command_kind_t;

typedef struct {
    uint8_t byte0;
    uint8_t byte1;
    ams_adbms_command_kind_t kind;
    ams_adbms_counter_effect_t counter_effect;
} ams_adbms_command_info_t;

typedef struct {
    bool known;
    uint8_t expected;
    uint32_t mismatch_count;
    uint32_t unexpected_reset_count;
    uint32_t unknown_count;
} ams_adbms_counter_tracker_t;

typedef struct {
    uint8_t data[AMS_ADBMS_PACKET_DATA_BYTES];
    uint8_t counter;
    bool pec_valid;
    bool counter_valid;
} ams_adbms_packet_t;

typedef struct {
    bool valid;
    uint16_t cs_fault_mask;
    uint16_t ccts;
    bool va_ov;
    bool va_uv;
    bool vd_ov;
    bool vd_uv;
    bool ced;
    bool cmed;
    bool sed;
    bool smed;
    bool vdel;
    bool vde;
    bool comp;
    bool spiflt;
    bool sleep;
    bool thsd;
    bool tmodchk;
    bool oscchk;
} ams_adbms_statc_t;

typedef struct {
    bool valid;
    uint16_t cell_uv_mask;
    uint16_t cell_ov_mask;
    uint8_t osc_counter;
} ams_adbms_statd_t;

uint16_t ams_adbms_pec15(const uint8_t *data, size_t length);
uint16_t ams_adbms_pec10(const uint8_t data[AMS_ADBMS_PACKET_DATA_BYTES], uint8_t counter);

bool ams_adbms_command_info(ams_adbms_command_t command, ams_adbms_command_info_t *info);
bool ams_adbms_build_command_frame(ams_adbms_command_t command,
                                   uint8_t frame[AMS_ADBMS_COMMAND_FRAME_BYTES]);
bool ams_adbms_build_write_frame(ams_adbms_command_t command,
                                 const uint8_t data[AMS_ADBMS_PACKET_DATA_BYTES],
                                 uint8_t frame[AMS_ADBMS_WRITE_FRAME_BYTES]);

void ams_adbms_counter_reset(ams_adbms_counter_tracker_t *tracker);
void ams_adbms_counter_unknown(ams_adbms_counter_tracker_t *tracker);
uint8_t ams_adbms_counter_next(uint8_t current);
void ams_adbms_counter_note_success(ams_adbms_counter_tracker_t *tracker,
                                    ams_adbms_counter_effect_t effect);
ams_adbms_result_t ams_adbms_counter_observe(ams_adbms_counter_tracker_t *tracker,
                                             uint8_t observed,
                                             bool pec_valid);

ams_adbms_result_t ams_adbms_decode_packet(
    const uint8_t response[AMS_ADBMS_PACKET_BYTES],
    ams_adbms_counter_tracker_t *tracker,
    ams_adbms_packet_t *packet);

void ams_adbms_z017_production_cfga(uint8_t data[AMS_ADBMS_PACKET_DATA_BYTES]);
void ams_adbms_z017_production_cfgb(uint8_t data[AMS_ADBMS_PACKET_DATA_BYTES]);
void ams_adbms_z017_clear_flag_payload(uint8_t data[AMS_ADBMS_PACKET_DATA_BYTES]);
void ams_adbms_z017_clear_ovuv_payload(uint8_t data[AMS_ADBMS_PACKET_DATA_BYTES]);

bool ams_adbms_z017_config_matches(ams_adbms_command_t command,
                                   const uint8_t actual[AMS_ADBMS_PACKET_DATA_BYTES]);
bool ams_adbms_sid_is_6830b(const uint8_t sid[AMS_ADBMS_PACKET_DATA_BYTES]);

void ams_adbms_parse_statc(const uint8_t data[AMS_ADBMS_PACKET_DATA_BYTES],
                           ams_adbms_statc_t *status);
void ams_adbms_parse_statd(const uint8_t data[AMS_ADBMS_PACKET_DATA_BYTES],
                           ams_adbms_statd_t *status);

int16_t ams_adbms_cell_code(const uint8_t lo, const uint8_t hi);
bool ams_adbms_cell_code_to_mv(int16_t code, uint16_t *mv);

#ifdef __cplusplus
}
#endif

#endif /* AMS_CORE_ADBMS_PROTOCOL_H_ */
