#include <stdint.h>
#include <stddef.h>

enum {
    VGA_WIDTH = 80,
    VGA_HEIGHT = 25,
};

static volatile uint16_t *const vga_buffer = (volatile uint16_t *)0xB8000;
static size_t cursor_row = 0;
static size_t cursor_col = 0;

static uint16_t vga_entry(unsigned char ch, uint8_t color) {
    return (uint16_t)ch | ((uint16_t)color << 8);
}

static void vga_scroll(void) {
    size_t i;

    for (i = 0; i < VGA_WIDTH * (VGA_HEIGHT - 1); ++i) {
        vga_buffer[i] = vga_buffer[i + VGA_WIDTH];
    }

    for (i = VGA_WIDTH * (VGA_HEIGHT - 1); i < VGA_WIDTH * VGA_HEIGHT; ++i) {
        vga_buffer[i] = vga_entry(' ', 0x07);
    }

    cursor_row = VGA_HEIGHT - 1;
}

void vga_clear(void) {
    size_t i;

    for (i = 0; i < VGA_WIDTH * VGA_HEIGHT; ++i) {
        vga_buffer[i] = vga_entry(' ', 0x07);
    }

    cursor_row = 0;
    cursor_col = 0;
}

void vga_putchar(char ch) {
    if (ch == '\n') {
        cursor_col = 0;
        if (++cursor_row >= VGA_HEIGHT) {
            vga_scroll();
        }
        return;
    }

    vga_buffer[cursor_row * VGA_WIDTH + cursor_col] = vga_entry((unsigned char)ch, 0x0F);

    if (++cursor_col >= VGA_WIDTH) {
        cursor_col = 0;
        if (++cursor_row >= VGA_HEIGHT) {
            vga_scroll();
        }
    }
}

void vga_puts(const char *str) {
    while (*str != '\0') {
        vga_putchar(*str);
        ++str;
    }
}
