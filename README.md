# Toy OS

[EN](README.md) | [日本語](README_JA.md)

Minimal bare-metal x86 bootloader project.
Current state: boot sector prints `Hello, World!` from 16-bit real mode.

## Quickstart

```sh
make        # Build boot_512.img and run in QEMU
make run    # Run existing boot_512.img in QEMU
make clean  # Remove generated files (*.o, *.elf, *.img)
```

## Documentation

- [Documentation index](docs/README.md): recommended read order.
- [Protected mode tutorial](docs/tutorial-01-protected-mode.md): step-by-step 16-bit -> 32-bit transition.
- [Canonical roadmap (JA)](docs/ROADMAP.md): detailed implementation roadmap.
- [Roadmap companion (EN)](docs/ROADMAP_EN.md): token-efficient roadmap summary.
- [Canonical report (JA)](docs/REPORT_JA.md): deep technical report.
- [Report companion (EN)](docs/REPORT_EN.md): token-efficient report summary.
