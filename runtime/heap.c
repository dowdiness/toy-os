#include "runtime/heap.h"

#include "drivers/serial.h"
#include "kernel/fmt.h"

static struct heap_block *g_heap_head;
static uint32_t g_heap_size;

static uint32_t align_up_u32(uint32_t value, uint32_t align) {
    return (value + (align - 1u)) & ~(align - 1u);
}

void heap_init(void *base_addr, uint32_t size_bytes) {
    uintptr_t aligned = ((uintptr_t)base_addr + (HEAP_ALIGN - 1u)) & ~((uintptr_t)(HEAP_ALIGN - 1u));
    uint32_t shrink = (uint32_t)(aligned - (uintptr_t)base_addr);

    if (size_bytes <= shrink + HEAP_BLOCK_HEADER_SIZE + HEAP_MIN_ALLOC) {
        g_heap_head = (struct heap_block *)0;
        g_heap_size = 0u;
        return;
    }

    size_bytes -= shrink;
    g_heap_head = (struct heap_block *)aligned;
    g_heap_head->size = size_bytes - HEAP_BLOCK_HEADER_SIZE;
    g_heap_head->is_free = 1u;
    g_heap_head->next = (struct heap_block *)0;
    g_heap_size = size_bytes;
}

void *heap_malloc(size_t size) {
    struct heap_block *blk;
    uint32_t req;

    if (g_heap_head == (struct heap_block *)0) {
        return (void *)0;
    }

    if (size == 0u) {
        size = 1u;
    }

    if (size > 0xFFFFFFFFu) {
        return (void *)0;
    }

    req = align_up_u32((uint32_t)size, HEAP_ALIGN);
    if (req < HEAP_MIN_ALLOC) {
        req = HEAP_MIN_ALLOC;
    }

    for (blk = g_heap_head; blk != (struct heap_block *)0; blk = blk->next) {
        if (blk->is_free == 0u || blk->size < req) {
            continue;
        }

        if (blk->size >= req + HEAP_BLOCK_HEADER_SIZE + HEAP_MIN_ALLOC) {
            struct heap_block *new_blk = (struct heap_block *)((uint8_t *)(blk + 1) + req);
            new_blk->size = blk->size - req - HEAP_BLOCK_HEADER_SIZE;
            new_blk->is_free = 1u;
            new_blk->next = blk->next;
            blk->size = req;
            blk->next = new_blk;
        }

        blk->is_free = 0u;
        return (void *)(blk + 1);
    }

    return (void *)0;
}

void heap_free(void *ptr) {
    struct heap_block *blk;

    if (ptr == (void *)0) {
        return;
    }

    blk = ((struct heap_block *)ptr) - 1;
    blk->is_free = 1u;

    while (blk->next != (struct heap_block *)0 && blk->next->is_free != 0u) {
        uint8_t *blk_end = (uint8_t *)(blk + 1) + blk->size;
        if (blk_end != (uint8_t *)blk->next) {
            break;
        }
        blk->size += HEAP_BLOCK_HEADER_SIZE + blk->next->size;
        blk->next = blk->next->next;
    }
}

void *heap_calloc(size_t count, size_t size) {
    size_t total;
    uint8_t *p;
    size_t i;

    if (count != 0u && size > ((size_t)-1) / count) {
        return (void *)0;
    }

    total = count * size;
    p = (uint8_t *)heap_malloc(total);
    if (p == (uint8_t *)0) {
        return (void *)0;
    }

    for (i = 0u; i < total; ++i) {
        p[i] = 0u;
    }

    return p;
}

void *heap_realloc(void *ptr, size_t new_size) {
    struct heap_block *blk;
    void *new_ptr;
    size_t copy_size;
    size_t i;

    if (ptr == (void *)0) {
        return heap_malloc(new_size);
    }

    if (new_size == 0u) {
        heap_free(ptr);
        return (void *)0;
    }

    blk = ((struct heap_block *)ptr) - 1;
    if (blk->size >= new_size) {
        return ptr;
    }

    new_ptr = heap_malloc(new_size);
    if (new_ptr == (void *)0) {
        return (void *)0;
    }

    copy_size = blk->size;
    for (i = 0u; i < copy_size; ++i) {
        ((uint8_t *)new_ptr)[i] = ((uint8_t *)ptr)[i];
    }

    heap_free(ptr);
    return new_ptr;
}

void heap_dump(void) {
    struct heap_block *blk;

    serial_puts("[heap] dump size=");
    put_hex32(g_heap_size, serial_puts, serial_putchar);
    serial_puts("\n");

    for (blk = g_heap_head; blk != (struct heap_block *)0; blk = blk->next) {
        serial_puts("  blk ");
        put_hex32((uint32_t)(uintptr_t)blk, serial_puts, serial_putchar);
        serial_puts(" size=");
        put_hex32(blk->size, serial_puts, serial_putchar);
        serial_puts(blk->is_free ? " free\n" : " used\n");
    }
}
