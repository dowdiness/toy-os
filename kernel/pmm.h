#ifndef KERNEL_PMM_H
#define KERNEL_PMM_H

#include <stdint.h>

struct multiboot_info;

#define PMM_PAGE_SIZE 4096u

uint32_t pmm_init(uint32_t kernel_end, const struct multiboot_info *info);
uint32_t pmm_alloc_page(void);
void pmm_free_page(uint32_t addr);
uint32_t pmm_alloc_contiguous(uint32_t count);
uint32_t pmm_total_pages(void);
uint32_t pmm_free_pages(void);
void pmm_dump_stats(void);
uint32_t pmm_bitmap_end(void);

#endif
