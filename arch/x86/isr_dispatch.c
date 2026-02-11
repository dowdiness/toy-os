#include "arch/x86/isr_dispatch.h"

#include <stdint.h>

#include "arch/x86/pic.h"
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
        return;
    }

    if (frame->vector >= 32u && frame->vector <= 47u) {
        /* Step 4 baseline: ACK IRQs; Step 6/7 will add IRQ-specific dispatch. */
        /* Hardening TODO: handle spurious IRQ7/IRQ15 before sending EOI. */
        pic_send_eoi((uint8_t)(frame->vector - 32u));
    }
}
