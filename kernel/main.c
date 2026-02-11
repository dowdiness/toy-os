#include <stdint.h>
#include "drivers/vga.h"
#include "drivers/serial.h"

#define MULTIBOOT_BOOTLOADER_MAGIC 0x2BADB002u

static void vga_put_hex32(uint32_t value) {
    static const char hex[] = "0123456789ABCDEF";
    int shift;

    vga_puts("0x");
    for (shift = 28; shift >= 0; shift -= 4) {
        vga_putchar(hex[(value >> shift) & 0x0Fu]);
    }
}

static void serial_put_hex32(uint32_t value) {
    static const char hex[] = "0123456789ABCDEF";
    int shift;

    serial_puts("0x");
    for (shift = 28; shift >= 0; shift -= 4) {
        serial_putchar(hex[(value >> shift) & 0x0Fu]);
    }
}

void kernel_main(uint32_t multiboot_magic, uint32_t multiboot_info_addr) {
    serial_init();
    serial_puts("COM1 serial initialized.\n");

    vga_clear();
    vga_puts("Hello from bare metal C kernel!\n");
    vga_puts("Multiboot magic: ");
    vga_put_hex32(multiboot_magic);
    vga_puts("\n");
    vga_puts("Multiboot info:  ");
    vga_put_hex32(multiboot_info_addr);
    vga_puts("\n");

    serial_puts("Hello from bare metal C kernel!\n");
    serial_puts("Multiboot magic: ");
    serial_put_hex32(multiboot_magic);
    serial_puts("\n");
    serial_puts("Multiboot info:  ");
    serial_put_hex32(multiboot_info_addr);
    serial_puts("\n");

    if (multiboot_magic != MULTIBOOT_BOOTLOADER_MAGIC) {
        vga_puts("ERROR: Invalid multiboot magic.\n");
        serial_puts("ERROR: Invalid multiboot magic.\n");
        return;
    }

    vga_puts("Kernel C path is running.\n");
    serial_puts("Kernel C path is running.\n");
}
