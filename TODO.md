# TODO

## Phase 0: Bare-Metal C Kernel Path (Step 2)

- [x] Create task tracker for upcoming implementation work.
- [x] Add Multiboot entry assembly (`arch/x86/multiboot_boot.s`).
- [x] Add linker script for kernel ELF (`linker.ld`).
- [x] Add minimal VGA text driver (`drivers/vga.c`).
- [x] Add C kernel entrypoint (`kernel/main.c`).
- [x] Extend `Makefile` with parallel kernel targets while preserving current boot-sector targets.
- [x] Add quick verification notes/commands for the new path.
- [x] Run sanity build checks for updated targets (where toolchain is available).

## Phase 0.5: Early Serial Debug (Step 3)

- [x] Add COM1 serial driver (`drivers/serial.c`).
- [x] Print boot diagnostics to serial from `kernel/main.c`.
- [x] Add QEMU serial run target (`run-kernel-serial`) for headless debugging.
- [x] Document serial debug workflow in README files.
- [x] Run sanity build + short serial-output check.

## Code Review Fixes (Post Phase 0.5)

- [x] Add VGA shadow buffer to avoid expensive MMIO reads during scroll (`drivers/vga.c`).
- [x] Extract shared `put_hex32()` formatter into `kernel/fmt.c` to eliminate duplication.
- [x] Fix QEMU boot sector invocation to suppress spurious floppy/disk error messages.

## Phase 1: MoonBit "Hello Bare Metal" (Baseline Complete)

- [x] Initialize MoonBit module/package layout in this repository.
- [x] Add minimal MoonBit entry (`moon_kernel_entry`) using C FFI for serial/VGA output.
- [x] Add freestanding runtime support stubs (`runtime/runtime_stubs.c`) required by MoonBit runtime.
- [x] Add MoonBit <-> C bridge wrappers (`runtime/moon_kernel_ffi.c`) for `Bytes` output.
- [x] Harden runtime allocator edge cases (`malloc` overflow, `calloc` overflow, `realloc` data preservation).
- [x] Extend `Makefile` with MoonBit codegen/build/link targets (`moon-kernel.elf`).
- [x] Run `moon check/build` and compile the MoonBit kernel path.
- [x] Attempt serial boot verification for `moon-kernel.elf` and document current status.
- [x] Address MoonBit FFI `Bytes` ownership annotation warnings.
