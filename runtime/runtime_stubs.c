#include <stddef.h>
#include <stdint.h>

#include "drivers/serial.h"

#define HEAP_SIZE (4 * 1024 * 1024)
#define ALLOC_ALIGN 8u

struct alloc_header {
    size_t size;
};

static union {
    uint8_t bytes[HEAP_SIZE];
    uintptr_t align;
} heap_storage;

static size_t heap_offset = 0;

static uint8_t *heap_begin(void) {
    return &heap_storage.bytes[0];
}

static uint8_t *heap_end(void) {
    return &heap_storage.bytes[heap_offset];
}

static void halt_forever(void) {
    __asm__ volatile("cli");
    for (;;) {
        __asm__ volatile("hlt");
    }
}

void *malloc(size_t size) {
    size_t aligned;
    size_t total;
    struct alloc_header *header;
    void *ptr;

    if (size == 0) {
        size = 1;
    }

    if (size > ((size_t)-1) - (ALLOC_ALIGN - 1u)) {
        return (void *)0;
    }

    aligned = (size + (ALLOC_ALIGN - 1u)) & ~((size_t)(ALLOC_ALIGN - 1u));
    if (aligned > ((size_t)-1) - sizeof(struct alloc_header)) {
        return (void *)0;
    }

    total = sizeof(struct alloc_header) + aligned;
    if (total > HEAP_SIZE - heap_offset) {
        return (void *)0;
    }

    header = (struct alloc_header *)(void *)(heap_begin() + heap_offset);
    header->size = size;
    ptr = (void *)(header + 1);
    heap_offset += total;
    return ptr;
}

void free(void *ptr) {
    (void)ptr;
}

void *calloc(size_t count, size_t size) {
    size_t total;
    uint8_t *buf;
    size_t i;

    if (count != 0 && size > ((size_t)-1) / count) {
        return (void *)0;
    }
    total = count * size;
    buf = (uint8_t *)malloc(total);

    if (buf == (uint8_t *)0) {
        return (void *)0;
    }

    for (i = 0; i < total; ++i) {
        buf[i] = 0;
    }
    return buf;
}

void *realloc(void *ptr, size_t size) {
    struct alloc_header *header;
    uint8_t *base;
    uint8_t *limit;
    void *new_ptr;
    size_t old_size;
    size_t copy_size;
    size_t i;

    if (ptr == (void *)0) {
        return malloc(size);
    }

    if (size == 0) {
        free(ptr);
        return (void *)0;
    }

    base = heap_begin();
    limit = heap_end();

    if ((uint8_t *)ptr < base + sizeof(struct alloc_header) || (uint8_t *)ptr >= limit) {
        return (void *)0;
    }

    header = ((struct alloc_header *)ptr) - 1;
    if ((uint8_t *)header < base || (uint8_t *)(header + 1) > limit) {
        return (void *)0;
    }

    old_size = header->size;
    new_ptr = malloc(size);
    if (new_ptr == (void *)0) {
        return (void *)0;
    }

    copy_size = old_size < size ? old_size : size;
    for (i = 0; i < copy_size; ++i) {
        ((uint8_t *)new_ptr)[i] = ((uint8_t *)ptr)[i];
    }

    return new_ptr;
}

void *memset(void *dst, int c, size_t n) {
    uint8_t *d = (uint8_t *)dst;
    size_t i;

    for (i = 0; i < n; ++i) {
        d[i] = (uint8_t)c;
    }
    return dst;
}

void *memcpy(void *dst, const void *src, size_t n) {
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    size_t i;

    for (i = 0; i < n; ++i) {
        d[i] = s[i];
    }
    return dst;
}

void *memmove(void *dst, const void *src, size_t n) {
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    size_t i;

    if (d < s) {
        for (i = 0; i < n; ++i) {
            d[i] = s[i];
        }
    } else if (d > s) {
        for (i = n; i > 0; --i) {
            d[i - 1] = s[i - 1];
        }
    }
    return dst;
}

int memcmp(const void *lhs, const void *rhs, size_t n) {
    const uint8_t *a = (const uint8_t *)lhs;
    const uint8_t *b = (const uint8_t *)rhs;
    size_t i;

    for (i = 0; i < n; ++i) {
        if (a[i] != b[i]) {
            return (int)a[i] - (int)b[i];
        }
    }
    return 0;
}

size_t strlen(const char *s) {
    size_t n = 0;

    while (s[n] != '\0') {
        ++n;
    }
    return n;
}

int strcmp(const char *s1, const char *s2) {
    while (*s1 != '\0' && *s1 == *s2) {
        ++s1;
        ++s2;
    }
    return (int)(unsigned char)*s1 - (int)(unsigned char)*s2;
}

int strncmp(const char *s1, const char *s2, size_t n) {
    size_t i;

    for (i = 0; i < n; ++i) {
        unsigned char c1 = (unsigned char)s1[i];
        unsigned char c2 = (unsigned char)s2[i];
        if (c1 != c2) {
            return (int)c1 - (int)c2;
        }
        if (c1 == '\0') {
            return 0;
        }
    }
    return 0;
}

int putchar(int c) {
    if (c == '\n') {
        serial_putchar('\r');
    }
    serial_putchar((char)c);
    return c;
}

long write(int fd, const void *buf, size_t n) {
    const uint8_t *bytes = (const uint8_t *)buf;
    size_t i;

    (void)fd;
    for (i = 0; i < n; ++i) {
        putchar((int)bytes[i]);
    }
    return (long)n;
}

void abort(void) {
    serial_puts("[moon-runtime] abort\n");
    halt_forever();
}

void exit(int status) {
    (void)status;
    serial_puts("[moon-runtime] exit\n");
    halt_forever();
}
