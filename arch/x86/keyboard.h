#ifndef ARCH_X86_KEYBOARD_H
#define ARCH_X86_KEYBOARD_H

#include <stdint.h>

int32_t keyboard_pop_event(void);
void keyboard_init(void);

#endif
