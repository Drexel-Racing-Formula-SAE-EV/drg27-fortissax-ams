#ifndef TEST_ZEPHYR_KERNEL_H
#define TEST_ZEPHYR_KERNEL_H
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
typedef uintptr_t k_tid_t;
extern uint32_t fake_now;extern uintptr_t fake_thread;extern bool fake_isr;extern bool fake_locked;
struct k_mutex { int unused; };struct k_spinlock { int unused; };
typedef struct { unsigned int key; } k_spinlock_key_t;
#define K_MUTEX_DEFINE(name) struct k_mutex name
#define K_MSEC(n) (n)
static inline int k_mutex_lock(struct k_mutex *m,int t){(void)m;(void)t;
#ifdef AMS_TEST_MUTEX_FAILURE
 extern bool fake_mutex_failure;
 if(fake_mutex_failure)return -1;
#endif
 fake_locked=true;return 0;}
static inline int k_mutex_unlock(struct k_mutex *m){(void)m;fake_locked=false;return 0;}
static inline uint32_t k_uptime_get_32(void){return fake_now;}
static inline k_tid_t k_current_get(void){return fake_thread;}
static inline bool k_is_in_isr(void){return fake_isr;}
static inline k_spinlock_key_t k_spin_lock(struct k_spinlock *l){(void)l;return (k_spinlock_key_t){0};}
static inline void k_spin_unlock(struct k_spinlock *l,k_spinlock_key_t k){(void)l;(void)k;}
#endif
