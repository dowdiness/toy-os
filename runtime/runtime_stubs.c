#include <stddef.h>
#include <stdint.h>

#include "drivers/serial.h"
#include "runtime/heap.h"

static void halt_forever(void) {
    __asm__ volatile("cli");
    for (;;) {
        __asm__ volatile("hlt");
    }
}

void *malloc(size_t size) {
    return heap_malloc(size);
}

void free(void *ptr) {
    heap_free(ptr);
}

void *calloc(size_t count, size_t size) {
    return heap_calloc(count, size);
}

void *realloc(void *ptr, size_t size) {
    return heap_realloc(ptr, size);
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
