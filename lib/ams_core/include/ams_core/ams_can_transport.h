#ifndef AMS_CAN_TRANSPORT_H_
#define AMS_CAN_TRANSPORT_H_
#include <ams_core/ams_can_tx_scheduler.h>
#include <stdatomic.h>

#define AMS_CAN_OUTSTANDING 3U
#define AMS_CAN_EVENT_CAPACITY 16U
#define AMS_CAN_EVENT_BUDGET 8U
#define AMS_CAN_SEND_BUDGET 3U

enum { AMS_CAN_LOSS=1U, AMS_CAN_BUS_OFF=2U, AMS_CAN_SEND_ERROR=4U,
       AMS_CAN_COMPLETION_ERROR=8U, AMS_CAN_EXHAUSTED=16U,
       AMS_CAN_BAD_FRAME=32U, AMS_CAN_TIME_ERROR=64U, AMS_CAN_TX_TIMEOUT=128U };
typedef enum { AMS_CAN_SEND_ACCEPTED, AMS_CAN_SEND_BUSY,
               AMS_CAN_SEND_FAILED } ams_can_send_result_t;
typedef struct {
 uint64_t cookie;
 ams_can_tx_token_t token;
 uint32_t submitted_ms;
 bool used;
} ams_can_outstanding_t;
typedef struct {
 uint64_t cookie;
 uint32_t tick;
 bool success, rx, remote, fd;
 ams_can_tx_frame_t frame;
} ams_can_event_t;
/* Send must be bounded/nonblocking and copy the frame and cookie by value.
 * Completion may occur before send returns. Never retain a pointer to a reused
 * slot as callback identity. FAILED/BUSY must mean no request was accepted. */
typedef ams_can_send_result_t (*ams_can_send_fn)(void *,
 const ams_can_tx_frame_t *, uint64_t cookie);
typedef struct {
 ams_can_tx_scheduler_t scheduler;
 ams_can_outstanding_t outstanding[AMS_CAN_OUTSTANDING];
 ams_can_event_t events[AMS_CAN_EVENT_CAPACITY];
 atomic_bool event_lock;
 atomic_uint async_faults;
 unsigned head, count;
 uint64_t next_cookie, service_sequence;
 uint32_t faults, history, submitted, completed, rejected_callbacks;
 uint32_t rx_accepted, rx_rejected, last_rx_ms, last_service_ms;
 uint32_t published_generation, controller_epoch;
 ams_can_tx_frame_t last_rx;
 bool inhibited, tx_enabled, terminal, recovery_pending, rx_seen;
} ams_can_transport_t;
/* Initialize once before callbacks/owner start. All APIs except capture and
 * fault are owner-only; the eventual platform adapter must enforce ownership. */
void ams_can_transport_init(ams_can_transport_t *, bool isolated_tx);
void ams_can_transport_fault(ams_can_transport_t *, unsigned faults);
bool ams_can_transport_capture(ams_can_transport_t *, const ams_can_event_t *);
/* encoded_ms is the clock used by the codec, not the later enqueue time. */
bool ams_can_transport_publish(ams_can_transport_t *, uint32_t now, uint32_t encoded_ms,
 const ams_can_tx_frame_t *current_diagnostic);
/* Fixed budgets; no retries on a busy or failed send in this service release. */
bool ams_can_transport_service(ams_can_transport_t *, uint32_t now,
 ams_can_send_fn send, void *context);
/* Begin BEFORE establishing physical readiness/settlement. Any later callback
 * fault invalidates that proof, including a repeated instance of the same fault.
 * Beginning a new attempt never itself enables TX. */
bool ams_can_transport_recovery_begin(ams_can_transport_t *);
/* Owner-only copied RX diagnostic; expiry/fault/recovery withdraw validity. */
bool ams_can_transport_copy_rx(ams_can_transport_t *, uint32_t now,
 ams_can_tx_frame_t *);
/* After begin, caller must prove readiness AND settlement of old requests.
 * Resets queued generations (never replays them); preserves cookie identity and
 * sticky history. Async faults racing recovery remain latched. */
bool ams_can_transport_recover(ams_can_transport_t *, bool ready,
 bool callbacks_settled);
bool ams_can_bench_frame_allowed(const ams_can_tx_frame_t *);
#endif
