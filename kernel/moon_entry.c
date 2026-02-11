#include <stdint.h>

#include "arch/x86/idt.h"
#include "arch/x86/keyboard.h"
#include "arch/x86/pic.h"
#include "arch/x86/pit.h"
#include "drivers/serial.h"
#include "drivers/vga.h"

int main(int argc, char **argv);

static void enable_interrupts(void) {
    __asm__ volatile("sti");
}

static void cpu_idle_forever(void) {
    for (;;) {
        __asm__ volatile("hlt");
    }
}

static void irq_baseline_masking(void) {
    uint8_t irq;

    for (irq = 0u; irq < 16u; ++irq) {
        pic_set_mask(irq);
    }
    pic_clear_mask(0u);
    pic_clear_mask(1u);
}

void kernel_main(uint32_t multiboot_magic, uint32_t multiboot_info_addr) {
    (void)multiboot_magic;
    (void)multiboot_info_addr;

    serial_init();
    idt_init();
    pic_remap(0x20u, 0x28u);
    irq_baseline_masking();
    pit_init(100u);
    keyboard_init();
    vga_clear();

    serial_puts("[moon-kernel] IDT loaded (256 entries)\n");
    serial_puts("[moon-kernel] PIC remapped (0x20-0x2F)\n");
    serial_puts("[moon-kernel] PIT IRQ0 enabled (100Hz)\n");
    serial_puts("[moon-kernel] Keyboard IRQ1 enabled\n");
    serial_puts("[moon-kernel] entering generated MoonBit main\n");
    vga_puts("[moon-kernel] booting MoonBit path\n");

    (void)main(0, (char **)0);

    serial_puts("[moon-kernel] MoonBit main returned\n");
    vga_puts("[moon-kernel] MoonBit main returned\n");

    enable_interrupts();
    cpu_idle_forever();
}
