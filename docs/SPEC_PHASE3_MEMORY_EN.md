# Phase 3: Memory Management — Implementation Specification (Rev.2)

> Begin after Phase 2 (interrupt foundations) is complete. Prerequisite for Phase 4 (scheduler) and Phase 5a (capability syscall).
> Goal: a coding agent can complete the implementation using this document alone.

## Document Role

- **Authoritative spec**: All tasks, code structure, and verification criteria for Phase 3
- **Prerequisite**: Phase 2 Step 10 complete (IDT/ISR/PIC/PIT/keyboard confirmed working)
- **Successors**: Phase 4 (process management), Phase 5a (capability syscall)

## Goals

Physical memory management + paging + a heap allocator where `free` actually works.
All of the following must hold when Phase 3 is complete:

1. Parse the Multiboot memory map and recognize available RAM regions
2. Manage physical page frames (4 KiB units) with a bitmap; support alloc/free
3. Identity-map the kernel address space using 2-level page tables, load CR3, and keep running
4. A free-list heap allocator correctly handles `malloc`/`free` (MoonBit Perceus RC compatible)
5. All Phase 2 functionality (timer, keyboard, serial output) continues to work

### Acceptance Tests (Minimum Pass Criteria)

Phase 3 completion is judged by the following serial log output. All must be satisfied:

1. `[mmap] entries: ...` is printed (if 0, an error reason must be printed)
2. `[pmm] total=..., free=...` is printed (neither value may be 0)
3. `[paging] enabled` is printed **and** `[pit] heartbeat` continues afterward
4. All `[heap-test]` items print `OK` (zero `FAIL` lines)
5. The MoonBit path (`moon-kernel.elf`) boots at least as well as Phase 2, with no crashes

---

## 0. Understanding the Current State (For Agents)

Read the following files first to understand the current structure.

| File | Why |
|------|-----|
| `linker.ld` | Kernel load address (1 MiB) and section layout |
| `arch/x86/multiboot_boot.s` | Multiboot header flags and argument passing to `kernel_main` (EAX=magic, EBX=info) |
| `kernel/main.c` | C kernel entry. Receives `multiboot_magic` and `multiboot_info_addr` but currently unused |
| `kernel/moon_entry.c` | MoonBit kernel entry. Same (`(void)multiboot_info_addr`) |
| `runtime/runtime_stubs.c` | Current bump allocator (`free` is a no-op). Replacement target for Phase 3 |
| `arch/x86/idt.c` | Existing code style reference (packed struct, static array, init function pattern) |
| `Makefile` | Build rule addition pattern (how to add to `KERNEL_OBJS`) |

### Important Existing Constraints

- **Multiboot header flags** are set to `(1<<0) | (1<<1)` (`multiboot_boot.s` line 4).
  Bit 0 requests page-aligned modules; bit 1 is a hint requesting the `mem_lower`/`mem_upper`
  fields. **Bit 1 does NOT guarantee the memory map (mmap) is provided.**
  Whether mmap is present must be checked at runtime via bit 6 (`MULTIBOOT_FLAG_MMAP`) of
  `multiboot_info->flags`. QEMU's `-kernel` boot normally provides mmap, but this is not
  guaranteed by spec.
- **Kernel is loaded at 1 MiB** (`linker.ld` line 4: `. = 1M`).
- **Static heap is 4 MiB in BSS** (`runtime_stubs.c` line 6: `#define HEAP_SIZE`).
  This static buffer becomes unnecessary after the Phase 3 heap allocator replaces it.
- **Stack is 16 KiB in BSS** (`multiboot_boot.s` line 16: `.skip 16384`).
- **GDT kernel code selector is `0x08`** (`idt.c` line 5).
  Currently the GDT is only the flat-model setup in boot.s. No explicit `gdt.c` exists.

---

## 1. Memory Layout (Design Decision)

Physical memory layout after Phase 3 completion:

```
0x00000000 ┌─────────────────────────┐
           │ Real-mode IVT / BDA     │
           │ Entire low memory       │  ← All reserved (Phase 3 does not use this for simplicity)
0x000A0000 ├─────────────────────────┤
           │ VGA / ROM / BIOS        │  Reserved
0x000B8000 │   VGA text buffer       │  Identity-mapped
0x00100000 ├─────────────────────────┤  ← Kernel load address (1 MiB)
           │ .text .rodata .data     │
           │ .bss (stack + old heap) │
 __kernel_end ├──────────────────────┤  ← Linker symbol
           │ Physical page bitmap    │  ← Placed immediately after __kernel_end
           ├─────────────────────────┤
 bitmap_end│ Page directory (4K)     │
           │ Page tables             │
           ├─────────────────────────┤
           │ Heap region (contiguous)│  ← Allocated via pmm_alloc_contiguous
           ├─────────────────────────┤
           │ Free physical memory    │  ← Managed by bitmap
           │         ...             │
 ram_top    └─────────────────────────┘
```

**Reservation Rules (Phase 3):**
- `0x00000000` – `0x000FFFFF` (low memory, 1 MiB): All reserved. Includes IVT, BDA, VGA, BIOS ROM. Phase 3 never allocates pages from this region.
- `0x00100000` – `bitmap_end` (kernel body + bitmap): Reserved.
- Above `bitmap_end`, available regions: Managed by PMM.

### Virtual Address Space (Phase 3)

Phase 3 runs only the kernel (user space comes in Phase 5).
**All physical memory is identity-mapped (virt == phys).**

```
0x00000000 ~ ram_top    : Identity mapping (kernel space)
0xFFC00000 ~ 0xFFFFFFFF : Recursive mapping (page table self-reference)
```

Identity mapping upper bound: `min(ram_top, 256 MiB)` is recommended. QEMU defaults to
128 MiB RAM, so this is normally fine, but limits page table consumption under large RAM configs.
The actual mapping limit must be printed to serial.

Recursive mapping: The last page directory entry (index 1023) points to the page directory itself.
This allows reading/writing page table contents at `0xFFC00000 + table_index * 4096`.

---

## 2. linker.ld Changes

Export a `__kernel_end` symbol so the physical memory manager can determine the address
immediately after the kernel.

```ld
/* linker.ld — Phase 3 diff */
ENTRY(_start)

SECTIONS {
    . = 1M;

    .text BLOCK(4K) : ALIGN(4K) {
        *(.multiboot)
        *(.text)
    }

    .rodata BLOCK(4K) : ALIGN(4K) {
        *(.rodata)
    }

    .data BLOCK(4K) : ALIGN(4K) {
        *(.data)
    }

    .bss BLOCK(4K) : ALIGN(4K) {
        *(COMMON)
        *(.bss)
    }

    /* Phase 3: kernel end symbol (4K aligned) */
    . = ALIGN(4K);
    __kernel_end = .;
}
```

**Change**: Append `. = ALIGN(4K); __kernel_end = .;` at the end. No changes to existing section layout.

---

## 3. Multiboot Memory Map Parsing

### 3.1 Multiboot Info Structure Definitions

```c
/* kernel/multiboot.h */
#ifndef KERNEL_MULTIBOOT_H
#define KERNEL_MULTIBOOT_H

#include <stdint.h>

#define MULTIBOOT_MAGIC 0x2BADB002u

/* multiboot_info.flags bits */
#define MULTIBOOT_FLAG_MEM      (1u << 0)  /* mem_lower / mem_upper valid */
#define MULTIBOOT_FLAG_MMAP     (1u << 6)  /* mmap_length / mmap_addr valid */

struct multiboot_info {
    uint32_t flags;
    uint32_t mem_lower;        /* In KB, valid when flags bit 0 set */
    uint32_t mem_upper;        /* In KB, valid when flags bit 0 set */
    uint32_t boot_device;
    uint32_t cmdline;
    uint32_t mods_count;
    uint32_t mods_addr;
    uint32_t syms[4];
    uint32_t mmap_length;      /* Total bytes of memory map */
    uint32_t mmap_addr;        /* Physical address of memory map array */
    /* Remaining fields unused in Phase 3 */
} __attribute__((packed));

/* Each memory map entry */
struct multiboot_mmap_entry {
    uint32_t size;             /* Size of this struct minus 4 (excludes the size field itself) */
    uint64_t addr;             /* Start physical address of region */
    uint64_t len;              /* Byte length of region */
    uint32_t type;             /* 1 = available RAM, anything else = reserved */
} __attribute__((packed));

#define MULTIBOOT_MMAP_TYPE_AVAILABLE 1u

#endif
```

### 3.2 Memory Map Scan Function

```c
/* kernel/multiboot.c */
#include "kernel/multiboot.h"
#include "drivers/serial.h"
#include "kernel/fmt.h"

/*
 * Scan the memory map and invoke a callback for each entry.
 * Callback: fn(base_addr, length, is_available)
 * Returns: number of entries scanned. 0 if MMAP bit is not set in flags.
 */
uint32_t multiboot_scan_mmap(
    const struct multiboot_info *info,
    void (*callback)(uint32_t base, uint32_t length, int available))
{
    uint32_t offset;
    uint32_t count;
    const struct multiboot_mmap_entry *entry;

    if ((info->flags & MULTIBOOT_FLAG_MMAP) == 0) {
        serial_puts("[mmap] ERROR: no memory map from bootloader\n");
        serial_puts("[mmap] info->flags = ");
        put_hex32(info->flags, serial_puts, serial_putchar);
        serial_puts("\n");
        return 0;
    }

    offset = 0;
    count = 0;

    while (offset < info->mmap_length) {
        /* Defensive check: cannot even read the size field */
        if (offset + 4u > info->mmap_length) {
            break;
        }

        entry = (const struct multiboot_mmap_entry *)(uintptr_t)(info->mmap_addr + offset);

        /* Defensive check: size of 0 indicates corrupt data */
        if (entry->size == 0) {
            serial_puts("[mmap] WARN: entry with size=0, stopping scan\n");
            break;
        }

        /* Defensive check: entire entry must fit within mmap_length */
        if (offset + entry->size + 4u > info->mmap_length) {
            serial_puts("[mmap] WARN: entry overflows mmap_length, stopping scan\n");
            break;
        }

        /*
         * 32-bit OS: ignore regions above 4 GiB.
         * Skip if upper 32 bits of addr are non-zero, or addr+len exceeds 0xFFFFFFFF.
         */
        if (entry->addr <= 0xFFFFFFFFull &&
            entry->addr + entry->len <= 0x100000000ull)
        {
            uint32_t base = (uint32_t)entry->addr;
            uint32_t length = (uint32_t)entry->len;
            int available = (entry->type == MULTIBOOT_MMAP_TYPE_AVAILABLE) ? 1 : 0;
            callback(base, length, available);
        }

        /* Next entry: size field (4 bytes) + value of size */
        offset += entry->size + 4u;
        count++;
    }

    return count;
}
```

### 3.3 Debug Output

```c
/* Add to kernel/multiboot.c */
static void mmap_debug_print(uint32_t base, uint32_t length, int available) {
    serial_puts("  ");
    put_hex32(base, serial_puts, serial_putchar);
    serial_puts(" - ");
    put_hex32(base + length, serial_puts, serial_putchar);
    serial_puts(available ? " [available]" : " [reserved]");
    serial_puts("\n");
}

void multiboot_dump_mmap(const struct multiboot_info *info) {
    serial_puts("[mmap] Memory map:\n");
    uint32_t count = multiboot_scan_mmap(info, mmap_debug_print);
    serial_puts("[mmap] entries: ");
    put_hex32(count, serial_puts, serial_putchar);
    serial_puts("\n");
}
```

---

## 4. Physical Page Frame Allocator (Bitmap)

### 4.1 Design

- Page size: 4 KiB (4096 bytes)
- Granularity: 1 bit = 1 page (0 = free, 1 = in use)
- Bitmap placed immediately after `__kernel_end`
- Max supported RAM: 4 GiB (bitmap size = 128 KiB = 1,048,576 pages / 8)
- Actual bitmap size determined by detected RAM

### 4.2 Data Structures and Interface

```c
/* kernel/pmm.h */
#ifndef KERNEL_PMM_H
#define KERNEL_PMM_H

#include <stdint.h>

struct multiboot_info;  /* forward declaration */

#define PMM_PAGE_SIZE 4096u

/*
 * Initialize. kernel_end is the value of linker symbol __kernel_end.
 * Reads memory map from multiboot_info and places the bitmap.
 * Returns: top address of detected RAM (ram_top). 0 on failure.
 */
uint32_t pmm_init(uint32_t kernel_end, const struct multiboot_info *info);

/*
 * Allocate one page (4 KiB) and return its physical address. Returns 0 on failure.
 * Returned address is PMM_PAGE_SIZE-aligned.
 */
uint32_t pmm_alloc_page(void);

/* Free the page at physical address addr. addr must have been obtained from pmm_alloc_page. */
void pmm_free_page(uint32_t addr);

/*
 * Allocate count physically contiguous pages.
 * Returned address is PMM_PAGE_SIZE-aligned. Returns 0 on failure.
 * Used for initial heap region allocation.
 */
uint32_t pmm_alloc_contiguous(uint32_t count);

/* Statistics getters */
uint32_t pmm_total_pages(void);
uint32_t pmm_free_pages(void);

/* Debug: print stats to serial */
void pmm_dump_stats(void);

#endif
```

**API Design Rationale:**
- `pmm_init` returns `ram_top`. `paging_init` needs this as the identity mapping upper bound.
- Statistics (total/free) are accessed via separate getter functions. Init success/failure is determined by `ram_top == 0`.
- `pmm_alloc_contiguous` is essential to satisfy the heap's requirement for contiguous physical memory.

### 4.3 Implementation Essentials

```c
/* kernel/pmm.c */
#include "kernel/pmm.h"
#include "kernel/multiboot.h"
#include "drivers/serial.h"
#include "kernel/fmt.h"

static uint32_t *bitmap;          /* Base address of bitmap array */
static uint32_t bitmap_size;      /* Number of uint32_t elements in bitmap */
static uint32_t total_page_count; /* Total pages under management */
static uint32_t free_page_count;  /* Current free page count */

/* Convert physical address to bitmap index */
static inline uint32_t page_to_index(uint32_t addr) {
    return addr / PMM_PAGE_SIZE;
}

/* Set bit (mark as in-use) */
static inline void bitmap_set(uint32_t page_index) {
    bitmap[page_index / 32u] |= (1u << (page_index % 32u));
}

/* Clear bit (mark as free) */
static inline void bitmap_clear(uint32_t page_index) {
    bitmap[page_index / 32u] &= ~(1u << (page_index % 32u));
}

/* Test bit (0 = free, non-zero = in-use) */
static inline int bitmap_test(uint32_t page_index) {
    return (bitmap[page_index / 32u] >> (page_index % 32u)) & 1u;
}

/*
 * Mark pages in the specified range as in-use.
 * start_addr and end_addr need not be PMM_PAGE_SIZE-aligned (handled via rounding).
 */
static void pmm_mark_reserved(uint32_t start_addr, uint32_t end_addr) {
    uint32_t start_page = start_addr / PMM_PAGE_SIZE;
    uint32_t end_page = (end_addr + PMM_PAGE_SIZE - 1u) / PMM_PAGE_SIZE;
    uint32_t i;

    if (end_page > total_page_count) end_page = total_page_count;
    for (i = start_page; i < end_page; ++i) {
        if (!bitmap_test(i)) {
            bitmap_set(i);
            if (free_page_count > 0) free_page_count--;
        }
    }
}

/*
 * Mark pages in the specified range as free.
 */
static void pmm_mark_free(uint32_t start_addr, uint32_t end_addr) {
    uint32_t start_page = start_addr / PMM_PAGE_SIZE;
    uint32_t end_page = (end_addr + PMM_PAGE_SIZE - 1u) / PMM_PAGE_SIZE;
    uint32_t i;

    if (end_page > total_page_count) end_page = total_page_count;
    for (i = start_page; i < end_page; ++i) {
        if (bitmap_test(i)) {
            bitmap_clear(i);
            free_page_count++;
        }
    }
}
```

### 4.4 `pmm_init` Procedure

1. Scan the Multiboot memory map to determine `ram_top` (highest address of available RAM)
2. Compute `total_page_count = ram_top / PMM_PAGE_SIZE`
3. Compute `bitmap_size = (total_page_count + 31) / 32`
4. Place `bitmap = (uint32_t *)kernel_end`
5. Fill entire bitmap with `0xFF` (**mark all pages as "in-use"**)
6. Mark only the `available` regions from the memory map as free using `pmm_mark_free`
7. Re-mark the following regions as "in-use" via `pmm_mark_reserved` (protect reserved regions):
   - `0x00000000` – `0x000FFFFF` (entire low memory, 1 MiB. Includes IVT, BDA, VGA, BIOS ROM)
   - `0x00100000` – `bitmap_end` (kernel body + the bitmap itself)
8. Tally `free_page_count`
9. Return `ram_top`

Computing `bitmap_end`:
```c
uint32_t bitmap_bytes = bitmap_size * sizeof(uint32_t);
uint32_t bitmap_end = kernel_end + bitmap_bytes;
/* 4K align */
bitmap_end = (bitmap_end + PMM_PAGE_SIZE - 1u) & ~(PMM_PAGE_SIZE - 1u);
```

### 4.5 `pmm_alloc_page` Approach

Linear scan (first-fit). Scan the bitmap array from the start and return the address of the first
free bit found.

Optimization: skip 32 pages at once when `bitmap[i] == 0xFFFFFFFF`.

```c
uint32_t pmm_alloc_page(void) {
    uint32_t i, bit;

    for (i = 0; i < bitmap_size; ++i) {
        if (bitmap[i] == 0xFFFFFFFFu) continue;  /* All bits in-use */
        for (bit = 0; bit < 32u; ++bit) {
            uint32_t page_index = i * 32u + bit;
            if (page_index >= total_page_count) return 0;
            if (!bitmap_test(page_index)) {
                bitmap_set(page_index);
                free_page_count--;
                return page_index * PMM_PAGE_SIZE;
            }
        }
    }
    return 0;  /* Out of memory */
}
```

### 4.6 `pmm_alloc_contiguous` Approach

Scan the bitmap for `count` consecutive free bits.

```c
uint32_t pmm_alloc_contiguous(uint32_t count) {
    uint32_t run_start = 0;
    uint32_t run_length = 0;
    uint32_t i;

    for (i = 0; i < total_page_count; ++i) {
        if (bitmap_test(i)) {
            run_length = 0;
            run_start = i + 1;
        } else {
            run_length++;
            if (run_length == count) {
                /* Allocate the contiguous region */
                uint32_t j;
                for (j = run_start; j < run_start + count; ++j) {
                    bitmap_set(j);
                    free_page_count--;
                }
                return run_start * PMM_PAGE_SIZE;
            }
        }
    }
    return 0;  /* No contiguous region found */
}
```

### 4.7 Statistics and Debug

```c
uint32_t pmm_total_pages(void) { return total_page_count; }
uint32_t pmm_free_pages(void)  { return free_page_count; }

void pmm_dump_stats(void) {
    serial_puts("[pmm] total=");
    put_hex32(total_page_count, serial_puts, serial_putchar);
    serial_puts(", free=");
    put_hex32(free_page_count, serial_puts, serial_putchar);
    serial_puts("\n");
}
```

---

## 5. Paging (2-Level Page Tables)

### 5.1 x86 Paging Overview (Knowledge Required for Implementation)

- **Page Directory (PD)**: 1024 entries × 4 bytes = 4 KiB. Each entry points to a page table.
- **Page Table (PT)**: 1024 entries × 4 bytes = 4 KiB. Each entry points to a physical page.
- **Virtual address decomposition**: `[PD index: 10 bits][PT index: 10 bits][offset: 12 bits]`
- **CR3 register**: Holds the physical address of the page directory

### 5.2 Entry Flags

```c
/* kernel/paging.h */
#ifndef KERNEL_PAGING_H
#define KERNEL_PAGING_H

#include <stdint.h>

#define PAGE_SIZE         4096u

/* Page directory / page table entry flags */
#define PTE_PRESENT       (1u << 0)   /* Page is present */
#define PTE_WRITABLE      (1u << 1)   /* Writable */
#define PTE_USER          (1u << 2)   /* Accessible from Ring 3 (for Phase 5) */
#define PTE_WRITE_THROUGH (1u << 3)
#define PTE_CACHE_DISABLE (1u << 4)
#define PTE_ACCESSED      (1u << 5)
#define PTE_DIRTY         (1u << 6)   /* PT entries only */
#define PTE_PAGE_SIZE     (1u << 7)   /* PD entry: 4 MiB page (not used) */

/* Address mask (upper 20 bits) */
#define PTE_ADDR_MASK     0xFFFFF000u

/* Recursive mapping index */
#define RECURSIVE_PD_INDEX 1023u

/* Addresses for accessing page tables via recursive mapping */
/* PD itself: 0xFFFFF000 */
/* PT[i]:     0xFFC00000 + i * 4096 */
#define RECURSIVE_PD_VADDR    0xFFFFF000u
#define RECURSIVE_PT_BASE     0xFFC00000u

/*
 * Initialize: build identity mapping and load CR3.
 * ram_top: upper bound for identity mapping (return value of pmm_init).
 * Actual mapping limit may be min(ram_top, 256 MiB).
 */
void paging_init(uint32_t ram_top);

/*
 * Map virtual address vaddr to physical address paddr.
 * flags: PTE_PRESENT | PTE_WRITABLE etc.
 * Allocates page tables via pmm_alloc_page as needed.
 * Returns: 0 = success, -1 = failure (out of memory).
 */
int paging_map_page(uint32_t vaddr, uint32_t paddr, uint32_t flags);

/*
 * Unmap the virtual address vaddr.
 * Performs TLB flush (invlpg).
 */
void paging_unmap_page(uint32_t vaddr);

/*
 * Reload CR3 to flush the entire TLB.
 */
void paging_flush_tlb(void);

#endif
```

### 5.3 Assembly Helpers

```c
/* Static inline functions inside kernel/paging.c */

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
    cr0 |= (1u << 31);  /* PG bit */
    __asm__ volatile("mov %0, %%cr0" : : "r"(cr0) : "memory");
}

static inline void invlpg(uint32_t vaddr) {
    __asm__ volatile("invlpg (%0)" : : "r"(vaddr) : "memory");
}
```

### 5.4 `paging_init` Procedure

1. Allocate one page for the page directory (4 KiB) via `pmm_alloc_page()` and zero-fill it
2. Determine identity mapping upper bound: `uint32_t map_top = ram_top;` (may cap if too large)
3. Build the identity mapping:
   - For each 4 MiB region from `0x00000000` to `map_top`, allocate a page table via `pmm_alloc_page()`
   - Set each page table entry to `phys_addr | PTE_PRESENT | PTE_WRITABLE`
   - Set the corresponding page directory entry to `pt_phys | PTE_PRESENT | PTE_WRITABLE`
   - The VGA buffer (`0xB8000`) region is identity-mapped automatically (it falls within the low memory range)
4. Set up recursive mapping:
   ```c
   page_directory[RECURSIVE_PD_INDEX] = pd_phys | PTE_PRESENT | PTE_WRITABLE;
   ```
5. Load CR3: `cr3_write(pd_phys)`
6. Enable paging: `paging_enable()` sets CR0.PG
7. Print `[paging] enabled, identity-mapped N MiB` to serial

**⚠️ Warning: The instant CR0.PG is set**, if the identity mapping is incorrect, a triple fault
occurs and QEMU resets. Debugging procedure:

1. First, only set CR3 without enabling PG and verify the build/boot still works
2. Then enable PG and check whether heartbeat output appears
3. If no heartbeat, dump the page tables (print PD/PT contents to serial before loading CR3) and investigate

### 5.5 Page Fault Handler

Connected to the Phase 2 interrupt dispatcher. CPU exception vector 14 is the page fault.
In Phase 3, print information and halt (graceful handling added in Phase 5 with user space).

**Change**: In the exception handler (the code path that panics for vectors < 32), add
CR2 (faulting virtual address) output specifically for vector 14.
The agent should read the interrupt dispatch source to determine exactly which file and line.

```c
/* Inside the exception panic handler, add for vector == 14: */
static inline uint32_t cr2_read(void) {
    uint32_t val;
    __asm__ volatile("mov %%cr2, %0" : "=r"(val));
    return val;
}

/* Add to panic output: */
if (frame->vector == 14) {
    serial_puts(" cr2=");
    put_hex32(cr2_read(), serial_puts, serial_putchar);
}
```

---

## 6. Heap Allocator (Free-List)

### 6.1 Design Approach

- **Replace** `malloc`/`free` in `runtime/runtime_stubs.c` with a free-list allocator
- MoonBit's Perceus RC calls `free` frequently, so a working `free` is mandatory
- The free list is a singly-linked list. Block header contains "size" and "next free block".
- On free: **forward coalescing** (merge with the next adjacent block)
- **Backward merge (merging with the preceding block) is NOT done in Phase 3.**
  Backward merge on a singly-linked list is O(n) and not worth the implementation complexity.
  However, Perceus RC's frequent frees may cause fragmentation to accumulate early.
  If `heap_malloc` starts returning NULL, suspect fragmentation first.
  Upgrade to a doubly-linked list or segregated free list in Phase 4+.
- Heap region is initially allocated after paging is enabled, using `pmm_alloc_contiguous`

### 6.2 Block Header

```c
/* runtime/heap.h */
#ifndef RUNTIME_HEAP_H
#define RUNTIME_HEAP_H

#include <stddef.h>
#include <stdint.h>

struct heap_block {
    uint32_t       size;    /* Payload bytes (excludes header) */
    uint32_t       is_free; /* 0 = in-use, 1 = free */
    struct heap_block *next;  /* Next block (address order) */
};

#define HEAP_BLOCK_HEADER_SIZE  sizeof(struct heap_block)
#define HEAP_MIN_ALLOC          8u  /* Minimum allocation size */
#define HEAP_ALIGN              8u  /* Alignment */

/*
 * Initialize the heap. base_addr and size_bytes define the initial heap region.
 * base_addr must be 8-byte aligned.
 * size_bytes must be >= HEAP_BLOCK_HEADER_SIZE + HEAP_MIN_ALLOC.
 */
void heap_init(void *base_addr, uint32_t size_bytes);

void *heap_malloc(size_t size);
void  heap_free(void *ptr);
void *heap_calloc(size_t count, size_t size);
void *heap_realloc(void *ptr, size_t new_size);

/* Debug: print free-list state to serial */
void heap_dump(void);

#endif
```

### 6.3 `heap_malloc` Procedure

1. Round `size` up to a multiple of `HEAP_ALIGN`. If `size < HEAP_MIN_ALLOC`, use `HEAP_MIN_ALLOC`
2. Walk the free list from the head (first-fit)
3. Find a block where `block->is_free == 1 && block->size >= size`
4. If the block is large enough (`block->size >= size + HEAP_BLOCK_HEADER_SIZE + HEAP_MIN_ALLOC`), split:
   - Shrink current block's size to `size`
   - Write a new block header in the remainder with `is_free = 1` and insert it into the list
5. Mark the block as `is_free = 0` and return `(block + 1)` as the user pointer
6. If no block found, return `NULL`

### 6.4 `heap_free` Procedure

1. If `ptr` is NULL, do nothing
2. Subtract `HEAP_BLOCK_HEADER_SIZE` from `ptr` to obtain the block header
3. Set `is_free = 1`
4. **Forward coalescing**: If the next block (`block->next`) exists, is physically adjacent
   (`(uint8_t *)block + HEAP_BLOCK_HEADER_SIZE + block->size == (uint8_t *)block->next`),
   and `block->next->is_free == 1`:
   - `block->size += HEAP_BLOCK_HEADER_SIZE + block->next->size`
   - `block->next = block->next->next`

### 6.5 `heap_realloc` Procedure

1. If `ptr == NULL`, return `heap_malloc(new_size)`
2. If `new_size == 0`, call `heap_free(ptr)` and return NULL
3. Retrieve `old_size` from the block header
4. If `new_size <= old_size` (shrink): return `ptr` as-is (splitting the remainder is optional)
5. If `new_size > old_size` (grow): `heap_malloc(new_size)` → copy data → `heap_free(ptr)`

### 6.6 Changes to `runtime_stubs.c`

Replace the existing `malloc`/`free`/`calloc`/`realloc` with delegation to `heap_*` functions.

**Before**: Bump allocator implementation (`heap_storage`, `heap_offset`, etc.)
**After**:

```c
/* runtime/runtime_stubs.c — Phase 3 diff */

#include "runtime/heap.h"

/* ===== Delete all old bump allocator code =====
 * (heap_storage, heap_offset, heap_begin, heap_end, alloc_header,
 *  old malloc, free, calloc, realloc implementations)
 */

void *malloc(size_t size)                    { return heap_malloc(size); }
void  free(void *ptr)                        { heap_free(ptr); }
void *calloc(size_t count, size_t size)      { return heap_calloc(count, size); }
void *realloc(void *ptr, size_t new_size)    { return heap_realloc(ptr, new_size); }
```

Remaining functions (`memset`, `memcpy`, `memmove`, `memcmp`, `strlen`, `strcmp`,
`strncmp`, `putchar`, `write`, `abort`, `exit`) are unchanged.

### 6.7 Heap Region Allocation

The initial heap region is allocated as contiguous physical pages via `pmm_alloc_contiguous`.

```c
/* Inside kernel/main.c initialization sequence */
#define INITIAL_HEAP_PAGES  256u  /* 256 * 4K = 1 MiB */

uint32_t heap_base = pmm_alloc_contiguous(INITIAL_HEAP_PAGES);
if (heap_base == 0) {
    serial_puts("[heap] ERROR: cannot allocate contiguous heap pages\n");
    /* No fallback — halt */
    return;
}
/* Identity mapping: physical address == virtual address */
heap_init((void *)(uintptr_t)heap_base, INITIAL_HEAP_PAGES * PMM_PAGE_SIZE);
serial_puts("[heap] initialized (1 MiB at ");
put_hex32(heap_base, serial_puts, serial_putchar);
serial_puts(")\n");
```

Future heap expansion: Add a `heap_expand` function that allocates additional pages via
`pmm_alloc_page` and appends them to the end of the free list. To be implemented in Phase 4+
as needed.

---

## 7. Integration of the Initialization Sequence

### 7.1 Changes to kernel/main.c

```c
/* kernel/main.c — Phase 3 initialization flow */

#include "kernel/multiboot.h"
#include "kernel/pmm.h"
#include "kernel/paging.h"
#include "runtime/heap.h"

extern uint32_t __kernel_end;  /* Linker symbol */

#define INITIAL_HEAP_PAGES 256u

void kernel_main(uint32_t multiboot_magic, uint32_t multiboot_info_addr) {
    const struct multiboot_info *mbi =
        (const struct multiboot_info *)(uintptr_t)multiboot_info_addr;

    /* ===== Same as Phase 2 (no changes) ===== */
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

    /* ===== Phase 3 additions ===== */

    /* Multiboot magic validation */
    if (multiboot_magic != MULTIBOOT_MAGIC) {
        serial_puts("[boot] ERROR: invalid multiboot magic\n");
        vga_puts("ERROR: Invalid multiboot magic.\n");
        return;
    }

    /* Step 1: Memory map parsing */
    multiboot_dump_mmap(mbi);

    /* Step 2: Physical page allocator init */
    uint32_t kernel_end = (uint32_t)(uintptr_t)&__kernel_end;
    uint32_t ram_top = pmm_init(kernel_end, mbi);
    if (ram_top == 0) {
        serial_puts("[pmm] ERROR: init failed\n");
        return;
    }
    pmm_dump_stats();

    /* Step 3: Enable paging */
    paging_init(ram_top);

    /* Step 4: Heap allocator init */
    uint32_t heap_base = pmm_alloc_contiguous(INITIAL_HEAP_PAGES);
    if (heap_base == 0) {
        serial_puts("[heap] ERROR: cannot allocate contiguous heap pages\n");
        return;
    }
    heap_init((void *)(uintptr_t)heap_base, INITIAL_HEAP_PAGES * PMM_PAGE_SIZE);
    serial_puts("[heap] initialized (1 MiB at ");
    put_hex32(heap_base, serial_puts, serial_putchar);
    serial_puts(")\n");

    /* Step 5 (optional): Heap self-test */
#if defined(PHASE3_HEAP_TEST)
    heap_selftest();
#endif

    /* ===== Same as Phase 2 from here ===== */
    vga_clear();
    vga_puts("Kernel running with paging + heap.\n");

    enable_interrupts();
    cpu_idle_forever();
}
```

### 7.2 Changes to kernel/moon_entry.c

Add the same initialization sequence to `moon_entry.c`.
`multiboot_info_addr` is currently cast to `(void)`, so remove that cast and use the value.
The initialization flow is identical to `kernel/main.c`. Complete
pmm_init → paging_init → heap_init before calling MoonBit's `main()`.

---

## 8. File Structure and Makefile Changes

### 8.1 New Files

| File | Contents |
|------|----------|
| `kernel/multiboot.h` | Multiboot structure definitions |
| `kernel/multiboot.c` | Memory map scanning + debug output |
| `kernel/pmm.h` | Physical page allocator interface |
| `kernel/pmm.c` | Bitmap physical page allocator |
| `kernel/paging.h` | Paging interface |
| `kernel/paging.c` | 2-level page tables + CR3 operations |
| `runtime/heap.h` | Free-list heap allocator interface |
| `runtime/heap.c` | Free-list heap allocator implementation |

### 8.2 Makefile Additions

Modify the existing Makefile as follows. Changes are marked explicitly.

**C kernel path: `KERNEL_OBJS` diff** (replace around line 31)

```makefile
KERNEL_OBJS  = arch/x86/multiboot_boot.o arch/x86/isr_stubs.o arch/x86/isr_dispatch.o arch/x86/idt.o \
               arch/x86/pic.o arch/x86/pit.o arch/x86/keyboard.o \
               drivers/vga.o drivers/serial.o kernel/fmt.o \
               kernel/multiboot.o kernel/pmm.o kernel/paging.o \
               runtime/heap.o \
               kernel/main.o
```

**MoonBit kernel path: `MOON_KERNEL_OBJS` diff** (replace around line 52)

```makefile
MOON_KERNEL_OBJS = arch/x86/multiboot_boot.o arch/x86/isr_stubs.o arch/x86/isr_dispatch.o arch/x86/idt.o \
                   arch/x86/pic.o arch/x86/pit.o arch/x86/keyboard.o \
                   drivers/vga.o drivers/serial.o kernel/fmt.o \
                   kernel/multiboot.o kernel/pmm.o kernel/paging.o \
                   runtime/heap.o \
                   runtime/runtime_stubs.o runtime/moon_kernel_ffi.o runtime/moon_runtime.o \
                   kernel/moon_entry.o $(MOON_GEN_O)
```

**New `.o` build rules** (add in the Phase 0 targets section, before the `kernel/main.o` rule)

```makefile
kernel/multiboot.o: kernel/multiboot.c kernel/multiboot.h
	$(KCC) $(KCFLAGS) -c $< -o $@

kernel/pmm.o: kernel/pmm.c kernel/pmm.h kernel/multiboot.h
	$(KCC) $(KCFLAGS) -c $< -o $@

kernel/paging.o: kernel/paging.c kernel/paging.h kernel/pmm.h
	$(KCC) $(KCFLAGS) -c $< -o $@

runtime/heap.o: runtime/heap.c runtime/heap.h
	$(KCC) $(KCFLAGS) -c $< -o $@
```

**clean target**: The existing `$(KERNEL_OBJS)` and `$(MOON_KERNEL_OBJS)` variable references
handle cleanup automatically. No changes needed.

**Note**: `kernel/multiboot.o` etc. are used in both the C kernel path and MoonBit kernel path.
The only difference between `KCFLAGS` and `MOON_KCFLAGS` is `-DMOONBIT_NATIVE_NO_SYS_HEADER`
and `-I$(MOON_INCLUDE_DIR)`, which `multiboot.c`, `pmm.c`, `paging.c`, and `heap.c` do not
depend on. Therefore the same `.o` files can be shared between both paths.

---

## 9. Verification Matrix (Phase 3 Completion Criteria)

Following the Phase 2 Step 9 pattern.

### 9.1 Build and Regression Checks

```bash
moon check --target native
make kernel.elf && make check-kernel
make moon-kernel.elf && make check-moon-kernel
make boot_512.img  # Legacy path regression
```

All must succeed.

### 9.2 Memory Map Output Check

```bash
timeout 6s make run-kernel-serial 2>&1 | grep '\[mmap\]'
```

Expected output (addresses for QEMU default 128 MiB RAM):

```
[mmap] Memory map:
  0x00000000 - 0x0009FC00 [available]
  0x0009FC00 - 0x000A0000 [reserved]
  0x000F0000 - 0x00100000 [reserved]
  0x00100000 - 0x07FE0000 [available]
  ...
[mmap] entries: 0x00000006
```

At least one `[available]` entry must exist, with at least 1 MiB of RAM detected.

### 9.3 Physical Page Allocator Check

```bash
timeout 6s make run-kernel-serial 2>&1 | grep '\[pmm\]'
```

Expected output:

```
[pmm] total=0x0000XXXX, free=0x0000XXXX
```

Neither `total` nor `free` may be 0.

### 9.4 Paging Enable Check

```bash
timeout 6s make run-kernel-serial 2>&1 | grep -E '\[paging\]|\[pit\]'
```

Expected output:

```
[paging] enabled, identity-mapped XX MiB
[pit] heartbeat
[pit] heartbeat
...
```

The kernel must not crash after paging is enabled (timer heartbeat must continue).

### 9.5 Heap malloc/free Check

Add the following self-test to `kernel/main.c` (guarded by `#ifdef PHASE3_HEAP_TEST`):

```c
#if defined(PHASE3_HEAP_TEST)
static void heap_selftest(void) {
    serial_puts("[heap-test] start\n");

    /* Test 1: malloc + free + re-malloc */
    void *p1 = malloc(128);
    if (!p1) { serial_puts("[heap-test] FAIL: malloc(128) returned NULL\n"); return; }
    serial_puts("[heap-test] malloc(128) OK: ");
    put_hex32((uint32_t)(uintptr_t)p1, serial_puts, serial_putchar);
    serial_puts("\n");

    free(p1);
    serial_puts("[heap-test] free OK\n");

    void *p2 = malloc(128);
    if (!p2) { serial_puts("[heap-test] FAIL: re-malloc returned NULL\n"); return; }
    serial_puts("[heap-test] re-malloc(128) OK: ");
    put_hex32((uint32_t)(uintptr_t)p2, serial_puts, serial_putchar);
    serial_puts("\n");

    /* Test 2: freed region is reused (p2 <= p1 address implies reuse) */
    if ((uint32_t)(uintptr_t)p2 <= (uint32_t)(uintptr_t)p1) {
        serial_puts("[heap-test] free-list reuse: OK\n");
    } else {
        serial_puts("[heap-test] WARN: no reuse (may be OK if layout differs)\n");
    }

    /* Test 3: calloc zero-initialization */
    uint8_t *p3 = (uint8_t *)calloc(64, 1);
    if (!p3) { serial_puts("[heap-test] FAIL: calloc returned NULL\n"); return; }
    int all_zero = 1;
    for (int i = 0; i < 64; ++i) {
        if (p3[i] != 0) { all_zero = 0; break; }
    }
    serial_puts(all_zero ? "[heap-test] calloc zero-init: OK\n"
                         : "[heap-test] FAIL: calloc not zeroed\n");
    free(p3);

    /* Test 4: realloc data preservation */
    uint8_t *p4 = (uint8_t *)malloc(32);
    if (!p4) { serial_puts("[heap-test] FAIL: malloc(32) returned NULL\n"); return; }
    for (int i = 0; i < 32; ++i) p4[i] = (uint8_t)i;
    uint8_t *p5 = (uint8_t *)realloc(p4, 64);
    if (!p5) { serial_puts("[heap-test] FAIL: realloc returned NULL\n"); return; }
    int data_ok = 1;
    for (int i = 0; i < 32; ++i) {
        if (p5[i] != (uint8_t)i) { data_ok = 0; break; }
    }
    serial_puts(data_ok ? "[heap-test] realloc data preserve: OK\n"
                        : "[heap-test] FAIL: realloc data lost\n");
    free(p5);

    /* Test 5: bulk alloc/free (Perceus RC pattern simulation) */
    serial_puts("[heap-test] stress: 1000x malloc(64)+free ... ");
    int stress_ok = 1;
    for (int i = 0; i < 1000; ++i) {
        void *p = malloc(64);
        if (!p) { stress_ok = 0; break; }
        free(p);
    }
    serial_puts(stress_ok ? "OK\n" : "FAIL\n");

    /* Test 6: free(NULL) is safe */
    free((void *)0);
    serial_puts("[heap-test] free(NULL): OK\n");

    serial_puts("[heap-test] all tests done\n");
}
#endif
```

Run command:

```bash
make clean-kernel
make kernel.elf KCFLAGS='-m32 -std=gnu11 -ffreestanding -O2 -Wall -Wextra -fno-stack-protector -fno-pie -fno-asynchronous-unwind-tables -fno-unwind-tables -MMD -MP -I. -DPHASE3_HEAP_TEST'
timeout 10s qemu-system-i386 -kernel kernel.elf -serial stdio -display none -monitor none
```

All tests must print `OK` with zero `FAIL` lines.

### 9.6 Phase 2 Regression Check

The following must continue to work after paging is enabled:

- `[pit] heartbeat` is printed to serial (timer interrupt working)
- `sendkey a` in QEMU monitor → `[kbd] scancode=...` is printed to serial (keyboard interrupt working)
- VGA output is normal (identity mapping allows access to `0xB8000`)

### 9.7 MoonBit Path Regression Check

```bash
timeout 6s make run-moon-kernel-serial 2>&1
```

`[moon] moon_kernel_entry start` must be printed, and behavior must match Phase 2.
In particular, MoonBit's `malloc`/`free` must not crash (regression confirmation after heap allocator replacement).

---

## 10. Implementation Steps (Phase 2 Style)

| Step | Task | Verification |
|------|------|--------------|
| 3-1 | Add `__kernel_end` symbol to `linker.ld` | `make kernel.elf && make check-kernel` succeeds. `nm kernel.elf \| grep __kernel_end` confirms symbol |
| 3-2 | Implement `kernel/multiboot.h` + `kernel/multiboot.c`. Print memory map to serial from `kernel/main.c` | Section 9.2 passes |
| 3-3 | Implement `kernel/pmm.h` + `kernel/pmm.c`. `pmm_init` + `pmm_alloc_page` + `pmm_free_page` + `pmm_alloc_contiguous` | Section 9.3 passes + unit test (alloc→free→re-alloc address reuse confirmation) |
| 3-4 | Implement `kernel/paging.h` + `kernel/paging.c`. Identity mapping + recursive mapping + CR3 + CR0.PG | Section 9.4 passes (heartbeat continuation is mandatory) |
| 3-5 | Add CR2 output to exception panic handler for vector 14 (#PF) | Intentional invalid access (e.g. `*(volatile uint32_t *)0xDEAD0000 = 0;`) → panic + CR2 output confirmed |
| 3-6 | Implement `runtime/heap.h` + `runtime/heap.c`. Change `runtime/runtime_stubs.c` malloc/free to delegate | All items in Section 9.5 pass with OK |
| 3-7 | Integrate Phase 3 initialization sequence into `kernel/moon_entry.c` | Section 9.7 passes |
| 3-8 | Update Makefile (per Section 8.2) + full build path regression | All build checks in Section 9.1 |
| 3-9 | Run full verification matrix + document sync (README, TODO.md, docs/ROADMAP*.md status update) | All tests in Section 9 pass |

---

## 11. Known Design Decisions and Future Changes

### What Changes in Phase 4

- In addition to identity mapping, **per-process address spaces** will be needed.
  Each process will own the page directory returned by `paging_init`.
- CR3 will be switched during context switches.

### What Changes in Phase 5a

- User-space page table entries will have the `PTE_USER` flag set.
- Kernel-space entries will NOT have `PTE_USER` (inaccessible from Ring 3).
- The VGA buffer (`0xB8000`) will be unmapped from user space (capability enforcement).

### Heap Expansion

Phase 3 uses a fixed initial 1 MiB. In the future, add a `heap_expand` function that allocates
additional pages via `pmm_alloc_page` and appends them to the end of the free list.

### Heap Fragmentation Mitigation

The Phase 3 heap only performs forward coalescing. Perceus RC's alloc/free pattern may cause
fragmentation to accumulate. Mitigation priority:

1. Measure fragmentation severity via stress tests (`heap_dump` to check free block count and max free block size)
2. Add backward merge by converting to a doubly-linked list (Phase 4)
3. Introduce size-class free lists (segregated fit) as needed

---

## References

- [OSDev Wiki — Paging](https://wiki.osdev.org/Paging) — x86 2-level page table explanation
- [OSDev Wiki — Memory Map (x86)](https://wiki.osdev.org/Memory_Map_(x86)) — Physical memory map
- [OSDev Wiki — Writing a memory manager](https://wiki.osdev.org/Writing_a_memory_manager) — Heap allocator patterns
- [Multiboot Specification](https://www.gnu.org/software/grub/manual/multiboot/multiboot.html) — Memory map structures
- This repository `runtime/runtime_stubs.c` — Bump allocator to be replaced
- This repository `arch/x86/idt.c` — Code style reference
