#include "arch/x86/pit.h"

#include <stdint.h>

#include "arch/x86/isr_dispatch.h"
#include "drivers/serial.h"

#define PIT_BASE_FREQUENCY_HZ 1193182u
#define PIT_COMMAND_PORT 0x43u
#define PIT_CHANNEL0_PORT 0x40u
#define PIT_MODE_RATE_GENERATOR 0x34u

static volatile uint32_t g_pit_ticks;
static volatile uint32_t g_heartbeat_countdown;
static volatile uint32_t g_heartbeat_reload;

static inline void outb(uint16_t port, uint8_t value) {
    __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

static void pit_program(uint32_t hz) {
    uint32_t divisor;
    uint16_t divisor16;

    if (hz == 0u) {
        hz = 100u;
    }

    divisor = PIT_BASE_FREQUENCY_HZ / hz;
    if (divisor == 0u) {
        divisor = 1u;
    } else if (divisor > 0xFFFFu) {
        divisor = 0xFFFFu;
    }

    divisor16 = (uint16_t)divisor;

    outb(PIT_COMMAND_PORT, PIT_MODE_RATE_GENERATOR);
    outb(PIT_CHANNEL0_PORT, (uint8_t)(divisor16 & 0xFFu));
    outb(PIT_CHANNEL0_PORT, (uint8_t)((divisor16 >> 8) & 0xFFu));
}

static void pit_irq0_handler(uint8_t irq_line, const struct isr_frame *frame) {
    (void)irq_line;
    (void)frame;

    g_pit_ticks++;

    if (g_heartbeat_reload == 0u) {
        return;
    }

    if (g_heartbeat_countdown > 0u) {
        g_heartbeat_countdown--;
    }

    if (g_heartbeat_countdown == 0u) {
        serial_puts("[pit] heartbeat\n");
        g_heartbeat_countdown = g_heartbeat_reload;
    }
}

void pit_init(uint32_t hz) {
    if (hz == 0u) {
        hz = 100u;
    }

    g_pit_ticks = 0u;
    g_heartbeat_reload = hz;
    g_heartbeat_countdown = hz;

    pit_program(hz);
    isr_register_irq_handler(0u, pit_irq0_handler);
}

uint32_t pit_get_ticks(void) {
    return g_pit_ticks;
}
