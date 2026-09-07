#ifndef AMS_MEASUREMENT_H_
#define AMS_MEASUREMENT_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <ams_core/ams_core_config.h>
#include <ams_core/ams_core_types.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Z-007 preserves the frozen measurement-publication architecture:
 *
 * - one writer owns publication;
 * - two fixed snapshot buffers are used;
 * - readers pin the published buffer only for the duration of their copy;
 * - the writer may only reuse an unpinned inactive buffer;
 * - publication sequence zero is reserved for "no publication";
 * - publication attempts that cannot safely obtain the inactive buffer drop
 *   rather than blocking or overwriting a reader-pinned snapshot.
 *
 * The measurement payload is platform-neutral. Hardware acquisition and the
 * current-window integration algorithm are connected in later migration steps.
 */

#define AMS_MEASUREMENT_BUFFER_COUNT        2U
#define AMS_MEASUREMENT_SNAPSHOT_MAX_BYTES 2048U
#define AMS_MEASUREMENT_STORE_MAX_BYTES    4096U

#define AMS_MEAS_VALID_VOLTAGE       (1U << 0U)
#define AMS_MEAS_VALID_TEMPERATURE   (1U << 1U)
#define AMS_MEAS_VALID_CURRENT       (1U << 2U)
#define AMS_MEAS_BALANCE_RECOVERED   (1U << 3U)
#define AMS_MEAS_BALANCE_WAS_ACTIVE  (1U << 4U)

/*
 * Immutable current-window result carried by a measurement epoch.
 *
 * Z-008 will port the producer/integration algorithm. Z-007 only freezes the
 * publication representation needed by downstream snapshot consumers.
 * selected_range == 0 is the published mixed/unknown state. Z-008 must keep
 * producer initialization state separate so a window that becomes mixed cannot
 * silently return to single-range before rotation. Consumers must not treat
 * uncertainty_mA == 0 or AMS_CURRENT_UNCERTAINTY_UNKNOWN as proof of calibrated
 * current quality.
 */
typedef struct
{
    ams_sequence_t sequence;
    ams_time_ms_t start_tick;
    ams_time_ms_t end_tick;
    uint32_t sample_count;
    uint32_t invalid_sample_count;
    ams_time_ms_t latest_sample_tick;
    float latest_A;
    float filtered_A;
    float average_A;
    float rms_A;
    float min_A;
    float max_A;
    double charge_As;
    double absolute_charge_As;
    double total_charge_As;
    double total_absolute_charge_As;
    uint32_t total_invalid_sample_count;
    uint32_t calibration_id;
    ams_current_uncertainty_t uncertainty_mA;
    uint8_t selected_range;
    bool calibration_record_confident;
    bool valid;
} ams_current_window_t;

/*
 * Coherent immutable measurement epoch.
 *
 * RAW cell values remain the safety-authoritative voltage representation.
 * AVG8/IIR values are observational/estimator candidates from the same epoch
 * and must never silently replace RAW voltage authority.
 */
typedef struct
{
    ams_sequence_t sequence;
    ams_time_ms_t acquisition_start_tick;
    ams_time_ms_t voltage_complete_tick;
    ams_time_ms_t publication_tick;

    uint16_t cell_mv[AMS_PHYSICAL_SEGMENT_COUNT][AMS_CELLS_PER_SEGMENT];
    uint32_t cell_age_ms[AMS_PHYSICAL_SEGMENT_COUNT][AMS_CELLS_PER_SEGMENT];
    uint16_t cell_usable_mask[AMS_PHYSICAL_SEGMENT_COUNT];

    uint16_t cell_avg8_mv[AMS_PHYSICAL_SEGMENT_COUNT][AMS_CELLS_PER_SEGMENT];
    uint16_t cell_iir_mv[AMS_PHYSICAL_SEGMENT_COUNT][AMS_CELLS_PER_SEGMENT];
    uint16_t cell_avg8_usable_mask[AMS_PHYSICAL_SEGMENT_COUNT];
    uint16_t cell_iir_usable_mask[AMS_PHYSICAL_SEGMENT_COUNT];

    int16_t temp_deci_c[AMS_PHYSICAL_SEGMENT_COUNT]
                       [AMS_TEMP_SENSORS_PER_SEGMENT];
    uint32_t temp_age_ms[AMS_PHYSICAL_SEGMENT_COUNT]
                        [AMS_TEMP_SENSORS_PER_SEGMENT];
    uint32_t temp_usable_mask[AMS_PHYSICAL_SEGMENT_COUNT];

    ams_current_window_t current;
    uint16_t balancing_mask[AMS_PHYSICAL_SEGMENT_COUNT];
    uint32_t balance_off_ms;
    uint32_t validity_flags;
} ams_measurement_snapshot_t;

/*
 * The metadata lock is intentionally injected.
 *
 * enter()/exit() surround only bounded store metadata transitions. They must
 * provide mutual exclusion plus the acquire/release memory-ordering semantics
 * needed for the publication handoff. Snapshot payload copies occur outside
 * the lock while reader pinning protects the source buffer. The key type is
 * wide enough for ordinary platform critical-section tokens without pulling
 * any kernel API into the portable core.
 */
typedef uintptr_t ams_measurement_lock_key_t;

typedef struct
{
    void *context;
    ams_measurement_lock_key_t (*enter)(void *context);
    void (*exit)(void *context, ams_measurement_lock_key_t key);
} ams_measurement_lock_ops_t;

typedef struct
{
    ams_measurement_snapshot_t buffer[AMS_MEASUREMENT_BUFFER_COUNT];
    uint16_t reader_count[AMS_MEASUREMENT_BUFFER_COUNT];
    uint32_t publication_drop_count;
    ams_sequence_t next_sequence;
    ams_sequence_t write_sequence;
    uint8_t published_index;
    uint8_t write_index;
    bool published;
    bool write_in_progress;
    bool initialized;
    ams_measurement_lock_ops_t lock_ops;
} ams_measurement_store_t;

/*
 * Initialize a statically allocated store. The lock callbacks are mandatory;
 * pass explicit no-op callbacks only in single-threaded tests.
 */
bool ams_measurement_store_init(ams_measurement_store_t *store,
                                const ams_measurement_lock_ops_t *lock_ops);

/*
 * Reserve the inactive buffer for the sole writer.
 *
 * Returns NULL instead of blocking if the inactive buffer is reader-pinned or
 * another write is already active. Every attempt consumes a nonzero sequence
 * number, matching the frozen oracle's observable sequence-gap behavior.
 */
ams_measurement_snapshot_t *ams_measurement_store_begin_write(
    ams_measurement_store_t *store);

/*
 * Recover from an abandoned writer transaction without publishing it.
 * The reserved sequence is intentionally not reused.
 */
bool ams_measurement_store_abort_write(ams_measurement_store_t *store,
                                       ams_measurement_snapshot_t *snapshot);

/* Clear a writer-owned snapshot before populating a new coherent epoch. */
void ams_measurement_snapshot_reset(ams_measurement_snapshot_t *snapshot);

/* Publish the currently reserved writer buffer and return its nonzero sequence. */
ams_sequence_t ams_measurement_store_publish(
    ams_measurement_store_t *store,
    ams_measurement_snapshot_t *snapshot);

/*
 * Pin, copy, and unpin the current publication.
 *
 * The potentially large structure copy deliberately occurs outside the
 * metadata lock. The writer cannot reuse that buffer until the reader count
 * returns to zero. The destination must be external to the store buffers.
 */
bool ams_measurement_store_copy_latest(ams_measurement_store_t *store,
                                       ams_measurement_snapshot_t *snapshot);

/* Link/build-contract helpers; they do not mutate store state. */
size_t ams_measurement_snapshot_size_bytes(void);
size_t ams_measurement_store_size_bytes(void);
uint32_t ams_measurement_buffer_count(void);

#ifdef __cplusplus
}
#endif

#endif /* AMS_MEASUREMENT_H_ */
