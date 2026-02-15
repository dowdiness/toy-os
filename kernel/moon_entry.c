#include <stdint.h>

#include "arch/x86/idt.h"
#include "arch/x86/keyboard.h"
#include "arch/x86/pic.h"
#include "arch/x86/pit.h"
#include "drivers/serial.h"
#include "drivers/vga.h"
#include "kernel/fmt.h"
#include "kernel/multiboot.h"
#include "kernel/paging.h"
#include "kernel/pmm.h"
#include "runtime/heap.h"

#define INITIAL_HEAP_PAGES 256u

int main(int argc, char **argv);
extern char __kernel_end[];

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
    const struct multiboot_info *mbi = (const struct multiboot_info *)(uintptr_t)multiboot_info_addr;
    uint32_t kernel_end;
    uint32_t ram_top;
    uint32_t heap_base;

    serial_init();
    idt_init();
    pic_remap(0x20u, 0x28u);
    irq_baseline_masking();
    pit_init(100u);
    keyboard_init();
    vga_clear();

    serial_puts("[moon-kernel] IDT loaded (256 entries)\n");
    serial_puts("[moon-kernel] PIC remapped (0x20-0x2F)\n");
    serial_puts("[moon-kernel] PIT IRQ0 + Keyboard IRQ1 enabled\n");

    if (multiboot_magic != MULTIBOOT_MAGIC) {
        serial_puts("[moon-kernel] ERROR: invalid multiboot magic\n");
        cpu_idle_forever();
    }

    multiboot_dump_mmap(mbi);

    kernel_end = (uint32_t)(uintptr_t)&__kernel_end;
    ram_top = pmm_init(kernel_end, mbi);
    if (ram_top == 0u) {
        serial_puts("[pmm] ERROR: init failed\n");
        cpu_idle_forever();
    }
    pmm_dump_stats();

    paging_init(ram_top);

    heap_base = pmm_alloc_contiguous(INITIAL_HEAP_PAGES);
    if (heap_base == 0u) {
        serial_puts("[heap] ERROR: cannot allocate contiguous heap pages\n");
        cpu_idle_forever();
    }
    heap_init((void *)(uintptr_t)heap_base, INITIAL_HEAP_PAGES * PMM_PAGE_SIZE);
    serial_puts("[heap] initialized (1 MiB at ");
    put_hex32(heap_base, serial_puts, serial_putchar);
    serial_puts(")\n");

    enable_interrupts();
    serial_puts("[moon-kernel] interrupts enabled\n");
    serial_puts("[moon-kernel] entering generated MoonBit main\n");
    vga_puts("[moon-kernel] booting MoonBit path\n");

    (void)main(0, (char **)0);

    serial_puts("[moon-kernel] MoonBit main returned\n");
    vga_puts("[moon-kernel] MoonBit main returned\n");

    cpu_idle_forever();
}
