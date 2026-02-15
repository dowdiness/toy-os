#ifndef KERNEL_PAGING_H
#define KERNEL_PAGING_H

#include <stdint.h>

#define PAGE_SIZE 4096u

#define PTE_PRESENT       (1u << 0)
#define PTE_WRITABLE      (1u << 1)
#define PTE_USER          (1u << 2)
#define PTE_WRITE_THROUGH (1u << 3)
#define PTE_CACHE_DISABLE (1u << 4)
#define PTE_ACCESSED      (1u << 5)
#define PTE_DIRTY         (1u << 6)
#define PTE_PAGE_SIZE     (1u << 7)

#define PTE_ADDR_MASK 0xFFFFF000u

#define RECURSIVE_PD_INDEX 1023u
#define RECURSIVE_PD_VADDR 0xFFFFF000u
#define RECURSIVE_PT_BASE  0xFFC00000u

void paging_init(uint32_t ram_top);
int paging_map_page(uint32_t vaddr, uint32_t paddr, uint32_t flags);
void paging_unmap_page(uint32_t vaddr);
void paging_flush_tlb(void);

#endif
