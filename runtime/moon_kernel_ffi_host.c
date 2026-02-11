#include <stdint.h>

void moon_kernel_serial_puts(uint8_t *s) {
    (void)s;
}

void moon_kernel_vga_puts(uint8_t *s) {
    (void)s;
}

int32_t moon_kernel_get_ticks(void) {
    return 0;
}

int32_t moon_kernel_keyboard_pop_event(void) {
    return 0;
}
