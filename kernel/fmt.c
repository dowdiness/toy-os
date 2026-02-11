#include "kernel/fmt.h"

void put_hex32(uint32_t value, void (*puts)(const char *), void (*putchar)(char)) {
    static const char hex[] = "0123456789ABCDEF";
    int shift;

    puts("0x");
    for (shift = 28; shift >= 0; shift -= 4) {
        putchar(hex[(value >> shift) & 0x0Fu]);
    }
}
