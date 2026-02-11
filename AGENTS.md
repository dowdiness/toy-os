# AGENTS.md - toy-os

## Build Commands
```sh
make          # Build and run in QEMU
make run      # Run existing boot_512.img in QEMU
make clean    # Remove all generated files (*.o, *.elf, *.img)
```

## Architecture
- **Bare-metal x86 bootloader** that prints "Hello, World!" using BIOS interrupts
- **boot.s**: 16-bit real mode assembly (i386), entry point at 0x7c00
- **Makefile**: Uses GNU as, ld, objcopy; runs on qemu-system-i386

## Code Style (x86 Assembly - AT&T Syntax)
- Use AT&T syntax: `mov $src, %dst` (source first, % prefix for registers)
- Comment with `#` for inline comments
- Japanese comments are acceptable for documentation
- Include `.code16` directive for 16-bit real mode
- End boot sector with `.word 0xaa55` MBR signature at byte 510
