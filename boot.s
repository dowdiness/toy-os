# boot.s - 32-bit Protected Mode Bootloader

.code16
.global _start

.section .text
_start:
    jmp start

# =================================================================
# GDT (先頭付近に配置)
# =================================================================
.align 8
gdt_start:
    .quad 0x0                      # Null descriptor
gdt_code:
    .word 0xFFFF, 0x0000
    .byte 0x00, 0b10011010, 0b11001111, 0x00
gdt_data:
    .word 0xFFFF, 0x0000
    .byte 0x00, 0b10010010, 0b11001111, 0x00
gdt_end:

gdt_descriptor:
    .word gdt_end - gdt_start - 1
    .long gdt_start

start:
    cli
    cld

    # セグメントレジスタ初期化
    xorw %ax, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    mov $0x7c00, %sp

    # 16-bit メッセージ
    mov $msg_realmode, %si
    call print16

    # A20有効化 (Fast A20)
    in $0x92, %al
    or $0x02, %al
    and $0xFE, %al
    out %al, $0x92

    mov $msg_a20, %si
    call print16

    # GDTロード
    lgdt gdt_descriptor

    mov $msg_gdt, %si
    call print16

    # プロテクトモードへ
    mov %cr0, %eax
    or $0x1, %eax
    mov %eax, %cr0

    ljmp $0x08, $pm_start

# --- 16-bit 文字列表示 ---
print16:
    lodsb
    test %al, %al
    jz 1f
    mov $0x0e, %ah
    int $0x10
    jmp print16
1:  ret

msg_realmode: .asciz "Real Mode\r\n"
msg_a20:      .asciz "A20\r\n"
msg_gdt:      .asciz "PM...\r\n"

# =================================================================
# 32-bit Protected Mode
# =================================================================
.code32
pm_start:
    # データセグメント設定
    mov $0x10, %eax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %ss
    mov $0x90000, %esp

    # 画面クリア (最初の行)
    mov $0xB8000, %edi
    mov $80, %ecx
    mov $0x0A20, %ax        # 緑背景スペース
    rep stosw

    # メッセージ表示
    mov $0xB8000, %edi
    mov $msg_pm, %esi
    mov $0x0A, %ah          # 緑色

.pm_print:
    lodsb
    test %al, %al
    jz .pm_halt
    stosw
    jmp .pm_print

.pm_halt:
    hlt
    jmp .pm_halt

msg_pm: .asciz "32-bit Protected Mode OK!"

# =================================================================
# Boot signature
# =================================================================
.org 510
.word 0xAA55
