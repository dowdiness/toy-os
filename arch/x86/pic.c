#include "arch/x86/pic.h"

#include <stdint.h>

#define PIC1_COMMAND 0x20u
#define PIC1_DATA    0x21u
#define PIC2_COMMAND 0xA0u
#define PIC2_DATA    0xA1u

#define ICW1_ICW4    0x01u
#define ICW1_INIT    0x10u
#define ICW4_8086    0x01u

static inline void outb(uint16_t port, uint8_t value) {
    __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t value;
    __asm__ volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline void io_wait(void) {
    outb(0x80u, 0u);
}

void pic_remap(uint8_t master_offset, uint8_t slave_offset) {
    uint8_t master_mask;
    uint8_t slave_mask;

    master_mask = inb(PIC1_DATA);
    slave_mask = inb(PIC2_DATA);

    outb(PIC1_COMMAND, ICW1_INIT | ICW1_ICW4);
    io_wait();
    outb(PIC2_COMMAND, ICW1_INIT | ICW1_ICW4);
    io_wait();

    outb(PIC1_DATA, master_offset);
    io_wait();
    outb(PIC2_DATA, slave_offset);
    io_wait();

    outb(PIC1_DATA, 4u);
    io_wait();
    outb(PIC2_DATA, 2u);
    io_wait();

    outb(PIC1_DATA, ICW4_8086);
    io_wait();
    outb(PIC2_DATA, ICW4_8086);
    io_wait();

    outb(PIC1_DATA, master_mask);
    outb(PIC2_DATA, slave_mask);
}

void pic_set_mask(uint8_t irq_line) {
    uint16_t port;
    uint8_t value;
    uint8_t bit;

    if (irq_line < 8u) {
        port = PIC1_DATA;
        bit = irq_line;
    } else {
        port = PIC2_DATA;
        bit = (uint8_t)(irq_line - 8u);
    }

    value = inb(port);
    value = (uint8_t)(value | (uint8_t)(1u << bit));
    outb(port, value);
}

void pic_clear_mask(uint8_t irq_line) {
    uint16_t port;
    uint8_t value;
    uint8_t bit;

    if (irq_line < 8u) {
        port = PIC1_DATA;
        bit = irq_line;
    } else {
        port = PIC2_DATA;
        bit = (uint8_t)(irq_line - 8u);
    }

    value = inb(port);
    value = (uint8_t)(value & (uint8_t)~(uint8_t)(1u << bit));
    outb(port, value);
}

void pic_send_eoi(uint8_t irq_line) {
    if (irq_line >= 8u) {
        outb(PIC2_COMMAND, 0x20u);
    }
    outb(PIC1_COMMAND, 0x20u);
}
