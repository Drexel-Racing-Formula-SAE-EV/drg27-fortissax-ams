#include "ams_safety.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include <zephyr/fatal.h>

static int init_return;
static unsigned int init_calls;
static unsigned int direct_low_calls;
static unsigned int halt_calls;
static unsigned int event_sequence;
static unsigned int direct_low_sequence;
static unsigned int halt_sequence;
static unsigned int halt_reason;
static bool halt_saw_panic_latched;

int ams_bms_ok_platform_init_low(void)
{
    init_calls++;
    return init_return;
}

void ams_bms_ok_force_low_direct(void)
{
    direct_low_calls++;
    direct_low_sequence = ++event_sequence;
}

void k_fatal_halt(unsigned int reason)
{
    halt_calls++;
    halt_reason = reason;
    halt_saw_panic_latched = ams_safety_panic_latched();
    halt_sequence = ++event_sequence;
}

/* Production override under test. */
void k_sys_fatal_error_handler(unsigned int reason, const struct arch_esf *esf);

#define CHECK(expr) do { \
    checks++; \
    if (!(expr)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        return 1; \
    } \
} while (0)

int main(void)
{
    unsigned int checks = 0U;
    struct arch_esf frame = {0};

    CHECK(!ams_safety_panic_latched());

    init_return = 0;
    CHECK(ams_safety_init() == 0);
    CHECK(init_calls == 1U);
    CHECK(direct_low_calls == 0U); /* normal adapter owns its own direct-first proof */

    init_return = -17;
    CHECK(ams_safety_init() == -17);
    CHECK(init_calls == 2U);

    event_sequence = 0U;
    direct_low_sequence = 0U;
    halt_sequence = 0U;
    halt_saw_panic_latched = false;
    k_sys_fatal_error_handler(0x55U, &frame);
    CHECK(direct_low_calls == 1U);
    CHECK(halt_calls == 1U);
    CHECK(direct_low_sequence == 1U);
    CHECK(halt_sequence == 2U);
    CHECK(halt_reason == 0x55U);
    CHECK(halt_saw_panic_latched);
    CHECK(ams_safety_panic_latched());

    /* Fatal action is idempotently fail-low and panic remains latched. */
    event_sequence = 0U;
    k_sys_fatal_error_handler(0xA5U, NULL);
    CHECK(direct_low_calls == 2U);
    CHECK(halt_calls == 2U);
    CHECK(direct_low_sequence == 1U);
    CHECK(halt_sequence == 2U);
    CHECK(halt_reason == 0xA5U);
    CHECK(halt_saw_panic_latched);
    CHECK(ams_safety_panic_latched());

    printf("PASS Zephyr fatal/fail-low composition: %u checks\n", checks);
    return 0;
}
