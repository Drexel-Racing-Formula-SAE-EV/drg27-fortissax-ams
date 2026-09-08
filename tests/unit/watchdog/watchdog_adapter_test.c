#include <ams_platform/watchdog.h>

#include <zephyr/device.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/drivers/watchdog.h>

#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct device fake_watchdog_device;

static bool fake_ready = true;
static int fake_install_ret = 0;
static int fake_setup_ret = 0;
static int fake_feed_ret = 0;
static int fake_reset_get_ret = 0;
static int fake_reset_clear_ret = 0;
static uint32_t fake_reset_cause = 0U;

static uint32_t install_calls;
static uint32_t setup_calls;
static uint32_t feed_calls;
static uint32_t disable_calls;
static uint32_t reset_get_calls;
static uint32_t reset_clear_calls;
static struct wdt_timeout_cfg last_timeout;
static uint8_t last_setup_options;
static int last_feed_channel;

void ams_watchdog_platform_test_reset(void);
void ams_watchdog_platform_test_seed_counters(uint32_t prepare_count,
                                               uint32_t start_count,
                                               uint32_t feed_success_count,
                                               uint32_t feed_failure_count);

bool device_is_ready(const struct device *dev)
{
    return dev == &fake_watchdog_device && fake_ready;
}

int wdt_install_timeout(const struct device *dev, const struct wdt_timeout_cfg *cfg)
{
    if (dev != &fake_watchdog_device || cfg == NULL) {
        return -EINVAL;
    }
    install_calls++;
    last_timeout = *cfg;
    return fake_install_ret;
}

int wdt_setup(const struct device *dev, uint8_t options)
{
    if (dev != &fake_watchdog_device) {
        return -EINVAL;
    }
    setup_calls++;
    last_setup_options = options;
    return fake_setup_ret;
}

int wdt_feed(const struct device *dev, int channel_id)
{
    if (dev != &fake_watchdog_device) {
        return -EINVAL;
    }
    feed_calls++;
    last_feed_channel = channel_id;
    return fake_feed_ret;
}

int wdt_disable(const struct device *dev)
{
    (void)dev;
    disable_calls++;
    return 0;
}

int hwinfo_get_reset_cause(uint32_t *cause)
{
    reset_get_calls++;
    if (fake_reset_get_ret == 0 && cause != NULL) {
        *cause = fake_reset_cause;
    }
    return fake_reset_get_ret;
}

int hwinfo_clear_reset_cause(void)
{
    reset_clear_calls++;
    return fake_reset_clear_ret;
}

static unsigned checks;
static unsigned failures;
#define CHECK(expr) do { checks++; if (!(expr)) { failures++; \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); } } while (0)

static void reset_fixture(void)
{
    fake_ready = true;
    fake_install_ret = 0;
    fake_setup_ret = 0;
    fake_feed_ret = 0;
    fake_reset_get_ret = 0;
    fake_reset_clear_ret = 0;
    fake_reset_cause = 0U;
    install_calls = setup_calls = feed_calls = disable_calls = 0U;
    reset_get_calls = reset_clear_calls = 0U;
    memset(&last_timeout, 0, sizeof(last_timeout));
    last_setup_options = 0xFFU;
    last_feed_channel = -1;
    ams_watchdog_platform_test_reset();
}

static void test_prepare_contract(void)
{
    ams_watchdog_platform_status_t s;
    reset_fixture();
    fake_install_ret = 3;
    fake_reset_cause = RESET_WATCHDOG;
    CHECK(ams_watchdog_platform_prepare() == 0);
    s = ams_watchdog_platform_status();
    CHECK(s.state == AMS_WATCHDOG_PLATFORM_READY_NOT_STARTED);
    CHECK(s.channel_id == 3);
    CHECK(install_calls == 1U);
    CHECK(setup_calls == 0U);
    CHECK(last_timeout.window.min == 0U);
    CHECK(last_timeout.window.max == 5000U);
    CHECK(last_timeout.callback == NULL);
    CHECK(last_timeout.flags == WDT_FLAG_RESET_SOC);
    CHECK(s.reset_cause_checked);
    CHECK(s.reset_cause_valid);
    CHECK(s.reset_was_watchdog);
    CHECK(reset_get_calls == 1U);
    CHECK(reset_clear_calls == 1U);
    CHECK(disable_calls == 0U);
    /* Idempotent prepare does not reinstall or reread reset flags. */
    CHECK(ams_watchdog_platform_prepare() == 0);
    CHECK(install_calls == 1U);
    CHECK(reset_get_calls == 1U);
}

static void test_prepare_failures_retry(void)
{
    ams_watchdog_platform_status_t s;
    reset_fixture();
    fake_ready = false;
    CHECK(ams_watchdog_platform_prepare() == -ENODEV);
    s = ams_watchdog_platform_status();
    CHECK(s.state == AMS_WATCHDOG_PLATFORM_PREPARE_FAILED_RETRYABLE);
    CHECK(install_calls == 0U);
    fake_ready = true;
    fake_install_ret = -EINVAL;
    CHECK(ams_watchdog_platform_prepare() == -EINVAL);
    CHECK(ams_watchdog_platform_status().state ==
          AMS_WATCHDOG_PLATFORM_PREPARE_FAILED_RETRYABLE);
    fake_install_ret = 2;
    CHECK(ams_watchdog_platform_prepare() == 0);
    CHECK(ams_watchdog_platform_status().state ==
          AMS_WATCHDOG_PLATFORM_READY_NOT_STARTED);
    CHECK(setup_calls == 0U);
}

static void test_start_irreversibility(void)
{
    reset_fixture();
    CHECK(ams_watchdog_platform_start() == AMS_WATCHDOG_START_RESULT_NOT_READY);
    fake_install_ret = 7;
    CHECK(ams_watchdog_platform_prepare() == 0);
    CHECK(ams_watchdog_platform_start() == AMS_WATCHDOG_START_RESULT_STARTED);
    CHECK(setup_calls == 1U);
    CHECK(last_setup_options == 0U); /* no debugger/sleep pause */
    CHECK(ams_watchdog_platform_status().state == AMS_WATCHDOG_PLATFORM_STARTED);
    CHECK(ams_watchdog_platform_start() == AMS_WATCHDOG_START_RESULT_ALREADY_STARTED);
    CHECK(setup_calls == 1U);
    CHECK(disable_calls == 0U);

    reset_fixture();
    fake_install_ret = 1;
    CHECK(ams_watchdog_platform_prepare() == 0);
    fake_setup_ret = -ENODEV;
    CHECK(ams_watchdog_platform_start() ==
          AMS_WATCHDOG_START_RESULT_AMBIGUOUS_TERMINAL);
    CHECK(ams_watchdog_platform_status().state ==
          AMS_WATCHDOG_PLATFORM_START_AMBIGUOUS_TERMINAL);
    CHECK(ams_watchdog_platform_start() ==
          AMS_WATCHDOG_START_RESULT_AMBIGUOUS_TERMINAL);
    CHECK(setup_calls == 1U); /* terminal: never retry */
    CHECK(ams_watchdog_platform_prepare() == -EPERM);
    CHECK(install_calls == 1U);
    CHECK(disable_calls == 0U);
}

static void test_feed_contract(void)
{
    ams_watchdog_platform_status_t s;
    reset_fixture();
    CHECK(ams_watchdog_platform_feed() == -EACCES);
    CHECK(feed_calls == 0U);
    fake_install_ret = 4;
    CHECK(ams_watchdog_platform_prepare() == 0);
    CHECK(ams_watchdog_platform_start() == AMS_WATCHDOG_START_RESULT_STARTED);
    CHECK(ams_watchdog_platform_feed() == 0);
    CHECK(feed_calls == 1U);
    CHECK(last_feed_channel == 4);
    s = ams_watchdog_platform_status();
    CHECK(s.feed_success_count == 1U);

    fake_feed_ret = -EIO;
    CHECK(ams_watchdog_platform_feed() == -EIO);
    s = ams_watchdog_platform_status();
    CHECK(s.state == AMS_WATCHDOG_PLATFORM_FEED_FAILED_TERMINAL);
    CHECK(s.feed_failure_count == 1U);
    CHECK(ams_watchdog_platform_feed() == -EACCES);
    CHECK(feed_calls == 2U); /* terminal: no second driver feed */
    CHECK(ams_watchdog_platform_start() ==
          AMS_WATCHDOG_START_RESULT_AMBIGUOUS_TERMINAL);
    CHECK(setup_calls == 1U);
    CHECK(disable_calls == 0U);
}


static void test_reset_cause_failure_semantics(void)
{
    ams_watchdog_platform_status_t s;

    reset_fixture();
    fake_reset_get_ret = -EIO;
    fake_install_ret = 2;
    CHECK(ams_watchdog_platform_prepare() == 0);
    s = ams_watchdog_platform_status();
    CHECK(s.reset_cause_checked);
    CHECK(!s.reset_cause_valid);
    CHECK(!s.reset_was_watchdog);
    CHECK(s.reset_cause_error == -EIO);
    CHECK(s.reset_cause_clear_error == 0);
    CHECK(reset_get_calls == 1U);
    CHECK(reset_clear_calls == 0U);

    /* Reset-cause observation is boot evidence: a later prepare/status retry
     * must not fabricate a second read after the first attempt failed. */
    fake_reset_get_ret = 0;
    fake_reset_cause = RESET_WATCHDOG;
    CHECK(ams_watchdog_platform_prepare() == 0);
    CHECK(reset_get_calls == 1U);
    CHECK(!ams_watchdog_platform_status().reset_cause_valid);

    reset_fixture();
    fake_install_ret = 5;
    fake_reset_cause = RESET_WATCHDOG;
    fake_reset_clear_ret = -EIO;
    CHECK(ams_watchdog_platform_prepare() == 0);
    s = ams_watchdog_platform_status();
    CHECK(s.reset_cause_valid);
    CHECK(s.reset_was_watchdog);
    CHECK(s.reset_cause_error == 0);
    CHECK(s.reset_cause_clear_error == -EIO);
    CHECK(reset_get_calls == 1U);
    CHECK(reset_clear_calls == 1U);
}

static void test_counter_saturation(void)
{
    ams_watchdog_platform_status_t s;

    reset_fixture();
    ams_watchdog_platform_test_seed_counters(UINT32_MAX, UINT32_MAX,
                                             UINT32_MAX, UINT32_MAX);
    fake_install_ret = 6;
    CHECK(ams_watchdog_platform_prepare() == 0);
    CHECK(ams_watchdog_platform_start() == AMS_WATCHDOG_START_RESULT_STARTED);
    CHECK(ams_watchdog_platform_feed() == 0);
    s = ams_watchdog_platform_status();
    CHECK(s.prepare_attempt_count == UINT32_MAX);
    CHECK(s.start_attempt_count == UINT32_MAX);
    CHECK(s.feed_success_count == UINT32_MAX);

    fake_feed_ret = -EIO;
    CHECK(ams_watchdog_platform_feed() == -EIO);
    s = ams_watchdog_platform_status();
    CHECK(s.feed_failure_count == UINT32_MAX);
    CHECK(s.state == AMS_WATCHDOG_PLATFORM_FEED_FAILED_TERMINAL);
}

static uint32_t rng = 0xD14C0DEU;
static uint32_t rand32(void)
{
    uint32_t x = rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    rng = x;
    return x;
}

static void randomized_state_machine(void)
{
    reset_fixture();
    fake_install_ret = 0;

    for (uint32_t i = 0U; i < 100000U; ++i) {
        uint32_t op = rand32() % 5U;
        ams_watchdog_platform_status_t before = ams_watchdog_platform_status();
        uint32_t setup_before = setup_calls;
        uint32_t feed_before = feed_calls;
        uint32_t disable_before = disable_calls;

        if (op == 0U) {
            fake_ready = (rand32() & 3U) != 0U;
            fake_install_ret = ((rand32() & 15U) == 0U) ? -EINVAL : 0;
            (void)ams_watchdog_platform_prepare();
        } else if (op == 1U) {
            fake_setup_ret = ((rand32() & 31U) == 0U) ? -ENODEV : 0;
            (void)ams_watchdog_platform_start();
        } else if (op == 2U) {
            fake_feed_ret = ((rand32() & 63U) == 0U) ? -EIO : 0;
            (void)ams_watchdog_platform_feed();
        } else if (op == 3U) {
            (void)ams_watchdog_platform_status();
        } else if (before.state == AMS_WATCHDOG_PLATFORM_UNPREPARED ||
                   before.state == AMS_WATCHDOG_PLATFORM_PREPARE_FAILED_RETRYABLE) {
            /* Reset-cause query itself can fail but must remain one-shot. */
            fake_reset_get_ret = ((rand32() & 7U) == 0U) ? -EIO : 0;
            (void)ams_watchdog_platform_prepare();
        }

        ams_watchdog_platform_status_t after = ams_watchdog_platform_status();
        CHECK(disable_calls == disable_before);
        if (before.state == AMS_WATCHDOG_PLATFORM_START_AMBIGUOUS_TERMINAL ||
            before.state == AMS_WATCHDOG_PLATFORM_FEED_FAILED_TERMINAL) {
            CHECK(setup_calls == setup_before);
            CHECK(feed_calls == feed_before);
            CHECK(after.state == before.state);
        }
        if (before.state == AMS_WATCHDOG_PLATFORM_STARTED && op == 1U) {
            CHECK(setup_calls == setup_before);
        }
    }
    CHECK(disable_calls == 0U);
}

int main(void)
{
    test_prepare_contract();
    test_prepare_failures_retry();
    test_start_irreversibility();
    test_feed_contract();
    test_reset_cause_failure_semantics();
    test_counter_saturation();
    randomized_state_machine();

    if (failures != 0U) {
        fprintf(stderr, "watchdog adapter: %u checks, %u failures\n", checks, failures);
        return 1;
    }
    printf("PASS watchdog production adapter SIL: %u checks, 0 failures\n", checks);
    return 0;
}
