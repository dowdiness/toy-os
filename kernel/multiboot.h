#ifndef KERNEL_MULTIBOOT_H
#define KERNEL_MULTIBOOT_H

#include <stdint.h>

#define MULTIBOOT_MAGIC 0x2BADB002u

#define MULTIBOOT_FLAG_MEM  (1u << 0)
#define MULTIBOOT_FLAG_MMAP (1u << 6)

struct multiboot_info {
    uint32_t flags;
    uint32_t mem_lower;
    uint32_t mem_upper;
    uint32_t boot_device;
    uint32_t cmdline;
    uint32_t mods_count;
    uint32_t mods_addr;
    uint32_t syms[4];
    uint32_t mmap_length;
    uint32_t mmap_addr;
} __attribute__((packed));

struct multiboot_mmap_entry {
    uint32_t size;
    uint64_t addr;
    uint64_t len;
    uint32_t type;
} __attribute__((packed));

#define MULTIBOOT_MMAP_TYPE_AVAILABLE 1u

uint32_t multiboot_scan_mmap(const struct multiboot_info *info,
                             void (*callback)(uint32_t base, uint32_t length, int available));
void multiboot_dump_mmap(const struct multiboot_info *info);

#endif
