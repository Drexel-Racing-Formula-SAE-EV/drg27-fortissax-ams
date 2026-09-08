#include <ams_platform/fan_pwm.h>

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/device.h>

struct device fake_device_pwm3 = {3, true};
struct device fake_device_pwm4 = {4, true};
struct device fake_device_pwm5 = {5, true};

struct call_record {
    int dev_id;
    uint32_t channel;
    uint32_t period;
    uint32_t pulse;
    uint32_t flags;
};

static struct call_record calls[200000];
static uint32_t call_count;
static uint32_t fail_call_mask;
static uint64_t timer_hz = AMS_FAN_PWM_EXPECTED_TIMER_CLOCK_HZ;
unsigned fake_fan_irq_disable_count;
unsigned fake_fan_irq_clear_count;
uint32_t fake_fan_last_disabled_irq;
uint32_t fake_fan_last_cleared_irq;
static unsigned checks;
static unsigned failures;

#define CHECK(x) do { checks++; if (!(x)) { failures++; \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); } } while (0)

int pwm_get_cycles_per_sec(const struct device *dev, uint32_t channel, uint64_t *cycles)
{
    (void)channel;
    if (!dev || !dev->ready) return -19;
    *cycles = timer_hz;
    return 0;
}

int pwm_set_cycles(const struct device *dev, uint32_t channel,
                   uint32_t period, uint32_t pulse, uint32_t flags)
{
    uint32_t idx = call_count++;
    if (idx < (uint32_t)(sizeof(calls) / sizeof(calls[0]))) {
        calls[idx] = (struct call_record){dev ? dev->id : -1, channel, period, pulse, flags};
    }
    if ((fail_call_mask & (1U << (idx & 31U))) != 0U) return -5;
    return 0;
}

static uint32_t oracle_compare(float p)
{
    if (!isfinite(p)) p = 0.0f;
    else if (p > 100.0f) p = 100.0f;
    else if (p < 0.0f) p = 0.0f;
    if (p >= 100.0f) return 3360U;
    return (uint32_t)(((double)3360U * (double)p) / 100.0);
}

static uint32_t rng = 0xC001D00DU;
static uint32_t rnd(void) { rng = rng * 1664525U + 1013904223U; return rng; }

int main(void)
{
    fake_device_pwm3.ready = fake_device_pwm4.ready = fake_device_pwm5.ready = true;
    timer_hz = AMS_FAN_PWM_EXPECTED_TIMER_CLOCK_HZ;
    call_count = 0U; fail_call_mask = 0U;
    fake_fan_irq_disable_count = fake_fan_irq_clear_count = 0U;
    CHECK(ams_fan_pwm_init() == 0);
    CHECK(ams_fan_pwm_platform_ready());
    CHECK(ams_fan_pwm_startup_fail_mask() == 0U);
    CHECK(call_count == 6U);
    CHECK(fake_fan_irq_disable_count == 3U);
    CHECK(fake_fan_irq_clear_count == 3U);
    CHECK(fake_fan_last_disabled_irq == 50U);
    CHECK(fake_fan_last_cleared_irq == 50U);
    for (uint32_t i = 0; i < 6U; ++i) {
        CHECK(calls[i].period == 3361U);
        CHECK(calls[i].pulse == 0U);
    }
    CHECK(calls[0].dev_id == 3 && calls[0].channel == 2U);
    CHECK(calls[1].dev_id == 3 && calls[1].channel == 4U);
    CHECK(calls[2].dev_id == 4 && calls[2].channel == 3U);
    CHECK(calls[3].dev_id == 4 && calls[3].channel == 4U);
    CHECK(calls[4].dev_id == 5 && calls[4].channel == 1U);
    CHECK(calls[5].dev_id == 5 && calls[5].channel == 2U);

    const float directed[] = {NAN, -1.0f, 0.0f, 0.01f, 25.0f, 35.0f, 50.0f, 75.0f, 99.9f, 100.0f, 101.0f};
    for (uint32_t z = 0; z < 6U; ++z) {
        for (uint32_t i = 0; i < sizeof(directed)/sizeof(directed[0]); ++i) {
            uint32_t before = call_count;
            CHECK(ams_fan_pwm_set_percent((uint8_t)z, directed[i]) == 0);
            CHECK(call_count == before + 1U);
            CHECK(calls[before].period == 3361U);
            CHECK(calls[before].pulse == oracle_compare(directed[i]));
        }
    }
    CHECK(ams_fan_pwm_set_percent(6U, 50.0f) != 0);

    /* 50k randomized mapping operations. */
    for (uint32_t i = 0; i < 50000U; ++i) {
        uint8_t zone = (uint8_t)(rnd() % 6U);
        float p = ((float)(int32_t)(rnd() % 140001U) - 20000.0f) / 1000.0f;
        uint32_t before = call_count;
        CHECK(ams_fan_pwm_set_percent(zone, p) == 0);
        CHECK(calls[before].period == 3361U);
        CHECK(calls[before].pulse == oracle_compare(p));
    }

    /* Init must fail hard on unavailable platform or wrong timer clock. */
    fake_device_pwm4.ready = false;
    CHECK(ams_fan_pwm_init() != 0);
    CHECK(!ams_fan_pwm_platform_ready());
    fake_device_pwm4.ready = true;
    timer_hz = 107999999ULL;
    CHECK(ams_fan_pwm_init() != 0);
    CHECK(!ams_fan_pwm_platform_ready());
    timer_hz = AMS_FAN_PWM_EXPECTED_TIMER_CLOCK_HZ;

    /* Exhaust all 64 six-zone startup-failure combinations. A channel-start
     * failure is soft, every later zone is still attempted, and the exact
     * failure mask is retained for the real fan worker to publish/retry. */
    for (uint32_t mask = 0U; mask < 64U; ++mask) {
        call_count = 0U;
        fail_call_mask = mask;
        CHECK(ams_fan_pwm_init() == 0);
        CHECK(ams_fan_pwm_platform_ready());
        CHECK(ams_fan_pwm_startup_fail_mask() == mask);
        CHECK(call_count == 6U);
    }

    /* Force-off also attempts all six for every possible failure pattern. */
    for (uint32_t mask = 0U; mask < 64U; ++mask) {
        call_count = 0U;
        fail_call_mask = mask;
        CHECK(ams_fan_pwm_force_all_off() == mask);
        CHECK(call_count == 6U);
    }

    /* A soft PWM write failure must not poison the adapter. The next fan-task
     * cycle can retry the same physical channel. */
    call_count = 0U;
    fail_call_mask = 1U;
    CHECK(ams_fan_pwm_set_percent(0U, 50.0f) != 0);
    fail_call_mask = 0U;
    CHECK(ams_fan_pwm_set_percent(0U, 50.0f) == 0);
    CHECK(calls[1].dev_id == 3 && calls[1].channel == 2U);
    CHECK(calls[1].pulse == oracle_compare(50.0f));

    if (failures) {
        fprintf(stderr, "fan PWM adapter: FAIL (%u checks, %u failures)\n", checks, failures);
        return 1;
    }
    printf("fan PWM adapter: PASS (%u checks)\n", checks);
    return 0;
}
