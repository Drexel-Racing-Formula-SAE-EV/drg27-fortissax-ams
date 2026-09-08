#include "watchdog_reference_oracle.h"

#include <ams_core/ams_watchdog_policy.h>

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t rng_state;
static uint32_t rng32(void)
{
    uint32_t x = rng_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    rng_state = x;
    return x;
}

int main(int argc, char **argv)
{
    uint32_t seed;
    uint32_t operations;
    const uint16_t final_mask =
        AMS_WATCHDOG_HEARTBEAT_BIT(AMS_WATCHDOG_HEARTBEAT_ADBMS) |
        AMS_WATCHDOG_HEARTBEAT_BIT(AMS_WATCHDOG_HEARTBEAT_CURRENT) |
        AMS_WATCHDOG_HEARTBEAT_BIT(AMS_WATCHDOG_HEARTBEAT_TEMP) |
        AMS_WATCHDOG_HEARTBEAT_BIT(AMS_WATCHDOG_HEARTBEAT_CAN) |
        AMS_WATCHDOG_HEARTBEAT_BIT(AMS_WATCHDOG_HEARTBEAT_IMD) |
        AMS_WATCHDOG_HEARTBEAT_BIT(AMS_WATCHDOG_HEARTBEAT_FAN);
    ams_watchdog_policy_state_t state;

    if (argc != 3) {
        fprintf(stderr, "usage: %s seed operations\n", argv[0]);
        return 2;
    }
    seed = (uint32_t)strtoul(argv[1], NULL, 0);
    operations = (uint32_t)strtoul(argv[2], NULL, 0);
    rng_state = seed != 0U ? seed : 1U;
    ams_watchdog_policy_init(&state);

    for (uint32_t i = 0U; i < operations; ++i) {
        watchdog_reference_input_t ref_in;
        watchdog_reference_output_t ref_out;
        ams_watchdog_policy_input_t in;
        ams_watchdog_policy_action_t out;
        uint32_t r = rng32();

        memset(&ref_in, 0, sizeof(ref_in));
        ref_in.runtime_enabled = (r & (1U << 0)) != 0U;
        ref_in.hw_started = true; /* exact pure-policy comparison */
        ref_in.panic = (r & (1U << 1)) != 0U;
        ref_in.stop_feed = (r & (1U << 2)) != 0U;
        ref_in.rtos_fault = (r & (1U << 3)) != 0U;
        ref_in.stack_critical = (r & (1U << 4)) != 0U;
        ref_in.boot_ms = rng32();
        switch (rng32() % 8U) {
        case 0: ref_in.now_ms = ref_in.boot_ms + 2999U; break;
        case 1: ref_in.now_ms = ref_in.boot_ms + 3000U; break;
        case 2: ref_in.now_ms = ref_in.boot_ms + 3001U; break;
        default: ref_in.now_ms = ref_in.boot_ms + (rng32() % 10000U); break;
        }
        ref_in.safety_stale_mask = (uint16_t)(rng32() & final_mask);
        ref_out = watchdog_reference_step(ref_in);

        memset(&in, 0, sizeof(in));
        in.now_ms = ref_in.now_ms;
        in.boot_ms = ref_in.boot_ms;
        in.runtime_enabled = ref_in.runtime_enabled;
        in.platform_ready_to_start = true;
        in.platform_started = true;
        in.panic_latched = ref_in.panic;
        in.stop_feed_test = ref_in.stop_feed;
        in.oracle_required_mask = final_mask;
        in.migration_evidence_mask = final_mask;
        in.stale_mask = ref_in.safety_stale_mask;
        in.rtos_integrity_fault = ref_in.rtos_fault;
        in.stack_critical_mask = ref_in.stack_critical ? 1U : 0U;

        ams_watchdog_policy_evaluate(&state, &in, &out);
        if ((out.block_reason != ref_out.reason) ||
            (out.feed_permitted != ref_out.feed) ||
            (out.health_good != ref_out.ok)) {
            fprintf(stderr,
                    "DIFF seed=0x%08" PRIx32 " op=%" PRIu32
                    " reason=%u/%u feed=%u/%u ok=%u/%u\n",
                    seed, i, (unsigned)out.block_reason, (unsigned)ref_out.reason,
                    out.feed_permitted ? 1U : 0U, ref_out.feed ? 1U : 0U,
                    out.health_good ? 1U : 0U, ref_out.ok ? 1U : 0U);
            return 1;
        }

        if (out.feed_permitted) {
            ams_watchdog_policy_record_feed(&state, in.now_ms, out.block_reason);
        } else {
            ams_watchdog_policy_record_block(&state, out.block_reason);
        }
    }

    printf("PASS watchdog differential seed=0x%08" PRIx32 " operations=%" PRIu32 "\n",
           seed, operations);
    return 0;
}
