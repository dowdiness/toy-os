# Building a Toy OS with MoonBit C Backend (English Companion)

This file is a compact English companion to the canonical deep report:
- Canonical detailed report (JA): [REPORT_JA.md](./REPORT_JA.md)
- Roadmap: [ROADMAP.md](./ROADMAP.md) ([ROADMAP_EN.md](./ROADMAP_EN.md) summary)
- Protected mode tutorial: [tutorial-01-protected-mode.md](./tutorial-01-protected-mode.md)
- Documentation index/read order: [README.md](./README.md)

## Executive Summary

MoonBit's native backend can be used for bare-metal x86 development because it emits C and relies on reference counting (Perceus style), not a tracing garbage collector. In practice, this means the runtime surface is manageable for OS work, but you must supply freestanding runtime dependencies yourself.

## Status Delta (February 11, 2026)

- Implemented: Multiboot C kernel path (`kernel.elf`) with serial diagnostics.
- Implemented: initial MoonBit kernel path (`moon-kernel.elf`) booting and emitting serial logs.
- Implemented: allocator hardening in `runtime/runtime_stubs.c`
  (`malloc` overflow guard, `calloc` multiplication overflow guard, `realloc` content preservation).
- Implemented: MoonBit FFI `Bytes` ownership warnings resolved in `moon_kernel.mbt`
  with `#borrow` annotations on C extern bindings.

## Key Findings

1. MoonBit -> C generation fits a two-stage kernel build pipeline.
2. The runtime model is OS-friendlier than tracing GC designs.
3. Main integration risk is freestanding compatibility of runtime symbols/headers.
4. Early serial output and strict milestone progression reduce debugging cost.

## Minimal Pipeline

1. MoonBit source -> `moon build --target native` -> generated C.
2. Cross-compile generated C + boot assembly + runtime stubs.
3. Link with freestanding flags into `kernel.elf`.
4. Run with QEMU (`-kernel`) and validate boot diagnostics.

## What To Read Next

- For concrete implementation tasks and phase details: [ROADMAP.md](./ROADMAP.md).
- For low-level mode-switch boot mechanics: [tutorial-01-protected-mode.md](./tutorial-01-protected-mode.md).
- For full technical context and rationale: [REPORT_JA.md](./REPORT_JA.md).
