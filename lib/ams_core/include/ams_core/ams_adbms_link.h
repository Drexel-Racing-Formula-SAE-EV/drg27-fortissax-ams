#ifndef AMS_ADBMS_LINK_H
#define AMS_ADBMS_LINK_H

#include <ams_core/ams_adbms_protocol.h>

#include <stdbool.h>
#include <stdint.h>

/* Z-016 temporary 6822 eval jumper: one physical SMB, String B only.
 * There is intentionally no direction, raw command or write selection API. */
#define AMS_ADBMS_LINK_STRING 1U
#define AMS_ADBMS_LINK_IC_COUNT 1U
#define AMS_ADBMS_LINK_GUARD_US 3000U

typedef ams_adbms_result_t ams_link_result_t;
#define AMS_LINK_OK              AMS_ADBMS_RESULT_OK
#define AMS_LINK_INVALID         AMS_ADBMS_RESULT_INVALID
#define AMS_LINK_IO              AMS_ADBMS_RESULT_TRANSPORT_IO
#define AMS_LINK_TIMEOUT         AMS_ADBMS_RESULT_TRANSPORT_TIMEOUT
#define AMS_LINK_TERMINAL        AMS_ADBMS_RESULT_TRANSPORT_TERMINAL
#define AMS_LINK_CLOCK           AMS_ADBMS_RESULT_CLOCK
#define AMS_LINK_PEC             AMS_ADBMS_RESULT_PEC
#define AMS_LINK_COUNTER         AMS_ADBMS_RESULT_COUNTER
#define AMS_LINK_IDENTITY        AMS_ADBMS_RESULT_IDENTITY
#define AMS_LINK_SESSION_EXPIRED AMS_ADBMS_RESULT_SESSION_EXPIRED

typedef enum { AMS_LINK_SID, AMS_LINK_CFGA } ams_link_read_t;

typedef struct {
    void *context;
    ams_adbms_result_t (*now_us)(void *, uint64_t *);
    ams_adbms_result_t (*wake)(void *, bool cold);
    ams_adbms_result_t (*read)(void *, const uint8_t command[4], uint8_t response[8]);
} ams_link_io_t;

typedef struct {
    bool session_valid;
    ams_adbms_counter_tracker_t counter;
    uint64_t last_us;
} ams_link_t;

typedef struct {
    uint8_t data[6];
    uint8_t counter;
    bool valid;
} ams_link_sample_t;

/* Compatibility names remain wrappers around the single protocol PEC source. */
uint16_t ams_link_pec15(const uint8_t *data, size_t length);
uint16_t ams_link_pec10(const uint8_t data[6], uint8_t counter);
void ams_link_invalidate(ams_link_t *link);
ams_link_result_t ams_link_wake(ams_link_t *, const ams_link_io_t *, bool cold);
/* guarded=true forbids re-waking inside a coherent session. On any error the
 * output is invalid/zeroed; transport success alone never validates a sample. */
ams_link_result_t ams_link_read(ams_link_t *, const ams_link_io_t *,
                                ams_link_read_t, bool guarded, ams_link_sample_t *);
#endif
