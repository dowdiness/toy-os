#include "arch/x86/idt.h"

#include <stdint.h>

#define IDT_ENTRY_COUNT 256u
#define IDT_KERNEL_CODE_SELECTOR 0x08u
#define IDT_ATTR_PRESENT 0x80u
#define IDT_ATTR_INT_GATE32 0x0Eu

struct idt_entry {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t zero;
    uint8_t type_attr;
    uint16_t offset_high;
} __attribute__((packed));

struct idt_descriptor {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

static struct idt_entry g_idt[IDT_ENTRY_COUNT];
static struct idt_descriptor g_idtr;

static void idt_clear(void) {
    uint32_t index;

    for (index = 0; index < IDT_ENTRY_COUNT; ++index) {
        g_idt[index].offset_low = 0;
        g_idt[index].selector = 0;
        g_idt[index].zero = 0;
        g_idt[index].type_attr = 0;
        g_idt[index].offset_high = 0;
    }
}

static void idt_lidt(const struct idt_descriptor *descriptor) {
    __asm__ volatile("lidt (%0)" : : "r"(descriptor));
}

void idt_set_interrupt_gate(uint8_t vector, void (*handler)(void)) {
    uintptr_t handler_addr;
    struct idt_entry *entry;

    entry = &g_idt[vector];

    if (handler == (void (*)(void))0) {
        entry->offset_low = 0;
        entry->selector = 0;
        entry->zero = 0;
        entry->type_attr = 0;
        entry->offset_high = 0;
        return;
    }

    handler_addr = (uintptr_t)handler;
    entry->offset_low = (uint16_t)(handler_addr & 0xFFFFu);
    entry->selector = IDT_KERNEL_CODE_SELECTOR;
    entry->zero = 0;
    entry->type_attr = IDT_ATTR_PRESENT | IDT_ATTR_INT_GATE32;
    entry->offset_high = (uint16_t)((handler_addr >> 16) & 0xFFFFu);
}

void idt_load(void) {
    g_idtr.limit = (uint16_t)(sizeof(g_idt) - 1u);
    g_idtr.base = (uint32_t)(uintptr_t)&g_idt[0];
    idt_lidt(&g_idtr);
}

void idt_init(void) {
    idt_clear();
    idt_load();
}
