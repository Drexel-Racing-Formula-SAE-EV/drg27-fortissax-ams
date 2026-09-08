#ifndef ZEPHYR_FATAL_H_
#define ZEPHYR_FATAL_H_
struct arch_esf { unsigned int unused; };
void k_fatal_halt(unsigned int reason);
#endif
