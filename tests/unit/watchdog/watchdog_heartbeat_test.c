#include <ams_core/ams_watchdog_heartbeat.h>

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static unsigned checks;
static unsigned failures;

#define CHECK(expr) do { \
    checks++; \
    if (!(expr)) { \
        failures++; \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); \
    } \
} while (0)

static uint16_t bit(ams_watchdog_heartbeat_id_t id)
{
    return AMS_WATCHDOG_HEARTBEAT_BIT(id);
}

static uint16_t final_safety_mask(void)
{
    return (uint16_t)(bit(AMS_WATCHDOG_HEARTBEAT_ADBMS) |
                      bit(AMS_WATCHDOG_HEARTBEAT_CURRENT) |
                      bit(AMS_WATCHDOG_HEARTBEAT_TEMP) |
                      bit(AMS_WATCHDOG_HEARTBEAT_CAN) |
                      bit(AMS_WATCHDOG_HEARTBEAT_IMD) |
                      bit(AMS_WATCHDOG_HEARTBEAT_FAN));
}

static void test_timeout_schema(void)
{
    CHECK(AMS_WATCHDOG_HEARTBEAT_STARTUP_GRACE_MS == 3000U);
    CHECK(ams_watchdog_heartbeat_timeout_ms(AMS_WATCHDOG_HEARTBEAT_ADBMS) == 3000U);
    CHECK(ams_watchdog_heartbeat_timeout_ms(AMS_WATCHDOG_HEARTBEAT_CURRENT) == 200U);
    CHECK(ams_watchdog_heartbeat_timeout_ms(AMS_WATCHDOG_HEARTBEAT_TEMP) == 3000U);
    CHECK(ams_watchdog_heartbeat_timeout_ms(AMS_WATCHDOG_HEARTBEAT_CAN) == 2000U);
    CHECK(ams_watchdog_heartbeat_timeout_ms(AMS_WATCHDOG_HEARTBEAT_LOGGER) == 2000U);
    CHECK(ams_watchdog_heartbeat_timeout_ms(AMS_WATCHDOG_HEARTBEAT_IMD) == 500U);
    CHECK(ams_watchdog_heartbeat_timeout_ms(AMS_WATCHDOG_HEARTBEAT_FAN) == 1000U);
    CHECK(ams_watchdog_heartbeat_timeout_ms(AMS_WATCHDOG_HEARTBEAT_ESTIMATOR) == 500U);
    CHECK(ams_watchdog_heartbeat_timeout_ms((ams_watchdog_heartbeat_id_t)99) == 0U);
}

static void test_unseen_global_grace(void)
{
    ams_watchdog_heartbeat_monitor_t m;
    uint16_t safety = final_safety_mask();

    ams_watchdog_heartbeat_init(&m, 100U);
    CHECK(m.boot_ms == 100U);
    CHECK(m.seen_mask == 0U);
    CHECK(ams_watchdog_heartbeat_update(&m, 3099U, safety) == 0U);
    CHECK(ams_watchdog_heartbeat_update(&m, 3100U, safety) ==
          AMS_WATCHDOG_HEARTBEAT_ALL_MASK);
    CHECK(m.safety_stale_mask == safety);
    CHECK(m.logger_stale_mask == bit(AMS_WATCHDOG_HEARTBEAT_LOGGER));

    ams_watchdog_heartbeat_init(&m, UINT32_MAX - 100U);
    CHECK(ams_watchdog_heartbeat_update(&m, 2898U, safety) == 0U); /* 2999 */
    CHECK(ams_watchdog_heartbeat_update(&m, 2899U, safety) ==
          AMS_WATCHDOG_HEARTBEAT_ALL_MASK); /* 3000 */
}

static void test_each_seen_timeout_boundary(void)
{
    ams_watchdog_heartbeat_monitor_t m;
    uint16_t safety = final_safety_mask();

    for (uint8_t i = 0U; i < (uint8_t)AMS_WATCHDOG_HEARTBEAT_COUNT; ++i) {
        ams_watchdog_heartbeat_id_t id = (ams_watchdog_heartbeat_id_t)i;
        uint32_t timeout = ams_watchdog_heartbeat_timeout_ms(id);
        uint16_t target = bit(id);
        uint16_t stale;

        ams_watchdog_heartbeat_init(&m, 0U);
        /* Mark every ID seen at the same post-grace time so only target age is
         * changed below and unseen-grace semantics cannot contaminate it. */
        for (uint8_t j = 0U; j < (uint8_t)AMS_WATCHDOG_HEARTBEAT_COUNT; ++j) {
            CHECK(ams_watchdog_heartbeat_kick(&m,
                  (ams_watchdog_heartbeat_id_t)j, 4000U));
        }
        m.last_ms[id] = 5000U;

        stale = ams_watchdog_heartbeat_update(&m, 5000U + timeout - 1U, safety);
        CHECK((stale & target) == 0U);
        stale = ams_watchdog_heartbeat_update(&m, 5000U + timeout, safety);
        CHECK((stale & target) == 0U); /* equality is fresh */
        stale = ams_watchdog_heartbeat_update(&m, 5000U + timeout + 1U, safety);
        CHECK((stale & target) != 0U);
    }
}

static void test_kick_gap_saturation_and_masks(void)
{
    ams_watchdog_heartbeat_monitor_t m;
    uint16_t fan = bit(AMS_WATCHDOG_HEARTBEAT_FAN);
    uint16_t logger = bit(AMS_WATCHDOG_HEARTBEAT_LOGGER);
    uint16_t estimator = bit(AMS_WATCHDOG_HEARTBEAT_ESTIMATOR);

    ams_watchdog_heartbeat_init(&m, 10U);
    CHECK(ams_watchdog_heartbeat_update(NULL, 20U, final_safety_mask()) ==
          AMS_WATCHDOG_HEARTBEAT_ALL_MASK);
    CHECK(!ams_watchdog_heartbeat_kick(NULL, AMS_WATCHDOG_HEARTBEAT_FAN, 20U));
    CHECK(!ams_watchdog_heartbeat_kick(&m, (ams_watchdog_heartbeat_id_t)99, 20U));
    CHECK(ams_watchdog_heartbeat_kick(&m, AMS_WATCHDOG_HEARTBEAT_FAN, 20U));
    CHECK(m.count[AMS_WATCHDOG_HEARTBEAT_FAN] == 1U);
    CHECK(m.last_gap_ms[AMS_WATCHDOG_HEARTBEAT_FAN] == 0U);
    CHECK(ams_watchdog_heartbeat_kick(&m, AMS_WATCHDOG_HEARTBEAT_FAN, 45U));
    CHECK(m.last_gap_ms[AMS_WATCHDOG_HEARTBEAT_FAN] == 25U);
    CHECK(m.max_gap_ms[AMS_WATCHDOG_HEARTBEAT_FAN] == 25U);
    CHECK(ams_watchdog_heartbeat_kick(&m, AMS_WATCHDOG_HEARTBEAT_FAN, 55U));
    CHECK(m.last_gap_ms[AMS_WATCHDOG_HEARTBEAT_FAN] == 10U);
    CHECK(m.max_gap_ms[AMS_WATCHDOG_HEARTBEAT_FAN] == 25U);

    m.count[AMS_WATCHDOG_HEARTBEAT_FAN] = UINT32_MAX;
    CHECK(ams_watchdog_heartbeat_kick(&m, AMS_WATCHDOG_HEARTBEAT_FAN, 60U));
    CHECK(m.count[AMS_WATCHDOG_HEARTBEAT_FAN] == UINT32_MAX);

    /* Logger and estimator can be stale without entering a no-SoP safety mask. */
    ams_watchdog_heartbeat_init(&m, 0U);
    CHECK(ams_watchdog_heartbeat_kick(&m, AMS_WATCHDOG_HEARTBEAT_FAN, 4000U));
    CHECK(ams_watchdog_heartbeat_kick(&m, AMS_WATCHDOG_HEARTBEAT_LOGGER, 1000U));
    CHECK(ams_watchdog_heartbeat_kick(&m, AMS_WATCHDOG_HEARTBEAT_ESTIMATOR, 1000U));
    (void)ams_watchdog_heartbeat_update(&m, 4000U, fan);
    CHECK((m.stale_mask & logger) != 0U);
    CHECK((m.stale_mask & estimator) != 0U);
    CHECK(m.safety_stale_mask == 0U);
    CHECK(m.logger_stale_mask == logger);

    (void)ams_watchdog_heartbeat_update(&m, 4000U, (uint16_t)(fan | estimator));
    CHECK((m.safety_stale_mask & estimator) != 0U);
}

int main(void)
{
    test_timeout_schema();
    test_unseen_global_grace();
    test_each_seen_timeout_boundary();
    test_kick_gap_saturation_and_masks();

    if (failures != 0U) {
        fprintf(stderr, "watchdog heartbeat: %u checks, %u failures\n", checks, failures);
        return 1;
    }
    printf("PASS watchdog heartbeat oracle: %u checks, 0 failures\n", checks);
    return 0;
}
