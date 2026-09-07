#ifndef FAKE_ZEPHYR_KERNEL_H_
#define FAKE_ZEPHYR_KERNEL_H_
#include <stdint.h>

#define K_POLL_TYPE_SIGNAL 1
#define K_POLL_MODE_NOTIFY_ONLY 0
#define K_MSEC(ms) ((int)(ms))

struct k_poll_signal {
    unsigned int signaled;
    int result;
};

struct k_poll_event {
    struct k_poll_signal *signal;
};

void k_poll_signal_init(struct k_poll_signal *signal);
int k_poll_signal_reset(struct k_poll_signal *signal);
void k_poll_event_init(struct k_poll_event *event, int type, int mode,
                       struct k_poll_signal *signal);
int k_poll(struct k_poll_event *events, int num_events, int timeout_ms);
void k_poll_signal_check(struct k_poll_signal *signal,
                         unsigned int *signaled,
                         int *result);
uint32_t k_uptime_get_32(void);

#endif
