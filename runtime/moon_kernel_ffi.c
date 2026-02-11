#include <stdint.h>

#include "drivers/serial.h"
#include "drivers/vga.h"
#include "moonbit.h"

static void write_bytes_to_serial(moonbit_bytes_t bytes) {
    int32_t len;
    int32_t i;

    if (bytes == (moonbit_bytes_t)0) {
        return;
    }

    len = (int32_t)Moonbit_array_length(bytes);
    for (i = 0; i < len; ++i) {
        serial_putchar((char)bytes[i]);
    }
}

static void write_bytes_to_vga(moonbit_bytes_t bytes) {
    int32_t len;
    int32_t i;

    if (bytes == (moonbit_bytes_t)0) {
        return;
    }

    len = (int32_t)Moonbit_array_length(bytes);
    for (i = 0; i < len; ++i) {
        vga_putchar((char)bytes[i]);
    }
}

void moon_kernel_serial_puts(moonbit_bytes_t s) {
    write_bytes_to_serial(s);
}

void moon_kernel_vga_puts(moonbit_bytes_t s) {
    write_bytes_to_vga(s);
}
