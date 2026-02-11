#ifndef KERNEL_FMT_H
#define KERNEL_FMT_H

#include <stdint.h>

void put_hex32(uint32_t value, void (*puts)(const char *), void (*putchar)(char));

#endif
