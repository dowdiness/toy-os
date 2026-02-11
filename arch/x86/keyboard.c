#include "arch/x86/keyboard.h"

#include <stdint.h>

#include "arch/x86/isr_dispatch.h"
#include "drivers/serial.h"
#include "kernel/fmt.h"

#define KBD_DATA_PORT 0x60u
#define KBD_STATUS_PORT 0x64u
#define KBD_STATUS_OUTPUT_FULL 0x01u

static uint8_t g_extended_prefix;

static inline uint8_t inb(uint16_t port) {
    uint8_t value;
    __asm__ volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static void keyboard_irq1_handler(uint8_t irq_line, const struct isr_frame *frame) {
    uint8_t status;
    uint8_t scancode;
    uint32_t logged_code;

    (void)irq_line;
    (void)frame;

    status = inb(KBD_STATUS_PORT);
    if ((status & KBD_STATUS_OUTPUT_FULL) == 0u) {
        return;
    }

    scancode = inb(KBD_DATA_PORT);
    if (scancode == 0xE0u) {
        g_extended_prefix = 1u;
        return;
    }

    logged_code = scancode;
    if (g_extended_prefix != 0u) {
        logged_code = 0xE000u | (uint32_t)scancode;
        g_extended_prefix = 0u;
    }

    serial_puts("[kbd] scancode=");
    put_hex32(logged_code, serial_puts, serial_putchar);
    if ((scancode & 0x80u) != 0u) {
        serial_puts(" release");
    } else {
        serial_puts(" press");
    }
    serial_puts("\n");
}

void keyboard_init(void) {
    g_extended_prefix = 0u;
    isr_register_irq_handler(1u, keyboard_irq1_handler);
}
