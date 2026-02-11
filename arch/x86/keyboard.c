#include "arch/x86/keyboard.h"

#include <stdint.h>

#include "arch/x86/isr_dispatch.h"
#include "drivers/serial.h"
#include "kernel/fmt.h"

#define KBD_DATA_PORT 0x60u
#define KBD_STATUS_PORT 0x64u
#define KBD_STATUS_OUTPUT_FULL 0x01u
#define KBD_EVENT_QUEUE_SIZE 64u
#define KBD_EVENT_VALID 0x40000000u
#define KBD_EVENT_RELEASE 0x20000000u
#define KBD_EVENT_EXTENDED 0x10000000u

static uint8_t g_extended_prefix;
static volatile uint32_t g_event_head;
static volatile uint32_t g_event_tail;
static uint32_t g_event_queue[KBD_EVENT_QUEUE_SIZE];

static inline uint8_t inb(uint16_t port) {
    uint8_t value;
    __asm__ volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline uint32_t irq_save_disable(void) {
    uint32_t flags;
    __asm__ volatile("pushfl; popl %0; cli" : "=r"(flags) : : "memory");
    return flags;
}

static inline void irq_restore(uint32_t flags) {
    __asm__ volatile("pushl %0; popfl" : : "r"(flags) : "memory", "cc");
}

static void keyboard_enqueue_event(uint32_t event) {
    uint32_t next_head;

    next_head = (g_event_head + 1u) % KBD_EVENT_QUEUE_SIZE;
    if (next_head == g_event_tail) {
        return;
    }

    g_event_queue[g_event_head] = event;
    g_event_head = next_head;
}

static void keyboard_irq1_handler(uint8_t irq_line, const struct isr_frame *frame) {
    uint8_t status;
    uint8_t scancode;
    uint32_t event;
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

    event = KBD_EVENT_VALID | (uint32_t)(scancode & 0x7Fu);
    logged_code = scancode;
    if (g_extended_prefix != 0u) {
        event |= KBD_EVENT_EXTENDED;
        logged_code = 0xE000u | (uint32_t)scancode;
        g_extended_prefix = 0u;
    }
    if ((scancode & 0x80u) != 0u) {
        event |= KBD_EVENT_RELEASE;
    }

    keyboard_enqueue_event(event);

    serial_puts("[kbd] scancode=");
    put_hex32(logged_code, serial_puts, serial_putchar);
    if ((scancode & 0x80u) != 0u) {
        serial_puts(" release");
    } else {
        serial_puts(" press");
    }
    serial_puts("\n");
}

int32_t keyboard_pop_event(void) {
    uint32_t flags;
    uint32_t event;

    flags = irq_save_disable();
    if (g_event_head == g_event_tail) {
        irq_restore(flags);
        return 0;
    }

    event = g_event_queue[g_event_tail];
    g_event_tail = (g_event_tail + 1u) % KBD_EVENT_QUEUE_SIZE;
    irq_restore(flags);
    return (int32_t)event;
}

void keyboard_init(void) {
    g_extended_prefix = 0u;
    g_event_head = 0u;
    g_event_tail = 0u;
    isr_register_irq_handler(1u, keyboard_irq1_handler);
}
