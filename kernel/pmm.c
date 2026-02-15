#include "kernel/pmm.h"

#include <stdint.h>

#include "drivers/serial.h"
#include "kernel/fmt.h"
#include "kernel/multiboot.h"

static uint32_t *bitmap;
static uint32_t bitmap_size;
static uint32_t total_page_count;
static uint32_t free_page_count;
static uint32_t detected_ram_top;
static uint32_t reserved_bitmap_end;

static inline int pmm_is_page_index_valid(uint32_t page_index) {
    return page_index < total_page_count;
}

static inline int bitmap_test(uint32_t page_index) {
    return (bitmap[page_index / 32u] >> (page_index % 32u)) & 1u;
}

static inline void bitmap_set(uint32_t page_index) {
    bitmap[page_index / 32u] |= (1u << (page_index % 32u));
}

static inline void bitmap_clear(uint32_t page_index) {
    bitmap[page_index / 32u] &= ~(1u << (page_index % 32u));
}

static void pmm_mark_reserved(uint32_t start_addr, uint32_t end_addr) {
    uint32_t start_page = start_addr / PMM_PAGE_SIZE;
    uint32_t end_page = (end_addr + PMM_PAGE_SIZE - 1u) / PMM_PAGE_SIZE;
    uint32_t i;

    if (end_page > total_page_count) {
        end_page = total_page_count;
    }

    for (i = start_page; i < end_page; ++i) {
        if (!bitmap_test(i)) {
            bitmap_set(i);
            if (free_page_count > 0u) {
                free_page_count--;
            }
        }
    }
}

static void pmm_mark_free(uint32_t start_addr, uint32_t end_addr) {
    uint32_t start_page = start_addr / PMM_PAGE_SIZE;
    uint32_t end_page = (end_addr + PMM_PAGE_SIZE - 1u) / PMM_PAGE_SIZE;
    uint32_t i;

    if (end_page > total_page_count) {
        end_page = total_page_count;
    }

    for (i = start_page; i < end_page; ++i) {
        if (bitmap_test(i)) {
            bitmap_clear(i);
            free_page_count++;
        }
    }
}

static uint32_t g_ram_top_scan;

static void pmm_scan_ram_top_callback(uint32_t base, uint32_t length, int available) {
    uint32_t top;

    if (available == 0 || length == 0u) {
        return;
    }

    top = base + length;
    if (top > g_ram_top_scan) {
        g_ram_top_scan = top;
    }
}

static void pmm_mark_available_callback(uint32_t base, uint32_t length, int available) {
    if (available == 0 || length == 0u) {
        return;
    }
    pmm_mark_free(base, base + length);
}

uint32_t pmm_init(uint32_t kernel_end, const struct multiboot_info *info) {
    uint32_t i;
    uint32_t bitmap_bytes;

    g_ram_top_scan = 0u;
    if (multiboot_scan_mmap(info, pmm_scan_ram_top_callback) == 0u) {
        return 0u;
    }

    detected_ram_top = g_ram_top_scan & ~(PMM_PAGE_SIZE - 1u);
    if (detected_ram_top == 0u) {
        return 0u;
    }

    total_page_count = detected_ram_top / PMM_PAGE_SIZE;
    bitmap_size = (total_page_count + 31u) / 32u;
    bitmap = (uint32_t *)(uintptr_t)kernel_end;

    for (i = 0; i < bitmap_size; ++i) {
        bitmap[i] = 0xFFFFFFFFu;
    }

    free_page_count = 0u;
    multiboot_scan_mmap(info, pmm_mark_available_callback);

    bitmap_bytes = bitmap_size * (uint32_t)sizeof(uint32_t);
    reserved_bitmap_end = kernel_end + bitmap_bytes;
    reserved_bitmap_end = (reserved_bitmap_end + PMM_PAGE_SIZE - 1u) & ~(PMM_PAGE_SIZE - 1u);

    pmm_mark_reserved(0x00000000u, 0x00100000u);
    pmm_mark_reserved(0x00100000u, reserved_bitmap_end);

    return detected_ram_top;
}

uint32_t pmm_alloc_page(void) {
    uint32_t i;
    uint32_t bit;

    for (i = 0u; i < bitmap_size; ++i) {
        if (bitmap[i] == 0xFFFFFFFFu) {
            continue;
        }

        for (bit = 0u; bit < 32u; ++bit) {
            uint32_t page_index = i * 32u + bit;
            if (page_index >= total_page_count) {
                return 0u;
            }

            if (!bitmap_test(page_index)) {
                bitmap_set(page_index);
                if (free_page_count > 0u) {
                    free_page_count--;
                }
                return page_index * PMM_PAGE_SIZE;
            }
        }
    }

    return 0u;
}

void pmm_free_page(uint32_t addr) {
    uint32_t page_index;

    if ((addr & (PMM_PAGE_SIZE - 1u)) != 0u) {
        return;
    }

    page_index = addr / PMM_PAGE_SIZE;
    if (!pmm_is_page_index_valid(page_index)) {
        return;
    }

    if (bitmap_test(page_index)) {
        bitmap_clear(page_index);
        free_page_count++;
    }
}

uint32_t pmm_alloc_contiguous(uint32_t count) {
    uint32_t i;
    uint32_t run_start = 0u;
    uint32_t run_length = 0u;

    if (count == 0u) {
        return 0u;
    }

    for (i = 0u; i < total_page_count; ++i) {
        if (bitmap_test(i)) {
            run_length = 0u;
            run_start = i + 1u;
        } else {
            run_length++;
            if (run_length == count) {
                uint32_t j;
                for (j = run_start; j < run_start + count; ++j) {
                    bitmap_set(j);
                    if (free_page_count > 0u) {
                        free_page_count--;
                    }
                }
                return run_start * PMM_PAGE_SIZE;
            }
        }
    }

    return 0u;
}

uint32_t pmm_total_pages(void) {
    return total_page_count;
}

uint32_t pmm_free_pages(void) {
    return free_page_count;
}

uint32_t pmm_bitmap_end(void) {
    return reserved_bitmap_end;
}

void pmm_dump_stats(void) {
    serial_puts("[pmm] total=");
    put_hex32(total_page_count, serial_puts, serial_putchar);
    serial_puts(", free=");
    put_hex32(free_page_count, serial_puts, serial_putchar);
    serial_puts("\n");
}
