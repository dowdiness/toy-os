#include "kernel/paging.h"

#include <stdint.h>

#include "drivers/serial.h"
#include "kernel/fmt.h"
#include "kernel/pmm.h"

#define PAGING_MAX_IDENTITY_MAP (256u * 1024u * 1024u)

static uint32_t g_page_directory_phys;

static inline void write_u32(uint32_t addr, uint32_t value) {
    *(uint32_t *)(uintptr_t)addr = value;
}

static inline uint32_t read_u32(uint32_t addr) {
    return *(uint32_t *)(uintptr_t)addr;
}

static void zero_page(uint32_t page_phys) {
    uint32_t i;
    uint32_t *p = (uint32_t *)(uintptr_t)page_phys;
    for (i = 0u; i < (PAGE_SIZE / sizeof(uint32_t)); ++i) {
        p[i] = 0u;
    }
}

static inline void cr3_write(uint32_t pd_phys) {
    __asm__ volatile("mov %0, %%cr3" : : "r"(pd_phys) : "memory");
}

static inline uint32_t cr3_read(void) {
    uint32_t val;
    __asm__ volatile("mov %%cr3, %0" : "=r"(val));
    return val;
}

static inline void paging_enable(void) {
    uint32_t cr0;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= (1u << 31);
    __asm__ volatile("mov %0, %%cr0" : : "r"(cr0) : "memory");
}

static inline void invlpg(uint32_t vaddr) {
    __asm__ volatile("invlpg (%0)" : : "r"(vaddr) : "memory");
}

static uint32_t paging_identity_map_top(uint32_t ram_top) {
    if (ram_top > PAGING_MAX_IDENTITY_MAP) {
        return PAGING_MAX_IDENTITY_MAP;
    }
    return ram_top;
}

void paging_init(uint32_t ram_top) {
    uint32_t map_top;
    uint32_t pd_phys;
    uint32_t pd_index;

    map_top = paging_identity_map_top(ram_top);
    pd_phys = pmm_alloc_page();
    if (pd_phys == 0u) {
        serial_puts("[paging] ERROR: failed to allocate page directory\n");
        return;
    }

    zero_page(pd_phys);

    for (pd_index = 0u; pd_index * 0x400000u < map_top; ++pd_index) {
        uint32_t pt_phys = pmm_alloc_page();
        uint32_t pt_entry;
        uint32_t base_addr = pd_index * 0x400000u;

        if (pt_phys == 0u) {
            serial_puts("[paging] ERROR: failed to allocate page table\n");
            return;
        }

        zero_page(pt_phys);

        for (pt_entry = 0u; pt_entry < 1024u; ++pt_entry) {
            uint32_t phys = base_addr + pt_entry * PAGE_SIZE;
            if (phys >= map_top) {
                break;
            }
            write_u32(pt_phys + pt_entry * 4u, phys | PTE_PRESENT | PTE_WRITABLE);
        }

        write_u32(pd_phys + pd_index * 4u, pt_phys | PTE_PRESENT | PTE_WRITABLE);
    }

    write_u32(pd_phys + RECURSIVE_PD_INDEX * 4u, pd_phys | PTE_PRESENT | PTE_WRITABLE);

    g_page_directory_phys = pd_phys;
    cr3_write(pd_phys);
    paging_enable();

    serial_puts("[paging] enabled, identity-mapped ");
    put_hex32(map_top / (1024u * 1024u), serial_puts, serial_putchar);
    serial_puts(" MiB\n");
}

int paging_map_page(uint32_t vaddr, uint32_t paddr, uint32_t flags) {
    uint32_t pd_index = (vaddr >> 22) & 0x3FFu;
    uint32_t pt_index = (vaddr >> 12) & 0x3FFu;
    uint32_t *pd;
    uint32_t *pt;
    uint32_t pt_phys;

    pd = (uint32_t *)(uintptr_t)RECURSIVE_PD_VADDR;
    if ((pd[pd_index] & PTE_PRESENT) == 0u) {
        pt_phys = pmm_alloc_page();
        if (pt_phys == 0u) {
            return -1;
        }

        zero_page(pt_phys);
        pd[pd_index] = (pt_phys & PTE_ADDR_MASK) | PTE_PRESENT | PTE_WRITABLE;
        paging_flush_tlb();
    }

    pt = (uint32_t *)(uintptr_t)(RECURSIVE_PT_BASE + pd_index * PAGE_SIZE);
    pt[pt_index] = (paddr & PTE_ADDR_MASK) | (flags | PTE_PRESENT);
    invlpg(vaddr);
    return 0;
}

void paging_unmap_page(uint32_t vaddr) {
    uint32_t pd_index = (vaddr >> 22) & 0x3FFu;
    uint32_t pt_index = (vaddr >> 12) & 0x3FFu;
    uint32_t *pd;
    uint32_t *pt;

    pd = (uint32_t *)(uintptr_t)RECURSIVE_PD_VADDR;
    if ((pd[pd_index] & PTE_PRESENT) == 0u) {
        return;
    }

    pt = (uint32_t *)(uintptr_t)(RECURSIVE_PT_BASE + pd_index * PAGE_SIZE);
    pt[pt_index] = 0u;
    invlpg(vaddr);
}

void paging_flush_tlb(void) {
    cr3_write(cr3_read());
}
