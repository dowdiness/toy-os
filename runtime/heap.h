#ifndef RUNTIME_HEAP_H
#define RUNTIME_HEAP_H

#include <stddef.h>
#include <stdint.h>

struct heap_block {
    uint32_t size;
    uint32_t is_free;
    struct heap_block *next;
    uint32_t _pad;  /* Padding to make sizeof(heap_block) == 16, aligning to HEAP_ALIGN=8 */
};

#define HEAP_BLOCK_HEADER_SIZE ((uint32_t)sizeof(struct heap_block))
#define HEAP_MIN_ALLOC 8u
#define HEAP_ALIGN 8u

void heap_init(void *base_addr, uint32_t size_bytes);
void *heap_malloc(size_t size);
void heap_free(void *ptr);
void *heap_calloc(size_t count, size_t size);
void *heap_realloc(void *ptr, size_t new_size);
void heap_dump(void);

#endif
