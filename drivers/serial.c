#include <stdint.h>

#define COM1 0x3F8

static inline void outb(uint16_t port, uint8_t value) {
    __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t value;
    __asm__ volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

void serial_init(void) {
    outb(COM1 + 1, 0x00);  /* Disable interrupts */
    outb(COM1 + 3, 0x80);  /* Enable DLAB */
    outb(COM1 + 0, 0x03);  /* Divisor low byte (38400 baud) */
    outb(COM1 + 1, 0x00);  /* Divisor high byte */
    outb(COM1 + 3, 0x03);  /* 8 bits, no parity, one stop bit */
    outb(COM1 + 2, 0xC7);  /* Enable FIFO, clear, 14-byte threshold */
    outb(COM1 + 4, 0x0B);  /* IRQs disabled, RTS/DSR set */
}

static int serial_can_transmit(void) {
    return (inb(COM1 + 5) & 0x20) != 0;
}

void serial_putchar(char ch) {
    while (!serial_can_transmit()) {
    }
    outb(COM1, (uint8_t)ch);
}

void serial_puts(const char *str) {
    while (*str != '\0') {
        if (*str == '\n') {
            serial_putchar('\r');
        }
        serial_putchar(*str);
        ++str;
    }
}
