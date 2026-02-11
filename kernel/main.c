#include <stdint.h>
#include "arch/x86/idt.h"
#include "arch/x86/pic.h"
#include "drivers/vga.h"
#include "drivers/serial.h"
#include "kernel/fmt.h"

#define MULTIBOOT_BOOTLOADER_MAGIC 0x2BADB002u

void kernel_main(uint32_t multiboot_magic, uint32_t multiboot_info_addr) {
    serial_init();
    serial_puts("COM1 serial initialized.\n");
    idt_init();
    serial_puts("IDT loaded (256 entries).\n");
    pic_remap(0x20u, 0x28u);
    serial_puts("PIC remapped to vectors 0x20-0x2F.\n");

    vga_clear();
    vga_puts("Hello from bare metal C kernel!\n");
    vga_puts("Multiboot magic: ");
    put_hex32(multiboot_magic, vga_puts, vga_putchar);
    vga_puts("\n");
    vga_puts("Multiboot info:  ");
    put_hex32(multiboot_info_addr, vga_puts, vga_putchar);
    vga_puts("\n");

    serial_puts("Hello from bare metal C kernel!\n");
    serial_puts("Multiboot magic: ");
    put_hex32(multiboot_magic, serial_puts, serial_putchar);
    serial_puts("\n");
    serial_puts("Multiboot info:  ");
    put_hex32(multiboot_info_addr, serial_puts, serial_putchar);
    serial_puts("\n");

    if (multiboot_magic != MULTIBOOT_BOOTLOADER_MAGIC) {
        vga_puts("ERROR: Invalid multiboot magic.\n");
        serial_puts("ERROR: Invalid multiboot magic.\n");
        return;
    }

    vga_puts("Kernel C path is running.\n");
    serial_puts("Kernel C path is running.\n");
}
