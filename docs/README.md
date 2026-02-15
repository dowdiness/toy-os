# Documentation Index

This repository has both deep technical docs and compact summaries.
Use this read order to minimize context size while keeping references clear.

## Current Status

- Phase 0 (C kernel path): available and validated with Multiboot checks.
- Phase 1 (MoonBit kernel path): baseline complete with serial output and validated build path.
- Phase 2 (interrupt foundations): completed (Steps 1-10; implementation through Step 9 and Step 10 docs sync).
- Phase 3 (memory management): spec complete, implementation pending.
- Phase 5 architecture: split into 5a/5b/5c; capability-based security model adopted.

## Recommended Read Order (Token-Efficient)

1. [README.md](../README.md) or [README_JA.md](../README_JA.md)
   - [README.md](../README.md): English project status and build/run commands.
   - [README_JA.md](../README_JA.md): 日本語のプロジェクト概要と実行手順。
2. [ROADMAP_EN.md](./ROADMAP_EN.md) or [ROADMAP.md](./ROADMAP.md)
   - Use [ROADMAP_EN.md](./ROADMAP_EN.md) for a compact English plan.
   - Use [ROADMAP.md](./ROADMAP.md) for full detailed Japanese planning notes.
3. Phase implementation specs (when working on a specific phase):
   - [SPEC_PHASE3_MEMORY.md](./SPEC_PHASE3_MEMORY.md): Phase 3 memory management.
   - [SPEC_PHASE5A_CAPABILITY_SYSCALL.md](./SPEC_PHASE5A_CAPABILITY_SYSCALL.md): Phase 5a capability syscall.
4. [REPORT_EN.md](./REPORT_EN.md) or [REPORT_JA.md](./REPORT_JA.md)
   - Use summary (`_EN`) first, then deep report (`_JA`) only when needed.
5. [tutorial-01-protected-mode.md](./tutorial-01-protected-mode.md)
   - Focused implementation tutorial for protected mode transition.

## Canonical vs Companion Docs

- Canonical deep roadmap: [ROADMAP.md](./ROADMAP.md)
- English roadmap companion: [ROADMAP_EN.md](./ROADMAP_EN.md)
- Canonical deep report: [REPORT_JA.md](./REPORT_JA.md)
- English report companion: [REPORT_EN.md](./REPORT_EN.md)

The companion docs intentionally avoid duplicating full content.
They summarize and point back to canonical deep docs.

## Scope Guide

- Need implementation sequence and milestones: roadmap docs.
- Need design rationale and research context: report docs.
- Need low-level boot transition details: tutorial doc.
