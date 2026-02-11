#ifndef ARCH_X86_PIC_H
#define ARCH_X86_PIC_H

#include <stdint.h>

void pic_remap(uint8_t master_offset, uint8_t slave_offset);
void pic_set_mask(uint8_t irq_line);
void pic_clear_mask(uint8_t irq_line);
void pic_send_eoi(uint8_t irq_line);

#endif
