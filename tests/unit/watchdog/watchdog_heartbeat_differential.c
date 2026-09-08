#include <ams_core/ams_watchdog_heartbeat.h>

#include "watchdog_heartbeat_reference_oracle.h"

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t rng_state;

static uint32_t rand32(void)
{
    uint32_t x = rng_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    rng_state = x;
    return x;
}

static int compare_monitor(const ams_watchdog_heartbeat_monitor_t *dut,
                           const ref_heartbeat_monitor_t *ref,
                           uint32_t step)
{
#define CMP(field) do { \
    if ((dut)->field != (ref)->field) { \
        fprintf(stderr, "step %u mismatch %s: dut=%u ref=%u\n", step, #field, \
                (unsigned)(dut)->field, (unsigned)(ref)->field); \
        return 1; \
    } \
} while (0)
    CMP(boot_ms);
    CMP(seen_mask);
    CMP(stale_mask);
    CMP(safety_stale_mask);
    CMP(logger_stale_mask);
#undef CMP

    for (uint8_t i = 0U; i < (uint8_t)AMS_WATCHDOG_HEARTBEAT_COUNT; ++i) {
        if (dut->last_ms[i] != ref->last_ms[i] ||
            dut->last_gap_ms[i] != ref->last_gap_ms[i] ||
            dut->max_gap_ms[i] != ref->max_gap_ms[i] ||
            dut->count[i] != ref->count[i]) {
            fprintf(stderr,
                    "step %u id %u mismatch last=%u/%u gap=%u/%u max=%u/%u count=%u/%u\n",
                    step, i, dut->last_ms[i], ref->last_ms[i],
                    dut->last_gap_ms[i], ref->last_gap_ms[i],
                    dut->max_gap_ms[i], ref->max_gap_ms[i],
                    dut->count[i], ref->count[i]);
            return 1;
        }
    }
    return 0;
}

int main(int argc, char **argv)
{
    uint32_t seed = argc > 1 ? (uint32_t)strtoul(argv[1], NULL, 0) : 1U;
    uint32_t operations = argc > 2 ? (uint32_t)strtoul(argv[2], NULL, 0) : 100000U;
    ams_watchdog_heartbeat_monitor_t dut;
    ref_heartbeat_monitor_t ref;
    uint32_t now;

    rng_state = seed == 0U ? 1U : seed;
    /* Deliberately start near wrap on half the seeds. */
    now = (seed & 1U) ? (UINT32_MAX - (rand32() & 0x7ffU)) : rand32();
    ams_watchdog_heartbeat_init(&dut, now);
    ref_heartbeat_init(&ref, now);

    for (uint32_t step = 0U; step < operations; ++step) {
        uint32_t op = rand32() % 100U;
        uint32_t advance = rand32() % 4001U;
        uint16_t safety_mask = (uint16_t)(rand32() & AMS_WATCHDOG_HEARTBEAT_ALL_MASK);
        now += advance;

        if (op < 5U) {
            /* Reinitialization is part of boot/reset state-machine fuzzing. */
            ams_watchdog_heartbeat_init(&dut, now);
            ref_heartbeat_init(&ref, now);
        } else if (op < 65U) {
            ams_watchdog_heartbeat_id_t id =
                (ams_watchdog_heartbeat_id_t)(rand32() % AMS_WATCHDOG_HEARTBEAT_COUNT);
            bool a = ams_watchdog_heartbeat_kick(&dut, id, now);
            bool b = ref_heartbeat_kick(&ref, id, now);
            if (a != b) {
                fprintf(stderr, "step %u kick result mismatch\n", step);
                return 1;
            }
        } else if (op < 69U) {
            /* Invalid IDs must be side-effect free. */
            ams_watchdog_heartbeat_id_t id = (ams_watchdog_heartbeat_id_t)(100U + (rand32() & 31U));
            bool a = ams_watchdog_heartbeat_kick(&dut, id, now);
            bool b = ref_heartbeat_kick(&ref, id, now);
            if (a != b) {
                fprintf(stderr, "step %u invalid kick result mismatch\n", step);
                return 1;
            }
        } else if (op < 72U) {
            /* Exercise saturation without spending 2^32 operations. */
            unsigned id = rand32() % AMS_WATCHDOG_HEARTBEAT_COUNT;
            dut.count[id] = UINT32_MAX;
            ref.count[id] = UINT32_MAX;
        } else {
            uint16_t a = ams_watchdog_heartbeat_update(&dut, now, safety_mask);
            uint16_t b = ref_heartbeat_update(&ref, now, safety_mask);
            if (a != b) {
                fprintf(stderr, "step %u update result mismatch dut=0x%04x ref=0x%04x\n",
                        step, a, b);
                return 1;
            }
        }

        if (compare_monitor(&dut, &ref, step) != 0) {
            return 1;
        }
    }

    printf("PASS heartbeat differential seed=0x%08x operations=%u\n", seed, operations);
    return 0;
}
