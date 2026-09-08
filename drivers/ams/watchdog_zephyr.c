#include <ams_platform/watchdog.h>

#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stddef.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/drivers/watchdog.h>
#include <zephyr/sys/util.h>

#define AMS_WATCHDOG_NODE DT_ALIAS(ams_watchdog)

BUILD_ASSERT(DT_NODE_EXISTS(AMS_WATCHDOG_NODE),
             "DER26 AMS requires the ams-watchdog Devicetree alias");
BUILD_ASSERT(DT_NODE_HAS_STATUS(AMS_WATCHDOG_NODE, okay),
             "ams-watchdog must reference an enabled watchdog device");

static const struct device *const watchdog_dev =
    DEVICE_DT_GET(AMS_WATCHDOG_NODE);

static ams_watchdog_platform_status_t watchdog_status = {
    .state = AMS_WATCHDOG_PLATFORM_UNPREPARED,
    .channel_id = -1,
};

static bool reset_cause_captured;

static uint32_t saturating_increment(uint32_t value)
{
    return value == UINT32_MAX ? UINT32_MAX : value + 1U;
}

static void capture_reset_cause_once(void)
{
    uint32_t cause = 0U;
    int ret;

    if (reset_cause_captured) {
        return;
    }

    reset_cause_captured = true;
    watchdog_status.reset_cause_checked = true;

    ret = hwinfo_get_reset_cause(&cause);
    watchdog_status.reset_cause_error = ret;
    if (ret == 0) {
        watchdog_status.reset_cause_valid = true;
        watchdog_status.reset_was_watchdog = (cause & RESET_WATCHDOG) != 0U;
        watchdog_status.reset_cause_clear_error = hwinfo_clear_reset_cause();
    }
}

int ams_watchdog_platform_prepare(void)
{
    struct wdt_timeout_cfg timeout_cfg = {
        .window = {
            .min = 0U,
            .max = AMS_WATCHDOG_PLATFORM_TIMEOUT_MS,
        },
        .callback = NULL,
        .flags = WDT_FLAG_RESET_SOC,
    };
    int channel;

    if ((watchdog_status.state == AMS_WATCHDOG_PLATFORM_READY_NOT_STARTED) ||
        (watchdog_status.state == AMS_WATCHDOG_PLATFORM_STARTED)) {
        return 0;
    }

    if ((watchdog_status.state == AMS_WATCHDOG_PLATFORM_START_AMBIGUOUS_TERMINAL) ||
        (watchdog_status.state == AMS_WATCHDOG_PLATFORM_FEED_FAILED_TERMINAL)) {
        return -EPERM;
    }

    watchdog_status.prepare_attempt_count =
        saturating_increment(watchdog_status.prepare_attempt_count);
    capture_reset_cause_once();

    if (!device_is_ready(watchdog_dev)) {
        watchdog_status.last_error = -ENODEV;
        watchdog_status.state = AMS_WATCHDOG_PLATFORM_PREPARE_FAILED_RETRYABLE;
        return -ENODEV;
    }

    channel = wdt_install_timeout(watchdog_dev, &timeout_cfg);
    if (channel < 0) {
        /* Zephyr STM32 v4.4 install_timeout() only calculates/stores the
         * prescaler/reload and explicitly does not enable hardware. Retrying
         * this state is therefore safe. */
        watchdog_status.last_error = channel;
        watchdog_status.state = AMS_WATCHDOG_PLATFORM_PREPARE_FAILED_RETRYABLE;
        return channel;
    }

    watchdog_status.channel_id = channel;
    watchdog_status.last_error = 0;
    watchdog_status.state = AMS_WATCHDOG_PLATFORM_READY_NOT_STARTED;
    return 0;
}

ams_watchdog_start_result_t ams_watchdog_platform_start(void)
{
    int ret;

    if (watchdog_status.state == AMS_WATCHDOG_PLATFORM_STARTED) {
        return AMS_WATCHDOG_START_RESULT_ALREADY_STARTED;
    }

    if ((watchdog_status.state == AMS_WATCHDOG_PLATFORM_START_AMBIGUOUS_TERMINAL) ||
        (watchdog_status.state == AMS_WATCHDOG_PLATFORM_FEED_FAILED_TERMINAL)) {
        return AMS_WATCHDOG_START_RESULT_AMBIGUOUS_TERMINAL;
    }

    if (watchdog_status.state != AMS_WATCHDOG_PLATFORM_READY_NOT_STARTED) {
        return AMS_WATCHDOG_START_RESULT_NOT_READY;
    }

    watchdog_status.start_attempt_count =
        saturating_increment(watchdog_status.start_attempt_count);

    /* Zephyr STM32 4.4 enables IWDG at the beginning of wdt_setup(), before
     * its status-register ready wait can return -ENODEV. Mark ambiguity BEFORE
     * crossing that irreversible call boundary. */
    watchdog_status.state = AMS_WATCHDOG_PLATFORM_START_AMBIGUOUS_TERMINAL;
    ret = wdt_setup(watchdog_dev, 0U);
    if (ret != 0) {
        watchdog_status.last_error = ret;
        return AMS_WATCHDOG_START_RESULT_AMBIGUOUS_TERMINAL;
    }

    watchdog_status.last_error = 0;
    watchdog_status.state = AMS_WATCHDOG_PLATFORM_STARTED;
    return AMS_WATCHDOG_START_RESULT_STARTED;
}

int ams_watchdog_platform_feed(void)
{
    int ret;

    if (watchdog_status.state != AMS_WATCHDOG_PLATFORM_STARTED) {
        return -EACCES;
    }

    ret = wdt_feed(watchdog_dev, watchdog_status.channel_id);
    if (ret != 0) {
        watchdog_status.feed_failure_count =
            saturating_increment(watchdog_status.feed_failure_count);
        watchdog_status.last_error = ret;
        watchdog_status.state = AMS_WATCHDOG_PLATFORM_FEED_FAILED_TERMINAL;
        return ret;
    }

    watchdog_status.feed_success_count =
        saturating_increment(watchdog_status.feed_success_count);
    watchdog_status.last_error = 0;
    return 0;
}

ams_watchdog_platform_status_t ams_watchdog_platform_status(void)
{
    return watchdog_status;
}

#ifdef AMS_WATCHDOG_ADAPTER_TEST
void ams_watchdog_platform_test_reset(void)
{
    watchdog_status = (ams_watchdog_platform_status_t){
        .state = AMS_WATCHDOG_PLATFORM_UNPREPARED,
        .channel_id = -1,
    };
    reset_cause_captured = false;
}

void ams_watchdog_platform_test_seed_counters(uint32_t prepare_count,
                                               uint32_t start_count,
                                               uint32_t feed_success_count,
                                               uint32_t feed_failure_count)
{
    watchdog_status.prepare_attempt_count = prepare_count;
    watchdog_status.start_attempt_count = start_count;
    watchdog_status.feed_success_count = feed_success_count;
    watchdog_status.feed_failure_count = feed_failure_count;
}
#endif
