#ifndef ARCH_X86_IDT_H
#define ARCH_X86_IDT_H

#include <stdint.h>

void idt_init(void);
void idt_load(void);
void idt_set_interrupt_gate(uint8_t vector, void (*handler)(void));

#endif
