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

#if defined(PHASE3_HEAP_TEST)
static void heap_selftest(void) {
    int i;

    serial_puts("[heap-test] start\n");

    {
        void *p1 = heap_malloc(128u);
        void *p2;
        if (p1 == (void *)0) {
            serial_puts("[heap-test] FAIL: malloc(128) returned NULL\n");
            return;
        }
        serial_puts("[heap-test] malloc(128) OK: ");
        put_hex32((uint32_t)(uintptr_t)p1, serial_puts, serial_putchar);
        serial_puts("\n");

        heap_free(p1);
        serial_puts("[heap-test] free OK\n");

        p2 = heap_malloc(128u);
        if (p2 == (void *)0) {
            serial_puts("[heap-test] FAIL: re-malloc returned NULL\n");
            return;
        }
        serial_puts("[heap-test] re-malloc(128) OK: ");
        put_hex32((uint32_t)(uintptr_t)p2, serial_puts, serial_putchar);
        serial_puts("\n");

        if ((uint32_t)(uintptr_t)p2 <= (uint32_t)(uintptr_t)p1) {
            serial_puts("[heap-test] free-list reuse: OK\n");
        } else {
            serial_puts("[heap-test] WARN: no reuse (may be OK if layout differs)\n");
        }
        heap_free(p2);
    }

    {
        uint8_t *p3 = (uint8_t *)heap_calloc(64u, 1u);
        int all_zero = 1;
        if (p3 == (uint8_t *)0) {
            serial_puts("[heap-test] FAIL: calloc returned NULL\n");
            return;
        }
        for (i = 0; i < 64; ++i) {
            if (p3[i] != 0u) {
                all_zero = 0;
                break;
            }
        }
        if (all_zero != 0) {
            serial_puts("[heap-test] calloc zero-init: OK\n");
        } else {
            serial_puts("[heap-test] FAIL: calloc not zeroed\n");
            return;
        }
        heap_free(p3);
    }

    {
        uint8_t *p4 = (uint8_t *)heap_malloc(32u);
        uint8_t *p5;
        int data_ok = 1;
        if (p4 == (uint8_t *)0) {
            serial_puts("[heap-test] FAIL: malloc(32) returned NULL\n");
            return;
        }
        for (i = 0; i < 32; ++i) {
            p4[i] = (uint8_t)i;
        }
        p5 = (uint8_t *)heap_realloc(p4, 64u);
        if (p5 == (uint8_t *)0) {
            serial_puts("[heap-test] FAIL: realloc returned NULL\n");
            return;
        }
        for (i = 0; i < 32; ++i) {
            if (p5[i] != (uint8_t)i) {
                data_ok = 0;
                break;
            }
        }
        if (data_ok != 0) {
            serial_puts("[heap-test] realloc data preserve: OK\n");
        } else {
            serial_puts("[heap-test] FAIL: realloc data lost\n");
            return;
        }
        heap_free(p5);
    }

    {
        int stress_ok = 1;
        serial_puts("[heap-test] stress: 1000x malloc(64)+free ... ");
        for (i = 0; i < 1000; ++i) {
            void *p = heap_malloc(64u);
            if (p == (void *)0) {
                stress_ok = 0;
                break;
            }
            heap_free(p);
        }
        serial_puts(stress_ok ? "OK\n" : "FAIL\n");
        if (stress_ok == 0) {
            return;
        }
    }

    heap_free((void *)0);
    serial_puts("[heap-test] free(NULL): OK\n");
    serial_puts("[heap-test] all tests done\n");
}
#endif

void kernel_main(uint32_t multiboot_magic, uint32_t multiboot_info_addr) {
    const struct multiboot_info *mbi = (const struct multiboot_info *)(uintptr_t)multiboot_info_addr;
    uint32_t kernel_end;
    uint32_t ram_top;
    uint32_t heap_base;

    serial_init();
    serial_puts("COM1 serial initialized.\n");
    idt_init();
    serial_puts("IDT loaded (256 entries).\n");
    pic_remap(0x20u, 0x28u);
    serial_puts("PIC remapped to vectors 0x20-0x2F.\n");
    irq_baseline_masking();
    pit_init(100u);
    keyboard_init();
    serial_puts("PIT IRQ0 + Keyboard IRQ1 enabled.\n");

    if (multiboot_magic != MULTIBOOT_MAGIC) {
        serial_puts("[boot] ERROR: invalid multiboot magic\n");
        vga_puts("ERROR: Invalid multiboot magic.\n");
        return;
    }

    multiboot_dump_mmap(mbi);

    kernel_end = (uint32_t)(uintptr_t)&__kernel_end;
    ram_top = pmm_init(kernel_end, mbi);
    if (ram_top == 0u) {
        serial_puts("[pmm] ERROR: init failed\n");
        return;
    }
    pmm_dump_stats();

    paging_init(ram_top);

    heap_base = pmm_alloc_contiguous(INITIAL_HEAP_PAGES);
    if (heap_base == 0u) {
        serial_puts("[heap] ERROR: cannot allocate contiguous heap pages\n");
        return;
    }

    heap_init((void *)(uintptr_t)heap_base, INITIAL_HEAP_PAGES * PMM_PAGE_SIZE);
    serial_puts("[heap] initialized (1 MiB at ");
    put_hex32(heap_base, serial_puts, serial_putchar);
    serial_puts(")\n");

#if defined(PHASE3_HEAP_TEST)
    heap_selftest();
#endif

    vga_clear();
    vga_puts("Kernel running with paging + heap.\n");

    enable_interrupts();
    cpu_idle_forever();
}
