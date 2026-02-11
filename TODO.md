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

## Phase 2: Interrupt Foundations (Step 4)

- [x] Step 1: Define Phase 2 scope and completion criteria (IDT + ISR + PIC + timer + keyboard on `moon-kernel.elf`, serial-first debug).
- [x] Step 1: Record baseline snapshot (2026-02-11).
  - `moon check --target native`: OK.
  - `make kernel.elf && make check-kernel`: OK.
  - `make moon-kernel.elf`: BLOCKED (`Makefile` still depends on missing `moon.pkg.json`).
- [x] Pre-step: fix MoonBit kernel build graph regression in `Makefile` (`moon.pkg`/`cmd/moon_kernel/moon.pkg` dependency wiring).
  - Updated `$(MOON_GEN_C)` dependencies from `*.pkg.json` to `moon.pkg` files.
  - Verification: `make moon-kernel.elf && make check-moon-kernel` now succeeds.
- [x] Step 2: Implement IDT setup in C (`idt_init`, 256 entries, `lidt`).
  - Added `arch/x86/idt.c` + `arch/x86/idt.h` with 256-entry IDT, `idt_set_interrupt_gate`, and `idt_load` (`lidt`).
  - Wired `idt_init()` into both `kernel/main.c` and `kernel/moon_entry.c`.
  - Verification: `make kernel.elf && make check-kernel` and `make moon-kernel.elf && make check-moon-kernel`.
- [x] Step 3: Add assembly ISR/IRQ stubs (`arch/x86/isr_stubs.asm`) with shared C dispatcher entry.
  - Added exception stubs (vectors 0-31) and IRQ stubs (vectors 32-47) with a unified common entry.
  - Added shared C dispatcher entry (`arch/x86/isr_dispatch.c`) and frame definition (`arch/x86/isr_dispatch.h`).
  - `idt_init()` now installs default gates from the stub table before `lidt`.
  - Verification: `make kernel.elf && make check-kernel`, `make moon-kernel.elf && make check-moon-kernel`, serial boot smoke tests for both paths.
- [x] Step 4: Add PIC remap + IRQ mask/EOI control (master/slave PIC at remapped vectors).
  - Added `arch/x86/pic.c` + `arch/x86/pic.h` (`pic_remap`, `pic_set_mask`, `pic_clear_mask`, `pic_send_eoi`).
  - Wired PIC remap (`0x20`, `0x28`) into both `kernel/main.c` and `kernel/moon_entry.c`.
  - Shared dispatcher now issues PIC EOI for IRQ vectors `32-47`.
  - Verification: kernel/moon-kernel build+multiboot checks and serial boot smoke tests.
- [ ] Step 5: Implement common interrupt dispatcher + exception panic path (vector/error/EIP over serial).
- [ ] Step 5.h: Add IRQ dispatch table/callback routing in `isr_common_handler` for PIT/keyboard integration.
- [ ] Step 5.h: Add spurious IRQ7/IRQ15 filtering (PIC ISR check) before EOI for robustness.
- [ ] Step 6: Wire PIT timer IRQ (IRQ0) with tick counter + rate-limited serial heartbeat.
- [ ] Step 7: Wire PS/2 keyboard IRQ (IRQ1) with scancode capture + serial logging.
- [ ] Step 8: Expose minimal MoonBit-facing interrupt API (ticks + keyboard event polling/queue).
- [ ] Step 9: Run Phase 2 verification matrix (fault path + timer + keyboard + regression boot checks).
- [ ] Step 10: Update docs (`README.md`, `README_JA.md`, `docs/README.md`, roadmap/report status) for Phase 2 progress.
