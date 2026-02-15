#include "kernel/multiboot.h"

#include <stdint.h>

#include "drivers/serial.h"
#include "kernel/fmt.h"

uint32_t multiboot_scan_mmap(const struct multiboot_info *info,
                             void (*callback)(uint32_t base, uint32_t length, int available)) {
    uint32_t offset;
    uint32_t count;
    const struct multiboot_mmap_entry *entry;

    if ((info->flags & MULTIBOOT_FLAG_MMAP) == 0u) {
        serial_puts("[mmap] ERROR: no memory map from bootloader\n");
        serial_puts("[mmap] info->flags = ");
        put_hex32(info->flags, serial_puts, serial_putchar);
        serial_puts("\n");
        return 0;
    }

    offset = 0u;
    count = 0u;

    while (offset < info->mmap_length) {
        if (offset + 4u > info->mmap_length) {
            break;
        }

        entry = (const struct multiboot_mmap_entry *)(uintptr_t)(info->mmap_addr + offset);

        if (entry->size == 0u) {
            serial_puts("[mmap] WARN: entry with size=0, stopping scan\n");
            break;
        }

        if (offset + entry->size + 4u > info->mmap_length) {
            serial_puts("[mmap] WARN: entry overflows mmap_length, stopping scan\n");
            break;
        }

        if (entry->addr <= 0xFFFFFFFFull && entry->addr + entry->len <= 0x100000000ull) {
            uint32_t base = (uint32_t)entry->addr;
            uint32_t length = (uint32_t)entry->len;
            int available = (entry->type == MULTIBOOT_MMAP_TYPE_AVAILABLE) ? 1 : 0;
            callback(base, length, available);
        }

        offset += entry->size + 4u;
        count++;
    }

    return count;
}

static void mmap_debug_print(uint32_t base, uint32_t length, int available) {
    serial_puts("  ");
    put_hex32(base, serial_puts, serial_putchar);
    serial_puts(" - ");
    put_hex32(base + length, serial_puts, serial_putchar);
    serial_puts(available ? " [available]" : " [reserved]");
    serial_puts("\n");
}

void multiboot_dump_mmap(const struct multiboot_info *info) {
    uint32_t count;

    serial_puts("[mmap] Memory map:\n");
    count = multiboot_scan_mmap(info, mmap_debug_print);
    serial_puts("[mmap] entries: ");
    put_hex32(count, serial_puts, serial_putchar);
    serial_puts("\n");
}
