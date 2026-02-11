#ifndef ARCH_X86_PIT_H
#define ARCH_X86_PIT_H

#include <stdint.h>

void pit_init(uint32_t hz);
uint32_t pit_get_ticks(void);

#endif
