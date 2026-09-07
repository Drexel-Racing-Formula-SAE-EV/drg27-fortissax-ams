#ifndef FAKE_ZEPHYR_UTIL_H_
#define FAKE_ZEPHYR_UTIL_H_
#define BUILD_ASSERT(cond, msg) _Static_assert((cond), msg)
#endif
