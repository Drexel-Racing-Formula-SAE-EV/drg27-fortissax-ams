#include <ams_core/ams_imd.h>

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static unsigned checks;
static unsigned failures;

#define CHECK(x) do { \
    checks++; \
    if (!(x)) { \
        failures++; \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); \
    } \
} while (0)

static bool nearf(float a, float b, float tol)
{
    return isfinite(a) && isfinite(b) && fabsf(a - b) <= tol;
}

static void publish_frequency(ams_imd_t *imd,
                              uint32_t clock_hz,
                              float frequency_hz,
                              float duty_percent,
                              uint32_t tick)
{
    uint32_t total = (uint32_t)((float)clock_hz / frequency_hz);
    uint32_t high = (uint32_t)((float)total * duty_percent / 100.0f);
    ams_imd_capture_publish(imd, high, total, tick);
}

static void test_fail_closed_and_normal(void)
{
    ams_imd_t imd;

    memset(&imd, 0xA5, sizeof(imd));
    ams_imd_init(&imd, 108000000U, true);
    CHECK(imd.ret == 1);
    CHECK(!imd.ok_hs);
    CHECK(imd.status == AMS_IMD_UNKNOWN);
    CHECK(!imd.capture_seen);
    CHECK(imd.capture_count == 0U);
    CHECK(ams_imd_read_at(&imd, true, true, 1000U) != 0);

    ams_imd_capture_publish(&imd, 5400000U, 10800000U, 1000U);
    CHECK(imd.capture_seen);
    CHECK(imd.capture_count == 1U);
    CHECK(ams_imd_read_at(&imd, true, true, 1000U) == 0);
    CHECK(imd.status == AMS_IMD_NORMAL);
    CHECK(imd.ok_hs);
    CHECK(nearf(imd.frequency_hz, 10.0f, 0.0001f));
    CHECK(nearf(imd.duty_percent, 50.0f, 0.0001f));
    CHECK(ams_imd_is_ok(&imd));

    CHECK(ams_imd_read_at(&imd, true, false, 1001U) == 0);
    CHECK(imd.status == AMS_IMD_NORMAL);
    CHECK(!imd.ok_hs);
    CHECK(!ams_imd_is_ok(&imd));

    CHECK(ams_imd_read_at(&imd, false, true, 1002U) != 0);
    CHECK(imd.status == AMS_IMD_UNKNOWN);
    CHECK(!imd.ok_hs);
    CHECK(imd.duty_percent == 0.0f);
    CHECK(imd.frequency_hz == 0.0f);
}

static void test_status_mapping(void)
{
    ams_imd_t imd;
    const float frequency[] = {1.0f, 10.0f, 20.0f, 30.0f, 40.0f, 50.0f};

    for (uint8_t code = 0U; code <= 5U; ++code) {
        ams_imd_init(&imd, 108000000U, true);
        publish_frequency(&imd, 108000000U, frequency[code], 37.0f, 200U);
        CHECK(ams_imd_read_at(&imd, true, true, 200U) == 0);
        CHECK((uint8_t)imd.status == code);
    }

    ams_imd_init(&imd, 108000000U, true);
    publish_frequency(&imd, 108000000U, 60.0f, 50.0f, 200U);
    CHECK(ams_imd_read_at(&imd, true, true, 200U) != 0);
    CHECK(imd.status == AMS_IMD_UNKNOWN);
}

static void test_boundaries_and_corruption(void)
{
    ams_imd_t imd;

    ams_imd_init(&imd, 108000000U, true);
    ams_imd_capture_publish(&imd, 101U, 100U, 10U);
    CHECK(ams_imd_read_at(&imd, true, true, 10U) != 0);

    ams_imd_init(&imd, 108000000U, true);
    ams_imd_capture_publish(&imd, 0U, 0U, 10U);
    CHECK(ams_imd_read_at(&imd, true, true, 10U) != 0);

    ams_imd_init(&imd, UINT32_MAX, true);
    ams_imd_capture_publish(&imd, UINT32_MAX, UINT32_MAX, 10U);
    CHECK(ams_imd_read_at(&imd, true, true, 10U) == 0);
    CHECK(nearf(imd.duty_percent, 100.0f, 0.01f));
    CHECK(isfinite(imd.frequency_hz));

    ams_imd_init(&imd, UINT32_MAX, true);
    ams_imd_capture_publish(&imd, 0U, 1U, 10U);
    CHECK(ams_imd_read_at(&imd, true, true, 10U) != 0);

    ams_imd_init(&imd, 0U, true);
    ams_imd_capture_publish(&imd, 50U, 100U, 10U);
    CHECK(ams_imd_read_at(&imd, true, true, 10U) != 0);

    ams_imd_init(&imd, 1000U, false);
    ams_imd_capture_publish(&imd, 50U, 100U, 10U);
    CHECK(!imd.capture_seen);
    CHECK(ams_imd_read_at(&imd, true, true, 10U) != 0);
}

static void test_freshness_wrap_and_seqlock(void)
{
    ams_imd_t imd;
    uint32_t tick = UINT32_MAX - 20U;

    ams_imd_init(&imd, 1000U, true);
    ams_imd_capture_publish(&imd, 50U, 100U, tick);

    CHECK(ams_imd_read_at(&imd, true, true, tick + 250U) == 0);
    CHECK(ams_imd_read_at(&imd, true, true, tick + 251U) != 0);

    ams_imd_init(&imd, 1000U, true);
    ams_imd_capture_publish(&imd, 50U, 100U, tick);
    CHECK(ams_imd_read_at(&imd, true, true, 30U) == 0);

    imd.capture_sequence |= 1U;
    CHECK(ams_imd_read_at(&imd, true, true, 30U) != 0);
    CHECK(imd.status == AMS_IMD_UNKNOWN);
    CHECK(!imd.ok_hs);

    ams_imd_set_capture_started(&imd, false);
    CHECK(!imd.capture_started);
    CHECK(imd.status == AMS_IMD_UNKNOWN);
    CHECK(!imd.ok_hs);
}

static void test_capture_counter_saturation(void)
{
    ams_imd_t imd;

    ams_imd_init(&imd, 1000U, true);
    imd.capture_count = UINT32_MAX - 1U;
    ams_imd_capture_publish(&imd, 50U, 100U, 1U);
    CHECK(imd.capture_count == UINT32_MAX);
    ams_imd_capture_publish(&imd, 50U, 100U, 2U);
    CHECK(imd.capture_count == UINT32_MAX);
}

int main(void)
{
    test_fail_closed_and_normal();
    test_status_mapping();
    test_boundaries_and_corruption();
    test_freshness_wrap_and_seqlock();
    test_capture_counter_saturation();

    if (failures != 0U) {
        fprintf(stderr, "FAIL IMD core: %u/%u failed\n", failures, checks);
        return 1;
    }

    printf("PASS IMD core: %u checks\n", checks);
    return 0;
}
