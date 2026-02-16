# Toy OS

[EN](README.md) | [日本語](README_JA.md)

Minimal bare-metal x86 bootloader project.
Current state:
- Boot-sector path: 16-bit startup -> 32-bit protected mode transition in `boot.s`.
- Phase 0 kernel path: Multiboot + freestanding C kernel build is available.
- Phase 1 kernel path: MoonBit-generated kernel path boots and logs via COM1 serial.
- Phase 2 interrupt foundations: completed (Steps 1-10; implementation through Step 9 + Step 10 documentation sync).
- Phase 3 memory management: **completed** ([spec](docs/SPEC_PHASE3_MEMORY_EN.md)) - multiboot mmap, bitmap PMM, identity paging, free-list heap.

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

## MoonBit Development

```sh
make check                           # Check MoonBit code (uses native backend)
make test                            # Run MoonBit tests (uses native backend)
moon check                           # Direct moon check (native backend is default)
moon test                            # Direct moon test (native backend is default)
```

## Phase 2 Verification (Step 9)

```sh
make check
make kernel.elf && make check-kernel
make moon-kernel.elf && make check-moon-kernel
timeout 6s make run-kernel-serial
timeout 6s make run-moon-kernel-serial
```

## Phase 3 Verification

Verify memory management subsystems:

```sh
make check
make kernel.elf && make check-kernel
make moon-kernel.elf && make check-moon-kernel
timeout 6s make run-kernel-serial
timeout 6s make run-moon-kernel-serial
```

Expected output should show:
- `[mmap] entries: 0x00000006` (memory map parsed)
- `[pmm] total=..., free=...` (both non-zero)
- `[paging] enabled, identity-mapped ... MiB`
- `[heap] initialized (1 MiB at ...)`
- `[pit] heartbeat` (continues after paging enabled)

Heap self-test (optional, compile-time only):

```sh
make clean-kernel
make kernel.elf KCFLAGS='-m32 -std=gnu11 -ffreestanding -O2 -Wall -Wextra -fno-stack-protector -fno-pie -fno-asynchronous-unwind-tables -fno-unwind-tables -MMD -MP -I. -DPHASE3_HEAP_TEST'
timeout 6s qemu-system-i386 -kernel kernel.elf -serial stdio -display none -monitor none
```

Phase 2 fault-path self-test (optional):

```sh
make clean-kernel
make kernel.elf KCFLAGS='-m32 -std=gnu11 -ffreestanding -O2 -Wall -Wextra -fno-stack-protector -fno-pie -fno-asynchronous-unwind-tables -fno-unwind-tables -MMD -MP -I. -DPHASE2_FAULT_TEST_INT3'
timeout 6s qemu-system-i386 -kernel kernel.elf -serial stdio -display none -monitor none
```

## Driver & Kernel Notes

- VGA driver (`drivers/vga.c`) uses a RAM shadow buffer; only single-character writes hit VRAM directly, while bulk operations (scroll, clear) flush once.
- Shared hex formatter (`kernel/fmt.c`) provides `put_hex32()` via function pointers, used by both VGA and serial output paths.
- IDT foundation (`arch/x86/idt.c`) provides 256 entries, `idt_set_interrupt_gate()`, and `idt_load()` (`lidt`).
- `kernel/main.c` has a guarded fault self-test hook (`PHASE2_FAULT_TEST_INT3`) for deterministic exception-path validation.

## Runtime Notes

- `runtime/heap.c` implements a free-list allocator with splitting and coalescing.
- `heap_malloc`, `heap_free`, `heap_calloc`, `heap_realloc` provide full dynamic memory management.
- Heap is initialized with 1 MiB of contiguous physical pages from PMM.
- All allocations are 8-byte aligned with proper header padding.

## Documentation

- [Documentation index](docs/README.md): recommended read order.
- [Protected mode tutorial](docs/tutorial-01-protected-mode.md): step-by-step 16-bit -> 32-bit transition.
- [Canonical roadmap (JA)](docs/ROADMAP.md): detailed implementation roadmap.
- [Roadmap companion (EN)](docs/ROADMAP_EN.md): token-efficient roadmap summary.
- [Canonical report (JA)](docs/REPORT_JA.md): deep technical report.
- [Report companion (EN)](docs/REPORT_EN.md): token-efficient report summary.
- [Phase 3 memory spec (EN)](docs/SPEC_PHASE3_MEMORY_EN.md): implementation spec for multiboot, PMM, paging, heap.
- [Phase 3 memory spec (JA)](docs/SPEC_PHASE3_MEMORY_JA.md): 日本語版メモリ管理仕様書.
