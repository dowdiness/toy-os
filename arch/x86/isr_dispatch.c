#include "arch/x86/isr_dispatch.h"

#include <stdint.h>

#include "arch/x86/pic.h"
#include "drivers/serial.h"
#include "kernel/fmt.h"

#define IRQ_VECTOR_BASE 32u
#define IRQ_VECTOR_COUNT 16u

static irq_handler_t g_irq_handlers[IRQ_VECTOR_COUNT];

static void isr_halt_forever(void) __attribute__((noreturn));

static void isr_halt_forever(void) {
    __asm__ volatile("cli");
    for (;;) {
        __asm__ volatile("hlt");
    }
}

static void isr_panic(const struct isr_frame *frame) __attribute__((noreturn));

static inline uint32_t isr_cr2_read(void) {
    uint32_t val;
    __asm__ volatile("mov %%cr2, %0" : "=r"(val));
    return val;
}

static void isr_panic(const struct isr_frame *frame) {
    serial_puts("[isr] PANIC exception vector=");
    put_hex32(frame->vector, serial_puts, serial_putchar);
    serial_puts(" error=");
    put_hex32(frame->error_code, serial_puts, serial_putchar);
    serial_puts(" eip=");
    put_hex32(frame->eip, serial_puts, serial_putchar);
    serial_puts(" cs=");
    put_hex32(frame->cs, serial_puts, serial_putchar);
    serial_puts(" eflags=");
    put_hex32(frame->eflags, serial_puts, serial_putchar);
    if (frame->vector == 14u) {
        serial_puts(" cr2=");
        put_hex32(isr_cr2_read(), serial_puts, serial_putchar);
    }
    serial_puts("\n");

    serial_puts("[isr] regs eax=");
    put_hex32(frame->eax, serial_puts, serial_putchar);
    serial_puts(" ebx=");
    put_hex32(frame->ebx, serial_puts, serial_putchar);
    serial_puts(" ecx=");
    put_hex32(frame->ecx, serial_puts, serial_putchar);
    serial_puts(" edx=");
    put_hex32(frame->edx, serial_puts, serial_putchar);
    serial_puts(" esi=");
    put_hex32(frame->esi, serial_puts, serial_putchar);
    serial_puts(" edi=");
    put_hex32(frame->edi, serial_puts, serial_putchar);
    serial_puts(" ebp=");
    put_hex32(frame->ebp, serial_puts, serial_putchar);
    serial_puts("\n");

    isr_halt_forever();
}

static int isr_is_spurious_irq(uint8_t irq_line) {
    uint16_t isr;
    uint16_t bit;

    if (irq_line != 7u && irq_line != 15u) {
        return 0;
    }

    isr = pic_get_isr();
    bit = (uint16_t)(1u << irq_line);
    if ((isr & bit) != 0u) {
        return 0;
    }

    if (irq_line == 15u) {
        /* Spurious slave IRQ requires EOI on cascade line only. */
        pic_send_eoi(2u);
    }
    return 1;
}

void isr_register_irq_handler(uint8_t irq_line, irq_handler_t handler) {
    if (irq_line >= IRQ_VECTOR_COUNT) {
        return;
    }
    g_irq_handlers[irq_line] = handler;
}

void isr_unregister_irq_handler(uint8_t irq_line) {
    if (irq_line >= IRQ_VECTOR_COUNT) {
        return;
    }
    g_irq_handlers[irq_line] = (irq_handler_t)0;
}

void isr_common_handler(struct isr_frame *frame) {
    uint8_t irq_line;
    irq_handler_t irq_handler;

    if (frame->vector < IRQ_VECTOR_BASE) {
        isr_panic(frame);
    }

    if (frame->vector >= IRQ_VECTOR_BASE && frame->vector < IRQ_VECTOR_BASE + IRQ_VECTOR_COUNT) {
        irq_line = (uint8_t)(frame->vector - IRQ_VECTOR_BASE);
        if (isr_is_spurious_irq(irq_line) != 0) {
            serial_puts("[isr] spurious irq=");
            put_hex32(irq_line, serial_puts, serial_putchar);
            serial_puts("\n");
            return;
        }

        irq_handler = g_irq_handlers[irq_line];
        if (irq_handler != (irq_handler_t)0) {
            irq_handler(irq_line, frame);
        }

        pic_send_eoi(irq_line);
        return;
    }

    serial_puts("[isr] unexpected vector=");
    put_hex32(frame->vector, serial_puts, serial_putchar);
    serial_puts("\n");
}
