#include <stdint.h>

#include "arch/x86/idt.h"
#include "drivers/serial.h"
#include "drivers/vga.h"

int main(int argc, char **argv);

void kernel_main(uint32_t multiboot_magic, uint32_t multiboot_info_addr) {
    (void)multiboot_magic;
    (void)multiboot_info_addr;

    serial_init();
    idt_init();
    vga_clear();

    serial_puts("[moon-kernel] IDT loaded (256 entries)\n");
    serial_puts("[moon-kernel] entering generated MoonBit main\n");
    vga_puts("[moon-kernel] booting MoonBit path\n");

    (void)main(0, (char **)0);

    serial_puts("[moon-kernel] MoonBit main returned\n");
    vga_puts("[moon-kernel] MoonBit main returned\n");
}
