# MoonBit Toy OS — Roadmap (English Companion)

This file is a compact English companion to the canonical roadmap:
- Canonical detailed roadmap (JA): [ROADMAP.md](./ROADMAP.md)
- Documentation index/read order: [README.md](./README.md)
- Technical rationale report: [REPORT_JA.md](./REPORT_JA.md) ([REPORT_EN.md](./REPORT_EN.md) summary)
- Protected mode implementation tutorial: [tutorial-01-protected-mode.md](./tutorial-01-protected-mode.md)

## Purpose

Use this file for quick planning context with minimal tokens.
Use [ROADMAP.md](./ROADMAP.md) when exact implementation detail is required.

## Implementation Status (February 11, 2026)

- Completed: boot-sector path with 16-bit -> 32-bit transition.
- Completed: Phase 0 C kernel path (`kernel.elf`) with Multiboot validation.
- Completed: early serial debugging path (`run-kernel-serial`).
- Completed: initial Phase 1 MoonBit path (`moon-kernel.elf`) booting and printing via serial.
- Completed: runtime allocator hardening in `runtime/runtime_stubs.c`
  (`malloc` overflow guard, `calloc` overflow guard, `realloc` data preservation).
- Completed: MoonBit FFI `Bytes` ownership warnings resolved in `moon_kernel.mbt`
  using `#borrow` annotations for C extern calls.
- Completed: VGA shadow buffer to reduce MMIO traffic during scroll.
- Completed: shared `put_hex32()` formatter extracted to `kernel/fmt.c` (DRY).
- Completed: QEMU boot-sector invocation fix (eliminated spurious error messages).

## Architecture Snapshot

1. Write kernel logic in MoonBit.
2. Generate C via `moon build --target native`.
3. Cross-compile generated C + runtime stubs + assembly for i686.
4. Link into `kernel.elf` and run on QEMU/GRUB.

## Guiding Constraints

- MoonBit C backend uses reference counting (Perceus style), not tracing GC.
- Freestanding environment must provide allocator/runtime symbols (`malloc`, `free`, `memcpy`, `memset`, `abort`, etc.).
- `extern "C"` FFI bridges MoonBit with C/assembly drivers.
- `extern type` is suitable for raw pointers (e.g., MMIO / hardware buffers).

## Milestones (Condensed)

1. Phase 0: Toolchain + pure C bare-metal validation.
   - Build i686-elf toolchain.
   - Boot with Multiboot-compatible entry.
   - Verify VGA or serial output in QEMU.
2. Phase 1: First MoonBit kernel integration.
   - Generate C from MoonBit.
   - Provide freestanding runtime stubs.
   - Link and boot MoonBit-backed kernel entry.
3. Phase 2: Interrupt foundations.
   - Set up GDT/IDT.
   - Add ISR stubs + PIC remapping.
   - Wire timer/keyboard paths.
4. Phase 3: Memory management upgrades.
   - Move from bump allocator to reusable allocator.
   - Validate RC-driven allocation/free behavior under load.
5. Phase 4+: Process model and userland experiments.
   - Introduce task/process abstraction.
   - Explore MoonBit-based user-space binaries/calls.

## Practical Next Step

Follow [ROADMAP.md](./ROADMAP.md) phase-by-phase and keep this file as the high-level checklist.
