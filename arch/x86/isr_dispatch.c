#include "arch/x86/isr_dispatch.h"

#include <stdint.h>

#include "drivers/serial.h"
#include "kernel/fmt.h"

void isr_common_handler(struct isr_frame *frame) {
    if (frame->vector < 32u) {
        serial_puts("[isr] exception vector=");
        put_hex32(frame->vector, serial_puts, serial_putchar);
        serial_puts(" error=");
        put_hex32(frame->error_code, serial_puts, serial_putchar);
        serial_puts(" eip=");
        put_hex32(frame->eip, serial_puts, serial_putchar);
        serial_puts("\n");
    }
}
