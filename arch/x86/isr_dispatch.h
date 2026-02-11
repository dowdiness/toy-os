#ifndef ARCH_X86_ISR_DISPATCH_H
#define ARCH_X86_ISR_DISPATCH_H

#include <stdint.h>

struct isr_frame {
    uint32_t gs;
    uint32_t fs;
    uint32_t es;
    uint32_t ds;

    uint32_t edi;
    uint32_t esi;
    uint32_t ebp;
    uint32_t esp;
    uint32_t ebx;
    uint32_t edx;
    uint32_t ecx;
    uint32_t eax;

    uint32_t vector;
    uint32_t error_code;

    uint32_t eip;
    uint32_t cs;
    uint32_t eflags;
};

typedef void (*irq_handler_t)(uint8_t irq_line, const struct isr_frame *frame);

void isr_common_handler(struct isr_frame *frame);
void isr_register_irq_handler(uint8_t irq_line, irq_handler_t handler);
void isr_unregister_irq_handler(uint8_t irq_line);

#endif
