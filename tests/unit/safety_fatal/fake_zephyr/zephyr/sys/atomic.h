#ifndef ZEPHYR_SYS_ATOMIC_H_
#define ZEPHYR_SYS_ATOMIC_H_
typedef int atomic_t;
static inline int atomic_get(const atomic_t *v) { return *v; }
static inline void atomic_set(atomic_t *v, int x) { *v = x; }
#endif
