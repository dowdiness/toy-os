# Multiboot entry stub for 32-bit C kernel startup.

.set MULTIBOOT_MAGIC,    0x1BADB002
.set MULTIBOOT_FLAGS,    (1 << 0) | (1 << 1)
.set MULTIBOOT_CHECKSUM, -(MULTIBOOT_MAGIC + MULTIBOOT_FLAGS)

.section .multiboot, "a"
.align 4
.long MULTIBOOT_MAGIC
.long MULTIBOOT_FLAGS
.long MULTIBOOT_CHECKSUM

.section .bss
.align 16
stack_bottom:
    .skip 16384
stack_top:

.section .text
.code32
.global _start

_start:
    mov $stack_top, %esp
    cld

    # C calling convention: push args right-to-left.
    push %ebx    # multiboot info pointer
    push %eax    # multiboot magic
    call kernel_main

    cli
1:
    hlt
    jmp 1b

.section .note.GNU-stack,"",@progbits
