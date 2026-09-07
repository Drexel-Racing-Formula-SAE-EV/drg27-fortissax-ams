#include <ams_core/ams_imd.h>

#include <math.h>
#include <stddef.h>

/* The original Cortex-M implementation uses __DMB() around its ISR/thread
 * capture seqlock. GCC/Clang's full atomic fence gives the same required
 * compiler + CPU ordering without importing Zephyr or STM32 HAL into the
 * portable core. */
static void ams_imd_memory_barrier(void)
{
#if defined(__GNUC__) || defined(__clang__)
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
#else
    volatile uint32_t barrier = 0U;
    (void)barrier;
#endif
}

void ams_imd_force_fail_closed(ams_imd_t *dev)
{
    if (dev == NULL) {
        return;
    }

    dev->ret = 1;
    dev->ok_hs = false;
    dev->status = AMS_IMD_UNKNOWN;
    dev->duty_percent = 0.0f;
    dev->frequency_hz = 0.0f;
}

static int imd_fail_closed(ams_imd_t *dev)
{
    ams_imd_force_fail_closed(dev);
    return 1;
}

void ams_imd_init(ams_imd_t *dev,
                  uint32_t clock_freq_hz,
                  bool capture_started)
{
    if (dev == NULL) {
        return;
    }

    dev->clock_freq_hz = clock_freq_hz;
    dev->high_count = 0U;
    dev->total_count = 0U;
    dev->duty_percent = 0.0f;
    dev->frequency_hz = 0.0f;

    /* Fail closed until both capture and the independent OK_HS input have
     * produced a valid observation. */
    dev->ret = 1;
    dev->ok_hs = false;
    dev->status = AMS_IMD_UNKNOWN;

    dev->capture_started = capture_started;
    dev->capture_seen = false;
    dev->capture_sequence = 0U;
    dev->captured_high_count = 0U;
    dev->captured_total_count = 0U;
    dev->capture_count = 0U;
    dev->last_capture_tick_ms = 0U;
}

void ams_imd_set_capture_started(ams_imd_t *dev, bool capture_started)
{
    if (dev == NULL) {
        return;
    }

    dev->capture_started = capture_started;
    if (!capture_started) {
        ams_imd_force_fail_closed(dev);
    }
}

void ams_imd_capture_publish(ams_imd_t *dev,
                             uint32_t high_count,
                             uint32_t total_count,
                             uint32_t now_ms)
{
    uint32_t sequence;

    if ((dev == NULL) || !dev->capture_started) {
        return;
    }

    /* Preserve the v2.6.27 coherent tuple publication. The Zephyr STM32 PWM
     * capture callback already receives period and pulse from the same reset-
     * mode capture event; the seqlock protects that tuple from task reads. */
    sequence = dev->capture_sequence;
    if ((sequence & 1U) != 0U) {
        sequence++;
    }

    dev->capture_sequence = sequence + 1U;
    ams_imd_memory_barrier();

    dev->captured_total_count = total_count;
    dev->captured_high_count = high_count;
    dev->last_capture_tick_ms = now_ms;

    if (dev->capture_count != UINT32_MAX) {
        dev->capture_count++;
    }

    dev->capture_seen = true;

    ams_imd_memory_barrier();
    dev->capture_sequence = sequence + 2U;
}

static bool imd_capture_snapshot(const ams_imd_t *dev,
                                 uint32_t *high_count,
                                 uint32_t *total_count,
                                 uint32_t *capture_tick_ms)
{
    if ((dev == NULL) ||
        (high_count == NULL) ||
        (total_count == NULL) ||
        (capture_tick_ms == NULL)) {
        return false;
    }

    for (uint8_t attempt = 0U; attempt < 3U; ++attempt) {
        uint32_t before = dev->capture_sequence;
        bool seen;

        if ((before & 1U) != 0U) {
            continue;
        }

        ams_imd_memory_barrier();
        *total_count = dev->captured_total_count;
        *high_count = dev->captured_high_count;
        *capture_tick_ms = dev->last_capture_tick_ms;
        seen = dev->capture_seen;
        ams_imd_memory_barrier();

        if (seen &&
            (before == dev->capture_sequence) &&
            ((dev->capture_sequence & 1U) == 0U)) {
            return true;
        }
    }

    return false;
}

int ams_imd_read_at(ams_imd_t *dev,
                    bool status_sample_valid,
                    bool ok_hs,
                    uint32_t now_ms)
{
    uint32_t captured_high;
    uint32_t captured_total;
    uint32_t captured_tick_ms;

    if ((dev == NULL) ||
        !dev->capture_started ||
        (dev->clock_freq_hz == 0U) ||
        !status_sample_valid) {
        return imd_fail_closed(dev);
    }

    /* Capture registers retain old values indefinitely. A real capture must
     * have occurred, and the tuple may be at most 250 ms old. Unsigned tick
     * subtraction intentionally preserves v2.6.27 wrap semantics. */
    if (!imd_capture_snapshot(dev,
                              &captured_high,
                              &captured_total,
                              &captured_tick_ms) ||
        ((uint32_t)(now_ms - captured_tick_ms) >
         AMS_IMD_CAPTURE_TIMEOUT_MS)) {
        return imd_fail_closed(dev);
    }

    dev->ok_hs = ok_hs;
    dev->total_count = captured_total;

    if (dev->total_count == 0U) {
        return imd_fail_closed(dev);
    }

    dev->high_count = captured_high;
    if (dev->high_count > dev->total_count) {
        return imd_fail_closed(dev);
    }

    /* Convert before multiplying so full-width 32-bit capture values cannot
     * overflow in integer arithmetic. */
    dev->duty_percent =
        ((float)dev->high_count * 100.0f) /
        (float)dev->total_count;
    dev->frequency_hz =
        (float)dev->clock_freq_hz /
        (float)dev->total_count;

    if (!isfinite(dev->duty_percent) ||
        !isfinite(dev->frequency_hz) ||
        (dev->duty_percent < 0.0f) ||
        (dev->duty_percent > 100.0f)) {
        return imd_fail_closed(dev);
    }

    /* Preserve the v2.6.27 10 Hz status encoding and its explicit guard
     * before float-to-int conversion. */
    float rounded_status = 0.5f + (dev->frequency_hz / 10.0f);

    if (!isfinite(rounded_status) ||
        (rounded_status < 0.0f) ||
        (rounded_status >= ((float)AMS_IMD_GROUND_FAULT + 1.0f))) {
        return imd_fail_closed(dev);
    }

    int status_code = (int)rounded_status;

    if ((status_code >= (int)AMS_IMD_SHORT_TO_CHASSIS_GROUND) &&
        (status_code <= (int)AMS_IMD_GROUND_FAULT)) {
        dev->status = (ams_imd_status_t)status_code;
        dev->ret = 0;
        return 0;
    }

    dev->status = AMS_IMD_UNKNOWN;
    return imd_fail_closed(dev);
}

bool ams_imd_is_ok(const ams_imd_t *dev)
{
    return (dev != NULL) &&
           (dev->ret == 0) &&
           dev->ok_hs &&
           (dev->status == AMS_IMD_NORMAL);
}
