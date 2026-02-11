.section .text
.code32

.extern isr_common_handler

.macro ISR_NOERR num
.global isr_stub_\num
isr_stub_\num:
    pushl $0
    pushl $\num
    jmp isr_common_entry
.endm

.macro ISR_ERR num
.global isr_stub_\num
isr_stub_\num:
    pushl $\num
    jmp isr_common_entry
.endm

.macro IRQ_STUB irq vector
.global irq_stub_\irq
irq_stub_\irq:
    pushl $0
    pushl $\vector
    jmp isr_common_entry
.endm

ISR_NOERR 0
ISR_NOERR 1
ISR_NOERR 2
ISR_NOERR 3
ISR_NOERR 4
ISR_NOERR 5
ISR_NOERR 6
ISR_NOERR 7
ISR_ERR   8
ISR_NOERR 9
ISR_ERR   10
ISR_ERR   11
ISR_ERR   12
ISR_ERR   13
ISR_ERR   14
ISR_NOERR 15
ISR_NOERR 16
ISR_ERR   17
ISR_NOERR 18
ISR_NOERR 19
ISR_NOERR 20
ISR_ERR   21
ISR_NOERR 22
ISR_NOERR 23
ISR_NOERR 24
ISR_NOERR 25
ISR_NOERR 26
ISR_NOERR 27
ISR_NOERR 28
ISR_ERR   29
ISR_ERR   30
ISR_NOERR 31

IRQ_STUB 0,  32
IRQ_STUB 1,  33
IRQ_STUB 2,  34
IRQ_STUB 3,  35
IRQ_STUB 4,  36
IRQ_STUB 5,  37
IRQ_STUB 6,  38
IRQ_STUB 7,  39
IRQ_STUB 8,  40
IRQ_STUB 9,  41
IRQ_STUB 10, 42
IRQ_STUB 11, 43
IRQ_STUB 12, 44
IRQ_STUB 13, 45
IRQ_STUB 14, 46
IRQ_STUB 15, 47

.global isr_common_entry
isr_common_entry:
    cld
    pushal
    pushl %ds
    pushl %es
    pushl %fs
    pushl %gs

    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs

    pushl %esp
    call isr_common_handler
    addl $4, %esp

    popl %gs
    popl %fs
    popl %es
    popl %ds
    popal
    addl $8, %esp
    iret

.section .rodata
.align 4
.global g_interrupt_stub_table
.global g_interrupt_stub_table_end
g_interrupt_stub_table:
    .long isr_stub_0
    .long isr_stub_1
    .long isr_stub_2
    .long isr_stub_3
    .long isr_stub_4
    .long isr_stub_5
    .long isr_stub_6
    .long isr_stub_7
    .long isr_stub_8
    .long isr_stub_9
    .long isr_stub_10
    .long isr_stub_11
    .long isr_stub_12
    .long isr_stub_13
    .long isr_stub_14
    .long isr_stub_15
    .long isr_stub_16
    .long isr_stub_17
    .long isr_stub_18
    .long isr_stub_19
    .long isr_stub_20
    .long isr_stub_21
    .long isr_stub_22
    .long isr_stub_23
    .long isr_stub_24
    .long isr_stub_25
    .long isr_stub_26
    .long isr_stub_27
    .long isr_stub_28
    .long isr_stub_29
    .long isr_stub_30
    .long isr_stub_31
    .long irq_stub_0
    .long irq_stub_1
    .long irq_stub_2
    .long irq_stub_3
    .long irq_stub_4
    .long irq_stub_5
    .long irq_stub_6
    .long irq_stub_7
    .long irq_stub_8
    .long irq_stub_9
    .long irq_stub_10
    .long irq_stub_11
    .long irq_stub_12
    .long irq_stub_13
    .long irq_stub_14
    .long irq_stub_15
g_interrupt_stub_table_end:

.section .note.GNU-stack,"",@progbits
