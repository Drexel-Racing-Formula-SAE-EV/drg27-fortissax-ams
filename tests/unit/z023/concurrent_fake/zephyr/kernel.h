#ifndef Z023_CONCURRENT_KERNEL_H
#define Z023_CONCURRENT_KERNEL_H
#include <stdint.h>
#include <stdbool.h>
#include <stdatomic.h>
typedef uintptr_t k_tid_t;
extern _Thread_local uintptr_t fake_thread;
extern _Thread_local bool fake_isr;
struct k_spinlock {atomic_uint locked;};
typedef struct {unsigned key;} k_spinlock_key_t;
static inline k_spinlock_key_t k_spin_lock(struct k_spinlock *s)
{while(atomic_exchange_explicit(&s->locked,1,memory_order_acquire)){} return (k_spinlock_key_t){0};}
static inline void k_spin_unlock(struct k_spinlock *s,k_spinlock_key_t key)
{(void)key;atomic_store_explicit(&s->locked,0,memory_order_release);}
static inline bool k_is_in_isr(void){return fake_isr;}
static inline k_tid_t k_current_get(void){return fake_thread;}
static inline uint32_t k_uptime_get_32(void){return 0;}
#endif
