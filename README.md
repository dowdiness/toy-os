# Toy OS

[EN](README.md) | [日本語](README_JA.md)

Minimal bare-metal x86 bootloader project.
Current state:
- Boot-sector path: 16-bit startup -> 32-bit protected mode transition in `boot.s`.
- Phase 0 kernel path: Multiboot + freestanding C kernel build is available.
- Phase 1 kernel path: MoonBit-generated kernel path boots and logs via COM1 serial.
- Phase 2 interrupt foundations: Step 2 completed (`arch/x86/idt.c`, `idt_init`, `lidt`) and wired into both C and MoonBit kernel paths.

## Quickstart

```sh
make        # Build boot_512.img and run in QEMU
make run    # Run existing boot_512.img in QEMU
make clean  # Remove generated files (*.o, *.elf, *.img)
```

## Phase 0 C Kernel Path

```sh
make kernel.elf                      # Build Multiboot kernel ELF (host gcc/as, 32-bit)
make check-kernel                    # Validate Multiboot header if grub-file is installed
make run-kernel                      # Run kernel.elf directly in QEMU
make run-kernel-serial               # Headless run with COM1 output to terminal
make KERNEL_CROSS=i686-elf- kernel.elf  # Optional: use cross toolchain
```

## Phase 1 MoonBit Kernel Path

```sh
make moon-gen                        # Generate native C from MoonBit package cmd/moon_kernel
make moon-kernel.elf                 # Build freestanding MoonBit kernel ELF
make check-moon-kernel               # Validate Multiboot header
make run-moon-kernel-serial          # Headless run + serial output
```

## Driver & Kernel Notes

- VGA driver (`drivers/vga.c`) uses a RAM shadow buffer; only single-character writes hit VRAM directly, while bulk operations (scroll, clear) flush once.
- Shared hex formatter (`kernel/fmt.c`) provides `put_hex32()` via function pointers, used by both VGA and serial output paths.
- IDT foundation (`arch/x86/idt.c`) provides 256 entries, `idt_set_interrupt_gate()`, and `idt_load()` (`lidt`).

## Runtime Notes

- `runtime/runtime_stubs.c` includes overflow-safe allocation guards for `malloc` and `calloc`.
- `realloc` now preserves previous contents when growing/shrinking buffers.
- `free` is currently a no-op (bump allocator model), suitable for current early-kernel milestones.

## Documentation

- [Documentation index](docs/README.md): recommended read order.
- [Protected mode tutorial](docs/tutorial-01-protected-mode.md): step-by-step 16-bit -> 32-bit transition.
- [Canonical roadmap (JA)](docs/ROADMAP.md): detailed implementation roadmap.
- [Roadmap companion (EN)](docs/ROADMAP_EN.md): token-efficient roadmap summary.
- [Canonical report (JA)](docs/REPORT_JA.md): deep technical report.
- [Report companion (EN)](docs/REPORT_EN.md): token-efficient report summary.
