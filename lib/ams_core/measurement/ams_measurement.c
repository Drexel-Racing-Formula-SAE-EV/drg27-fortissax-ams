#include <ams_core/ams_measurement.h>

#include <limits.h>
#include <string.h>

_Static_assert(AMS_MEASUREMENT_BUFFER_COUNT == 2U,
               "measurement publication requires exactly two buffers");
_Static_assert(AMS_CELLS_PER_SEGMENT <= 16U,
               "cell usable masks require at most 16 cells per segment");
_Static_assert(AMS_TEMP_SENSORS_PER_SEGMENT <= 32U,
               "temperature usable masks require at most 32 sensors per segment");
_Static_assert(sizeof(ams_measurement_snapshot_t) <=
                   AMS_MEASUREMENT_SNAPSHOT_MAX_BYTES,
               "measurement snapshot exceeded reviewed RAM ceiling");
_Static_assert(sizeof(ams_measurement_store_t) <=
                   AMS_MEASUREMENT_STORE_MAX_BYTES,
               "measurement store exceeded reviewed RAM ceiling");

static uint32_t saturating_increment_u32(uint32_t value)
{
    return (value == UINT32_MAX) ? UINT32_MAX : (value + 1U);
}

static ams_sequence_t sequence_increment(ams_sequence_t value)
{
    value++;
    return (value == 0U) ? 1U : value;
}

static bool store_ready(const ams_measurement_store_t *store)
{
    return (store != NULL) &&
           store->initialized &&
           (store->lock_ops.enter != NULL) &&
           (store->lock_ops.exit != NULL);
}

static ams_measurement_lock_key_t store_lock(ams_measurement_store_t *store)
{
    return store->lock_ops.enter(store->lock_ops.context);
}

static void store_unlock(ams_measurement_store_t *store,
                         ams_measurement_lock_key_t key)
{
    store->lock_ops.exit(store->lock_ops.context, key);
}

bool ams_measurement_store_init(ams_measurement_store_t *store,
                                const ams_measurement_lock_ops_t *lock_ops)
{
    if((store == NULL) ||
       (lock_ops == NULL) ||
       (lock_ops->enter == NULL) ||
       (lock_ops->exit == NULL))
    {
        return false;
    }

    /* Copy before clearing the store so initialization also behaves correctly
     * if the caller keeps the operations descriptor adjacent to store state. */
    ams_measurement_lock_ops_t ops = *lock_ops;

    memset(store, 0, sizeof(*store));
    store->lock_ops = ops;
    store->initialized = true;
    return true;
}

ams_measurement_snapshot_t *ams_measurement_store_begin_write(
    ams_measurement_store_t *store)
{
    if(!store_ready(store))
    {
        return NULL;
    }

    ams_measurement_snapshot_t *snapshot = NULL;
    ams_measurement_lock_key_t key = store_lock(store);

    /*
     * Preserve oracle sequence semantics: an attempted publication reserves
     * and consumes a sequence before availability is known. Failed/aborted
     * publications therefore leave intentional sequence gaps.
     */
    ams_sequence_t sequence = sequence_increment(store->next_sequence);
    store->next_sequence = sequence;

    uint8_t index = store->published ?
                    (uint8_t)(store->published_index ^ 1U) : 0U;

    if(!store->write_in_progress &&
       (index < AMS_MEASUREMENT_BUFFER_COUNT) &&
       (store->reader_count[index] == 0U))
    {
        store->write_index = index;
        store->write_sequence = sequence;
        store->write_in_progress = true;
        snapshot = &store->buffer[index];
    }
    else
    {
        store->publication_drop_count =
            saturating_increment_u32(store->publication_drop_count);
    }

    store_unlock(store, key);
    return snapshot;
}

bool ams_measurement_store_abort_write(ams_measurement_store_t *store,
                                       ams_measurement_snapshot_t *snapshot)
{
    if(!store_ready(store) || (snapshot == NULL))
    {
        return false;
    }

    bool aborted = false;
    ams_measurement_lock_key_t key = store_lock(store);
    uint8_t index = store->write_index;

    if(store->write_in_progress &&
       (index < AMS_MEASUREMENT_BUFFER_COUNT) &&
       (snapshot == &store->buffer[index]))
    {
        store->write_in_progress = false;
        store->write_sequence = 0U;
        aborted = true;
    }

    store_unlock(store, key);
    return aborted;
}

void ams_measurement_snapshot_reset(ams_measurement_snapshot_t *snapshot)
{
    if(snapshot != NULL)
    {
        memset(snapshot, 0, sizeof(*snapshot));
    }
}

ams_sequence_t ams_measurement_store_publish(
    ams_measurement_store_t *store,
    ams_measurement_snapshot_t *snapshot)
{
    if(!store_ready(store) || (snapshot == NULL))
    {
        return 0U;
    }

    ams_sequence_t sequence = 0U;
    ams_measurement_lock_key_t key = store_lock(store);
    uint8_t index = store->write_index;

    if(store->write_in_progress &&
       (index < AMS_MEASUREMENT_BUFFER_COUNT) &&
       (snapshot == &store->buffer[index]) &&
       (store->write_sequence != 0U) &&
       (store->reader_count[index] == 0U))
    {
        sequence = store->write_sequence;
        snapshot->sequence = sequence;
        store->published_index = index;
        store->published = true;
        store->write_in_progress = false;
        store->write_sequence = 0U;
    }

    store_unlock(store, key);
    return sequence;
}

bool ams_measurement_store_copy_latest(ams_measurement_store_t *store,
                                       ams_measurement_snapshot_t *snapshot)
{
    if(!store_ready(store) || (snapshot == NULL))
    {
        return false;
    }

    for(uint8_t i = 0U; i < AMS_MEASUREMENT_BUFFER_COUNT; ++i)
    {
        if(snapshot == &store->buffer[i])
        {
            /* Copying into either internal buffer could corrupt the current
             * publication or a writer-owned inactive buffer. */
            return false;
        }
    }

    bool available = false;
    uint8_t index = 0U;
    ams_measurement_lock_key_t key = store_lock(store);

    if(store->published &&
       (store->published_index < AMS_MEASUREMENT_BUFFER_COUNT))
    {
        index = store->published_index;

        if(store->reader_count[index] != UINT16_MAX)
        {
            store->reader_count[index]++;
            available = true;
        }
    }

    store_unlock(store, key);

    if(!available)
    {
        return false;
    }

    /*
     * Intentionally outside the lock. reader_count[index] pins the source
     * buffer so the sole writer cannot select it for reuse during this copy.
     */
    memcpy(snapshot, &store->buffer[index], sizeof(*snapshot));

    key = store_lock(store);
    if(store->reader_count[index] > 0U)
    {
        store->reader_count[index]--;
    }
    store_unlock(store, key);

    return true;
}

size_t ams_measurement_snapshot_size_bytes(void)
{
    return sizeof(ams_measurement_snapshot_t);
}

size_t ams_measurement_store_size_bytes(void)
{
    return sizeof(ams_measurement_store_t);
}

uint32_t ams_measurement_buffer_count(void)
{
    return AMS_MEASUREMENT_BUFFER_COUNT;
}
