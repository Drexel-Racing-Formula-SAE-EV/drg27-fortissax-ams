#ifndef ZEPHYR_SYS_ATOMIC_H_
#define ZEPHYR_SYS_ATOMIC_H_
#include <stdbool.h>
#include <stdint.h>
typedef int32_t atomic_val_t;
typedef struct { atomic_val_t v; } atomic_t;
static inline atomic_val_t atomic_get(const atomic_t *a) { return __atomic_load_n(&a->v, __ATOMIC_SEQ_CST); }
static inline atomic_val_t atomic_set(atomic_t *a, atomic_val_t v) { return __atomic_exchange_n(&a->v, v, __ATOMIC_SEQ_CST); }
static inline bool atomic_cas(atomic_t *a, atomic_val_t old_value, atomic_val_t new_value)
{
    return __atomic_compare_exchange_n(&a->v, &old_value, new_value, false,
                                       __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
}
#endif
