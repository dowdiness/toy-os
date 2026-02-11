# =================================================================
# OS自作 "Hello, World!" (ブートセクタ版) Makefile
# =================================================================

# 変数定義
AS      = as
LD      = ld
OBJCOPY = objcopy
DD      = dd
QEMU    = qemu-system-i386

SRC     = boot.s
OBJ     = boot.o
IMG     = boot.img
FINAL_IMG = boot_512.img

TTEXT   = 0x7c00
ARCH    = elf_i386

# -----------------------------------------------------------------
# Phase 0 C kernel path (Multiboot + freestanding C)
# -----------------------------------------------------------------
# Recommended override when available:
#   make KERNEL_CROSS=i686-elf- kernel.elf
KERNEL_CROSS ?=
KCC          = $(KERNEL_CROSS)gcc
KAS          = $(KERNEL_CROSS)as

KERNEL_ELF   = kernel.elf
KERNEL_OBJS  = arch/x86/multiboot_boot.o drivers/vga.o drivers/serial.o kernel/main.o

KCFLAGS      = -m32 -std=gnu11 -ffreestanding -O2 -Wall -Wextra -fno-stack-protector -fno-pie -fno-asynchronous-unwind-tables -fno-unwind-tables -MMD -MP -I.
KASFLAGS     = --32
KLDFLAGS     = -m32 -ffreestanding -nostdlib -no-pie -Wl,--build-id=none -T linker.ld
KLIBS        ?=
KERNEL_DEPS  = $(KERNEL_OBJS:.o=.d)
.DEFAULT_GOAL := all

# -----------------------------------------------------------------
# デフォルトターゲット: 全てをビルドし、QEMUで実行する
# -----------------------------------------------------------------
all: $(FINAL_IMG) run

-include $(KERNEL_DEPS)

# -----------------------------------------------------------------
# 最終イメージの生成: boot.img から 512バイトにトリミング
# -----------------------------------------------------------------
$(FINAL_IMG): $(IMG)
	# ddでファイルの先頭512バイトを切り出す
	$(DD) if=$(IMG) of=$(FINAL_IMG) bs=512 count=1

# -----------------------------------------------------------------
# バイナリイメージの生成 (objcopyとldの組み合わせ)
# -----------------------------------------------------------------
$(IMG): $(OBJ)
	# リンク: 32bit ELFを生成 (objcopyに渡すため)
	$(LD) -m $(ARCH) -Ttext $(TTEXT) $(OBJ) -o boot.elf
	# objcopy: ELFから純粋なバイナリコードを抽出
	$(OBJCOPY) -O binary boot.elf $(IMG)

# -----------------------------------------------------------------
# アセンブル
# -----------------------------------------------------------------
$(OBJ): $(SRC)
	# 32ビット i386アーキテクチャでアセンブル
	$(AS) --32 $(SRC) -o $(OBJ)

# -----------------------------------------------------------------
# 実行
# -----------------------------------------------------------------
run: $(FINAL_IMG)
	# QEMUでフロッピーとして起動
	$(QEMU) -drive format=raw,file=$(FINAL_IMG) -boot a

# -----------------------------------------------------------------
# Phase 0 C kernel path targets
# -----------------------------------------------------------------
$(KERNEL_ELF): $(KERNEL_OBJS) linker.ld
	$(KCC) $(KLDFLAGS) $(KERNEL_OBJS) -o $(KERNEL_ELF) $(KLIBS)

arch/x86/multiboot_boot.o: arch/x86/multiboot_boot.s
	$(KAS) $(KASFLAGS) $< -o $@

drivers/vga.o: drivers/vga.c
	$(KCC) $(KCFLAGS) -c $< -o $@

drivers/serial.o: drivers/serial.c
	$(KCC) $(KCFLAGS) -c $< -o $@

kernel/main.o: kernel/main.c
	$(KCC) $(KCFLAGS) -c $< -o $@

run-kernel: $(KERNEL_ELF)
	$(QEMU) -kernel $(KERNEL_ELF)

run-kernel-serial: $(KERNEL_ELF)
	$(QEMU) -kernel $(KERNEL_ELF) -serial stdio -display none -monitor none

check-kernel: $(KERNEL_ELF)
	@if command -v grub-file >/dev/null 2>&1; then \
		grub-file --is-x86-multiboot $(KERNEL_ELF) && echo "Multiboot header: OK"; \
	else \
		echo "grub-file not found; skipping multiboot check."; \
	fi

clean-kernel:
	rm -f $(KERNEL_ELF) $(KERNEL_OBJS) $(KERNEL_DEPS)

# -----------------------------------------------------------------
# クリーンアップ
# -----------------------------------------------------------------
clean:
	rm -f $(OBJ) boot.elf $(IMG) $(FINAL_IMG) $(KERNEL_ELF) $(KERNEL_OBJS) $(KERNEL_DEPS)

# .PHONY: all, run, clean などのターゲットは常に実行
.PHONY: all run clean run-kernel run-kernel-serial check-kernel clean-kernel
