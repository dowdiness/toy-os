# =================================================================
# OS自作 "Hello, World!" (ブートセクタ版) Makefile
# =================================================================

# 変数定義
AS      = as
LD      = ld
OBJCOPY = objcopy
DD      = dd
QEMU    = qemu-system-i386
MOON    = moon

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
KERNEL_OBJS  = arch/x86/multiboot_boot.o arch/x86/idt.o drivers/vga.o drivers/serial.o kernel/fmt.o kernel/main.o

KCFLAGS      = -m32 -std=gnu11 -ffreestanding -O2 -Wall -Wextra -fno-stack-protector -fno-pie -fno-asynchronous-unwind-tables -fno-unwind-tables -MMD -MP -I.
KASFLAGS     = --32
KLDFLAGS     = -m32 -ffreestanding -nostdlib -no-pie -Wl,--build-id=none -T linker.ld
KLIBS        ?=
KERNEL_DEPS  = $(KERNEL_OBJS:.o=.d)

# -----------------------------------------------------------------
# Phase 1 MoonBit kernel path
# -----------------------------------------------------------------
MOON_INCLUDE_DIR ?= $(HOME)/.moon/include
MOON_RUNTIME_C   ?= $(HOME)/.moon/lib/runtime.c
MOON_MAIN_PKG    ?= cmd/moon_kernel
MOON_MAIN_NAME   := $(notdir $(MOON_MAIN_PKG))
MOON_GEN_C       ?= _build/native/debug/build/$(MOON_MAIN_PKG)/$(MOON_MAIN_NAME).c
MOON_GEN_O       ?= _build/native/debug/build/$(MOON_MAIN_PKG)/$(MOON_MAIN_NAME).o

MOON_KERNEL_ELF  ?= moon-kernel.elf
MOON_KERNEL_OBJS = arch/x86/multiboot_boot.o arch/x86/idt.o drivers/vga.o drivers/serial.o \
                   runtime/runtime_stubs.o runtime/moon_kernel_ffi.o runtime/moon_runtime.o \
                   kernel/moon_entry.o $(MOON_GEN_O)
MOON_KCFLAGS     = $(KCFLAGS) -DMOONBIT_NATIVE_NO_SYS_HEADER -I$(MOON_INCLUDE_DIR)
MOON_KERNEL_DEPS = $(MOON_KERNEL_OBJS:.o=.d)
.DEFAULT_GOAL := all

# -----------------------------------------------------------------
# デフォルトターゲット: 全てをビルドし、QEMUで実行する
# -----------------------------------------------------------------
all: $(FINAL_IMG) run

-include $(KERNEL_DEPS) $(MOON_KERNEL_DEPS)

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
	$(QEMU) -drive format=raw,file=$(FINAL_IMG),if=floppy -boot a

# -----------------------------------------------------------------
# Phase 0 C kernel path targets
# -----------------------------------------------------------------
$(KERNEL_ELF): $(KERNEL_OBJS) linker.ld
	$(KCC) $(KLDFLAGS) $(KERNEL_OBJS) -o $(KERNEL_ELF) $(KLIBS)

arch/x86/multiboot_boot.o: arch/x86/multiboot_boot.s
	$(KAS) $(KASFLAGS) $< -o $@

arch/x86/idt.o: arch/x86/idt.c arch/x86/idt.h
	$(KCC) $(KCFLAGS) -c $< -o $@

drivers/vga.o: drivers/vga.c
	$(KCC) $(KCFLAGS) -c $< -o $@

drivers/serial.o: drivers/serial.c
	$(KCC) $(KCFLAGS) -c $< -o $@

kernel/fmt.o: kernel/fmt.c
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
# Phase 1 MoonBit kernel path targets
# -----------------------------------------------------------------
moon-gen: $(MOON_GEN_C)

$(MOON_GEN_C): moon.mod.json moon.pkg moon_kernel.mbt cmd/moon_kernel/moon.pkg cmd/moon_kernel/main.mbt runtime/moon_kernel_ffi_host.c
	$(MOON) build --target native $(MOON_MAIN_PKG)

$(MOON_GEN_O): $(MOON_GEN_C)
	$(KCC) $(MOON_KCFLAGS) -c $< -o $@

runtime/runtime_stubs.o: runtime/runtime_stubs.c
	$(KCC) $(MOON_KCFLAGS) -c $< -o $@

runtime/moon_kernel_ffi.o: runtime/moon_kernel_ffi.c
	$(KCC) $(MOON_KCFLAGS) -c $< -o $@

runtime/moon_runtime.o: $(MOON_RUNTIME_C)
	$(KCC) $(MOON_KCFLAGS) -c $< -o $@

kernel/moon_entry.o: kernel/moon_entry.c
	$(KCC) $(MOON_KCFLAGS) -c $< -o $@

$(MOON_KERNEL_ELF): $(MOON_KERNEL_OBJS) linker.ld
	$(KCC) $(KLDFLAGS) $(MOON_KERNEL_OBJS) -o $(MOON_KERNEL_ELF) $(KLIBS)

run-moon-kernel: $(MOON_KERNEL_ELF)
	$(QEMU) -kernel $(MOON_KERNEL_ELF)

run-moon-kernel-serial: $(MOON_KERNEL_ELF)
	$(QEMU) -kernel $(MOON_KERNEL_ELF) -serial stdio -display none -monitor none

check-moon-kernel: $(MOON_KERNEL_ELF)
	@if command -v grub-file >/dev/null 2>&1; then \
		grub-file --is-x86-multiboot $(MOON_KERNEL_ELF) && echo "MoonBit kernel multiboot header: OK"; \
	else \
		echo "grub-file not found; skipping multiboot check."; \
	fi

clean-moon-kernel:
	rm -f $(MOON_KERNEL_ELF) $(MOON_KERNEL_OBJS) $(MOON_KERNEL_DEPS)

# -----------------------------------------------------------------
# クリーンアップ
# -----------------------------------------------------------------
clean:
	rm -f $(OBJ) boot.elf $(IMG) $(FINAL_IMG) \
		$(KERNEL_ELF) $(KERNEL_OBJS) $(KERNEL_DEPS) \
		$(MOON_KERNEL_ELF) $(MOON_KERNEL_OBJS) $(MOON_KERNEL_DEPS)

# .PHONY: all, run, clean などのターゲットは常に実行
.PHONY: all run clean \
	run-kernel run-kernel-serial check-kernel clean-kernel \
	moon-gen run-moon-kernel run-moon-kernel-serial check-moon-kernel clean-moon-kernel
