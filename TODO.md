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
- [x] Step 5: Implement common interrupt dispatcher + exception panic path (vector/error/EIP over serial).
  - `arch/x86/isr_dispatch.c` now panics+halts on exceptions with vector/error/EIP/CS/EFLAGS and register dump.
  - Added IRQ dispatch routing via callback table (`isr_register_irq_handler` / `isr_unregister_irq_handler`).
  - Added fallback log for unexpected vectors outside exception/IRQ ranges.
- [x] Step 5.h: Add IRQ dispatch table/callback routing in `isr_common_handler` for PIT/keyboard integration.
- [x] Step 5.h: Add spurious IRQ7/IRQ15 filtering (PIC ISR check) before EOI for robustness.
  - Added `pic_get_isr()` in `arch/x86/pic.c` and spurious handling in dispatcher (IRQ15 sends master cascade EOI only).
  - Verification: kernel/moon-kernel build+multiboot checks and serial boot smoke tests.
- [x] Step 6: Wire PIT timer IRQ (IRQ0) with tick counter + rate-limited serial heartbeat.
  - Added `arch/x86/pit.c` + `arch/x86/pit.h` with PIT programming, IRQ0 handler registration, and tick counter.
  - Added rate-limited heartbeat (`[pit] heartbeat`) from IRQ0 handler (default once per second at 100Hz).
  - Enabled IRQ baseline masking (mask all, unmask IRQ0) and `sti` + idle loop in both kernel entry paths.
  - Verification: kernel/moon-kernel build+multiboot checks and serial boot logs with heartbeat output.
- [x] Step 7: Wire PS/2 keyboard IRQ (IRQ1) with scancode capture + serial logging.
  - Added `arch/x86/keyboard.c` + `arch/x86/keyboard.h` and registered IRQ1 handler via `isr_register_irq_handler(1u, ...)`.
  - Baseline IRQ mask now unmasks IRQ0 + IRQ1 in both entry paths; keyboard initialization wired into both kernels.
  - IRQ1 handler reads status/data ports (`0x64`/`0x60`) and logs set-1 scancodes (supports `0xE0` prefix).
  - Verification: kernel/moon-kernel build+multiboot checks and serial boot smoke tests.
  - Pending interactive check: verify live keypress scancode logs in a non-headless QEMU run.
- [x] Step 8: Expose minimal MoonBit-facing interrupt API (ticks + keyboard event polling/queue).
  - Added C-facing API exports in `runtime/moon_kernel_ffi.c`:
    - `moon_kernel_get_ticks()` (from PIT tick counter)
    - `moon_kernel_keyboard_pop_event()` (keyboard queue pop)
  - Added keyboard IRQ queue in `arch/x86/keyboard.c` and non-blocking `keyboard_pop_event()` API.
  - Queue dequeue return type polished to explicit `int32_t` for fixed-width ABI clarity.
  - Added MoonBit extern bindings in `moon_kernel.mbt` and integrated tick/event polling in `moon_kernel_entry()`.
  - Enabled interrupts before MoonBit `main` execution in `kernel/moon_entry.c` so MoonBit polling sees live IRQ state.
  - Documented that MoonBit `main` is interrupt-preemptible in current design; future critical sections should manage IRQ state explicitly.
  - Verification: `moon check --target native`, kernel/moon-kernel build+multiboot checks, serial boot smoke tests.
- [x] Step 9: Run Phase 2 verification matrix (fault path + timer + keyboard + regression boot checks).
  - Build/regression checks:
    - `moon check --target native`: OK.
    - `make kernel.elf && make check-kernel`: OK (`Multiboot header: OK`).
    - `make moon-kernel.elf && make check-moon-kernel`: OK (`MoonBit kernel multiboot header: OK`).
    - `make boot_512.img`: OK (legacy boot-sector image still builds).
  - Runtime serial checks:
    - `timeout 6s make run-kernel-serial`: boot diagnostics + repeated `[pit] heartbeat`.
    - `timeout 6s make run-moon-kernel-serial`: MoonBit entry logs + repeated `[pit] heartbeat`.
  - Keyboard IRQ checks:
    - Injected synthetic keys via QEMU monitor (`sendkey a`, `sendkey ret`) while logging serial to file.
    - Observed `[kbd] scancode=... press/release` logs on the MoonBit kernel path.
    - Explicitly closes Step 7 pending keyboard verification item (keypress-to-IRQ log path confirmed in Step 9).
  - Fault-path checks:
    - Added compile-time self-test hook in `kernel/main.c` guarded by `PHASE2_FAULT_TEST_INT3`.
    - Built with fault define and ran QEMU; observed deterministic panic dump:
      - `[isr] PANIC exception vector=0x00000003 ...`
      - register dump line + halt loop behavior.
    - Rebuilt without the define and re-ran regression/runtime checks to confirm normal behavior.
- [x] Step 10: Update docs (`README.md`, `README_JA.md`, `docs/README.md`, roadmap/report status) for Phase 2 progress.
  - Updated top-level status summaries in `README.md` and `README_JA.md` to reflect Phase 2 Step 9 completion.
  - Added Step 9 verification command set (including optional `PHASE2_FAULT_TEST_INT3` fault-path self-test) to both top-level READMEs.
  - Updated `docs/README.md` current status to “Phase 2 Steps 2-9 completed.”
  - Updated roadmap/report status sections:
    - `docs/ROADMAP_EN.md`, `docs/ROADMAP.md`
    - `docs/REPORT_EN.md`, `docs/REPORT_JA.md`

## Phase 3: メモリ管理（Step 5）

仕様書: [docs/SPEC_PHASE3_MEMORY.md](docs/SPEC_PHASE3_MEMORY.md)
（Step 番号は仕様書の Section 10 に準拠。仕様書が更新された場合はここも追従させること。）

- [ ] Step 3-1: linker.ld に __kernel_end シンボル追加
- [ ] Step 3-2: kernel/multiboot.h + multiboot.c 実装（メモリマップ解析）
- [ ] Step 3-3: kernel/pmm.h + pmm.c 実装（ビットマップ物理ページアロケータ）
- [ ] Step 3-4: kernel/paging.h + paging.c 実装（恒等マッピング + CR0.PG）
- [ ] Step 3-5: ページフォルトハンドラ（ベクタ 14 で CR2 出力）
- [ ] Step 3-6: runtime/heap.h + heap.c 実装（free-list アロケータ）
- [ ] Step 3-7: kernel/moon_entry.c に Phase 3 初期化統合
- [ ] Step 3-8: Makefile 更新 + 全ビルドパス回帰
- [ ] Step 3-9: 全検証マトリクス + ドキュメント同期
