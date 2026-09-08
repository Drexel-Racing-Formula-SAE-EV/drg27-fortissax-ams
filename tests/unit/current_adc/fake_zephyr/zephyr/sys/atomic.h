#ifndef ZEPHYR_SYS_ATOMIC_H_
#define ZEPHYR_SYS_ATOMIC_H_
#include <stdint.h>
typedef int32_t atomic_val_t;
typedef int32_t atomic_t;
#define ATOMIC_INIT(v) (v)
static inline atomic_val_t atomic_get(const atomic_t *a) { return *a; }
static inline void atomic_set(atomic_t *a, atomic_val_t v) { *a=v; }
static inline int atomic_cas(atomic_t *a, atomic_val_t oldv, atomic_val_t newv) { if (*a != oldv) return 0; *a=newv; return 1; }
#endif
