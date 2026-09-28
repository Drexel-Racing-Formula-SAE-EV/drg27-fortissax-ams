#ifndef ZEPHYR_SPINLOCK_H_
#define ZEPHYR_SPINLOCK_H_
typedef int k_spinlock_key_t;
struct k_spinlock { int unused; };
static inline k_spinlock_key_t k_spin_lock(struct k_spinlock *l) { (void)l; return 0; }
static inline void k_spin_unlock(struct k_spinlock *l, k_spinlock_key_t k) { (void)l; (void)k; }
#endif
