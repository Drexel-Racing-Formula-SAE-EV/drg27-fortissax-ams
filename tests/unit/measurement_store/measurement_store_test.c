#include <ams_core/ams_measurement.h>

#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition)                                                       \
    do                                                                         \
    {                                                                          \
        if(!(condition))                                                       \
        {                                                                      \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            return false;                                                      \
        }                                                                      \
    } while(0)

static ams_measurement_lock_key_t noop_enter(void *context)
{
    (void)context;
    return 0U;
}

static void noop_exit(void *context, ams_measurement_lock_key_t key)
{
    (void)context;
    (void)key;
}

static const ams_measurement_lock_ops_t noop_lock_ops = {
    .context = NULL,
    .enter = noop_enter,
    .exit = noop_exit,
};

static void fill_snapshot(ams_measurement_snapshot_t *snapshot,
                          uint16_t marker)
{
    ams_measurement_snapshot_reset(snapshot);

    snapshot->acquisition_start_tick = (uint32_t)marker * 10U;
    snapshot->voltage_complete_tick = snapshot->acquisition_start_tick + 3U;
    snapshot->publication_tick = snapshot->voltage_complete_tick + 2U;
    snapshot->validity_flags = AMS_MEAS_VALID_VOLTAGE |
                               AMS_MEAS_VALID_TEMPERATURE |
                               AMS_MEAS_VALID_CURRENT;
    snapshot->current.latest_A = (float)marker;
    snapshot->current.average_A = (float)marker;
    snapshot->current.uncertainty_mA = (uint16_t)(marker + 1U);
    snapshot->current.selected_range = 1U;
    snapshot->current.calibration_id = (uint32_t)marker + 100U;
    snapshot->current.calibration_record_confident = true;
    snapshot->current.valid = true;

    for(size_t seg = 0U; seg < AMS_PHYSICAL_SEGMENT_COUNT; ++seg)
    {
        snapshot->cell_usable_mask[seg] = 0x7FFFU;
        snapshot->cell_avg8_usable_mask[seg] = 0x7FFFU;
        snapshot->cell_iir_usable_mask[seg] = 0x7FFFU;
        snapshot->temp_usable_mask[seg] = 0x00FFFFFFU;
        snapshot->balancing_mask[seg] = (uint16_t)(marker ^ (uint16_t)seg);

        for(size_t cell = 0U; cell < AMS_CELLS_PER_SEGMENT; ++cell)
        {
            uint16_t value = (uint16_t)(marker + seg + cell);
            snapshot->cell_mv[seg][cell] = value;
            snapshot->cell_avg8_mv[seg][cell] = (uint16_t)(value + 1U);
            snapshot->cell_iir_mv[seg][cell] = (uint16_t)(value + 2U);
            snapshot->cell_age_ms[seg][cell] = (uint32_t)(seg * 100U + cell);
        }

        for(size_t temp = 0U; temp < AMS_TEMP_SENSORS_PER_SEGMENT; ++temp)
        {
            snapshot->temp_deci_c[seg][temp] =
                (int16_t)((int32_t)marker + (int32_t)seg + (int32_t)temp);
            snapshot->temp_age_ms[seg][temp] =
                (uint32_t)(seg * 100U + temp);
        }
    }
}

static bool snapshot_matches_marker(const ams_measurement_snapshot_t *snapshot,
                                    uint16_t marker)
{
    CHECK(snapshot != NULL);
    CHECK(snapshot->sequence != 0U);
    CHECK(snapshot->current.latest_A == (float)marker);
    CHECK(snapshot->current.average_A == (float)marker);
    CHECK(snapshot->current.uncertainty_mA == (uint16_t)(marker + 1U));
    CHECK(snapshot->current.calibration_id == (uint32_t)marker + 100U);

    for(size_t seg = 0U; seg < AMS_PHYSICAL_SEGMENT_COUNT; ++seg)
    {
        CHECK(snapshot->cell_usable_mask[seg] == 0x7FFFU);
        CHECK(snapshot->temp_usable_mask[seg] == 0x00FFFFFFU);

        for(size_t cell = 0U; cell < AMS_CELLS_PER_SEGMENT; ++cell)
        {
            uint16_t value = (uint16_t)(marker + seg + cell);
            CHECK(snapshot->cell_mv[seg][cell] == value);
            CHECK(snapshot->cell_avg8_mv[seg][cell] == (uint16_t)(value + 1U));
            CHECK(snapshot->cell_iir_mv[seg][cell] == (uint16_t)(value + 2U));
        }

        for(size_t temp = 0U; temp < AMS_TEMP_SENSORS_PER_SEGMENT; ++temp)
        {
            CHECK(snapshot->temp_deci_c[seg][temp] ==
                  (int16_t)((int32_t)marker + (int32_t)seg + (int32_t)temp));
        }
    }

    return true;
}

static bool test_init_contract(void)
{
    ams_measurement_store_t store;
    ams_measurement_snapshot_t copy;

    memset(&store, 0xA5, sizeof(store));

    CHECK(!ams_measurement_store_init(NULL, &noop_lock_ops));
    CHECK(!ams_measurement_store_init(&store, NULL));

    ams_measurement_lock_ops_t bad_ops = noop_lock_ops;
    bad_ops.exit = NULL;
    CHECK(!ams_measurement_store_init(&store, &bad_ops));

    CHECK(ams_measurement_store_init(&store, &noop_lock_ops));
    CHECK(store.initialized);
    CHECK(!store.published);
    CHECK(!store.write_in_progress);
    CHECK(store.publication_drop_count == 0U);
    CHECK(store.next_sequence == 0U);
    CHECK(!ams_measurement_store_copy_latest(&store, &copy));

    CHECK(ams_measurement_buffer_count() == 2U);
    CHECK(ams_measurement_snapshot_size_bytes() <=
          AMS_MEASUREMENT_SNAPSHOT_MAX_BYTES);
    CHECK(ams_measurement_store_size_bytes() <=
          AMS_MEASUREMENT_STORE_MAX_BYTES);

    return true;
}

static bool test_publish_and_copy(void)
{
    ams_measurement_store_t store;
    ams_measurement_snapshot_t copy;

    CHECK(ams_measurement_store_init(&store, &noop_lock_ops));

    ams_measurement_snapshot_t *write =
        ams_measurement_store_begin_write(&store);
    CHECK(write != NULL);
    CHECK(store.write_index == 0U);

    fill_snapshot(write, 100U);
    CHECK(ams_measurement_store_publish(&store, write) == 1U);
    CHECK(store.published_index == 0U);
    CHECK(ams_measurement_store_copy_latest(&store, &copy));
    CHECK(copy.sequence == 1U);
    CHECK(snapshot_matches_marker(&copy, 100U));

    write = ams_measurement_store_begin_write(&store);
    CHECK(write != NULL);
    CHECK(store.write_index == 1U);
    fill_snapshot(write, 200U);
    CHECK(ams_measurement_store_publish(&store, write) == 2U);
    CHECK(store.published_index == 1U);
    CHECK(ams_measurement_store_copy_latest(&store, &copy));
    CHECK(copy.sequence == 2U);
    CHECK(snapshot_matches_marker(&copy, 200U));

    return true;
}

static bool test_drop_abort_and_sequence_gaps(void)
{
    ams_measurement_store_t store;
    ams_measurement_snapshot_t copy;

    CHECK(ams_measurement_store_init(&store, &noop_lock_ops));

    ams_measurement_snapshot_t *write =
        ams_measurement_store_begin_write(&store);
    CHECK(write != NULL);
    fill_snapshot(write, 10U);

    /* A second writer attempt is dropped and consumes sequence 2. */
    CHECK(ams_measurement_store_begin_write(&store) == NULL);
    CHECK(store.publication_drop_count == 1U);
    CHECK(store.next_sequence == 2U);

    CHECK(ams_measurement_store_abort_write(&store, write));
    CHECK(!store.write_in_progress);

    write = ams_measurement_store_begin_write(&store);
    CHECK(write != NULL);
    fill_snapshot(write, 30U);
    CHECK(ams_measurement_store_publish(&store, write) == 3U);
    CHECK(ams_measurement_store_copy_latest(&store, &copy));
    CHECK(copy.sequence == 3U);
    CHECK(snapshot_matches_marker(&copy, 30U));

    return true;
}

static bool test_reader_pin_blocks_reuse(void)
{
    ams_measurement_store_t store;

    CHECK(ams_measurement_store_init(&store, &noop_lock_ops));

    ams_measurement_snapshot_t *write =
        ams_measurement_store_begin_write(&store);
    CHECK(write != NULL);
    fill_snapshot(write, 1U);
    CHECK(ams_measurement_store_publish(&store, write) == 1U);
    CHECK(store.published_index == 0U);

    /* Model a reader that has pinned publication buffer 0. */
    store.reader_count[0] = 1U;

    write = ams_measurement_store_begin_write(&store);
    CHECK(write != NULL);
    CHECK(store.write_index == 1U);
    fill_snapshot(write, 2U);
    CHECK(ams_measurement_store_publish(&store, write) == 2U);
    CHECK(store.published_index == 1U);

    /* The next inactive buffer is the still-pinned old publication. */
    CHECK(ams_measurement_store_begin_write(&store) == NULL);
    CHECK(store.publication_drop_count == 1U);

    store.reader_count[0] = 0U;

    write = ams_measurement_store_begin_write(&store);
    CHECK(write != NULL);
    CHECK(store.write_index == 0U);
    fill_snapshot(write, 4U);

    /* Sequence 3 belonged to the dropped attempt; next publication is 4. */
    CHECK(ams_measurement_store_publish(&store, write) == 4U);

    return true;
}

static bool test_sequence_wrap_and_saturation(void)
{
    ams_measurement_store_t store;
    ams_measurement_snapshot_t copy;

    CHECK(ams_measurement_store_init(&store, &noop_lock_ops));

    store.next_sequence = UINT32_MAX;
    ams_measurement_snapshot_t *write =
        ams_measurement_store_begin_write(&store);
    CHECK(write != NULL);
    fill_snapshot(write, 7U);
    CHECK(ams_measurement_store_publish(&store, write) == 1U);
    CHECK(ams_measurement_store_copy_latest(&store, &copy));
    CHECK(copy.sequence == 1U);

    store.publication_drop_count = UINT32_MAX;
    write = ams_measurement_store_begin_write(&store);
    CHECK(write != NULL);
    CHECK(ams_measurement_store_begin_write(&store) == NULL);
    CHECK(store.publication_drop_count == UINT32_MAX);
    CHECK(ams_measurement_store_abort_write(&store, write));

    store.reader_count[store.published_index] = UINT16_MAX;
    CHECK(!ams_measurement_store_copy_latest(&store, &copy));
    store.reader_count[store.published_index] = 0U;

    return true;
}

static bool test_abort_preserves_last_publication(void)
{
    ams_measurement_store_t store;
    ams_measurement_snapshot_t copy;

    CHECK(ams_measurement_store_init(&store, &noop_lock_ops));

    ams_measurement_snapshot_t *write =
        ams_measurement_store_begin_write(&store);
    CHECK(write != NULL);
    fill_snapshot(write, 111U);
    CHECK(ams_measurement_store_publish(&store, write) == 1U);

    write = ams_measurement_store_begin_write(&store);
    CHECK(write != NULL);
    fill_snapshot(write, 222U);
    CHECK(ams_measurement_store_abort_write(&store, write));

    CHECK(ams_measurement_store_copy_latest(&store, &copy));
    CHECK(copy.sequence == 1U);
    CHECK(snapshot_matches_marker(&copy, 111U));

    /* An internal store buffer is never a valid copy destination. */
    CHECK(!ams_measurement_store_copy_latest(&store, &store.buffer[0]));
    CHECK(!ams_measurement_store_copy_latest(&store, &store.buffer[1]));

    return true;
}

static bool test_rejects_wrong_publish_pointer(void)
{
    ams_measurement_store_t store;
    ams_measurement_snapshot_t outsider;

    CHECK(ams_measurement_store_init(&store, &noop_lock_ops));

    ams_measurement_snapshot_t *write =
        ams_measurement_store_begin_write(&store);
    CHECK(write != NULL);
    fill_snapshot(write, 9U);
    fill_snapshot(&outsider, 8U);

    CHECK(ams_measurement_store_publish(&store, &outsider) == 0U);
    CHECK(store.write_in_progress);
    CHECK(!store.published);
    CHECK(ams_measurement_store_abort_write(&store, write));

    return true;
}

struct pthread_lock_context
{
    pthread_mutex_t mutex;
};

static ams_measurement_lock_key_t test_mutex_enter(void *context)
{
    struct pthread_lock_context *lock = context;
    if(pthread_mutex_lock(&lock->mutex) != 0)
    {
        abort();
    }
    return 0U;
}

static void test_mutex_exit(void *context, ams_measurement_lock_key_t key)
{
    struct pthread_lock_context *lock = context;
    (void)key;
    if(pthread_mutex_unlock(&lock->mutex) != 0)
    {
        abort();
    }
}

struct stress_context
{
    ams_measurement_store_t store;
    atomic_bool stop;
    atomic_bool failed;
    atomic_uint copies;
};

static void *stress_writer(void *argument)
{
    struct stress_context *context = argument;

    for(uint32_t i = 1U; i <= 100000U; ++i)
    {
        ams_measurement_snapshot_t *write =
            ams_measurement_store_begin_write(&context->store);

        if(write == NULL)
        {
            continue;
        }

        uint16_t marker = (uint16_t)((i % 60000U) + 1U);
        fill_snapshot(write, marker);

        if(ams_measurement_store_publish(&context->store, write) == 0U)
        {
            atomic_store_explicit(&context->failed, true, memory_order_relaxed);
            break;
        }
    }

    atomic_store_explicit(&context->stop, true, memory_order_release);
    return NULL;
}

static void *stress_reader(void *argument)
{
    struct stress_context *context = argument;
    ams_measurement_snapshot_t copy;

    while(!atomic_load_explicit(&context->stop, memory_order_acquire))
    {
        if(!ams_measurement_store_copy_latest(&context->store, &copy))
        {
            continue;
        }

        uint16_t marker = (uint16_t)copy.current.latest_A;
        if((marker == 0U) || !snapshot_matches_marker(&copy, marker))
        {
            atomic_store_explicit(&context->failed, true, memory_order_relaxed);
            atomic_store_explicit(&context->stop, true, memory_order_release);
            break;
        }

        (void)atomic_fetch_add_explicit(&context->copies, 1U, memory_order_relaxed);
    }

    return NULL;
}

static bool test_concurrent_copy_stress(void)
{
    struct pthread_lock_context lock;
    struct stress_context context;
    pthread_t writer;
    pthread_t readers[3];

    memset(&context, 0, sizeof(context));
    atomic_init(&context.stop, false);
    atomic_init(&context.failed, false);
    atomic_init(&context.copies, 0U);
    CHECK(pthread_mutex_init(&lock.mutex, NULL) == 0);

    ams_measurement_lock_ops_t lock_ops = {
        .context = &lock,
        .enter = test_mutex_enter,
        .exit = test_mutex_exit,
    };

    CHECK(ams_measurement_store_init(&context.store, &lock_ops));

    /* Start readers first so this remains a real concurrency test even on a
     * fast host where the writer could otherwise finish before they launch. */
    for(size_t i = 0U; i < 3U; ++i)
    {
        CHECK(pthread_create(&readers[i], NULL, stress_reader, &context) == 0);
    }

    CHECK(pthread_create(&writer, NULL, stress_writer, &context) == 0);
    CHECK(pthread_join(writer, NULL) == 0);

    for(size_t i = 0U; i < 3U; ++i)
    {
        CHECK(pthread_join(readers[i], NULL) == 0);
    }

    CHECK(!atomic_load_explicit(&context.failed, memory_order_relaxed));
    CHECK(atomic_load_explicit(&context.copies, memory_order_relaxed) > 0U);
    CHECK(pthread_mutex_destroy(&lock.mutex) == 0);

    return true;
}

int main(void)
{
    struct test_case
    {
        const char *name;
        bool (*run)(void);
    } tests[] = {
        {"init_contract", test_init_contract},
        {"publish_and_copy", test_publish_and_copy},
        {"drop_abort_and_sequence_gaps", test_drop_abort_and_sequence_gaps},
        {"reader_pin_blocks_reuse", test_reader_pin_blocks_reuse},
        {"sequence_wrap_and_saturation", test_sequence_wrap_and_saturation},
        {"abort_preserves_last_publication", test_abort_preserves_last_publication},
        {"rejects_wrong_publish_pointer", test_rejects_wrong_publish_pointer},
        {"concurrent_copy_stress", test_concurrent_copy_stress},
    };

    size_t passed = 0U;

    for(size_t i = 0U; i < (sizeof(tests) / sizeof(tests[0])); ++i)
    {
        if(!tests[i].run())
        {
            fprintf(stderr, "FAILED: %s\n", tests[i].name);
            return 1;
        }

        printf("PASS: %s\n", tests[i].name);
        passed++;
    }

    printf("PASS: %zu measurement-store tests\n", passed);
    return 0;
}
