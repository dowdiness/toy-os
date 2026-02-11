# Toy OS

[EN](README.md) | [日本語](README_JA.md)

Minimal bare-metal x86 bootloader project.
Current state:
- Boot-sector path: 16-bit startup -> 32-bit protected mode transition in `boot.s`.
- Phase 0 kernel path: Multiboot + freestanding C kernel build is available.

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

## Documentation

- [Documentation index](docs/README.md): recommended read order.
- [Protected mode tutorial](docs/tutorial-01-protected-mode.md): step-by-step 16-bit -> 32-bit transition.
- [Canonical roadmap (JA)](docs/ROADMAP.md): detailed implementation roadmap.
- [Roadmap companion (EN)](docs/ROADMAP_EN.md): token-efficient roadmap summary.
- [Canonical report (JA)](docs/REPORT_JA.md): deep technical report.
- [Report companion (EN)](docs/REPORT_EN.md): token-efficient report summary.
