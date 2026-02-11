# Toy OS (日本語)

[EN](README.md) | [日本語](README_JA.md)

最小構成の x86 ベアメタルブートローダープロジェクトです。  
現在の状態:
- ブートセクタ経路: `boot.s` で 16-bit 起動から 32-bit protected mode へ遷移。
- Phase 0 カーネル経路: Multiboot + フリースタンディング C カーネルのビルドが可能。
- Phase 1 カーネル経路: MoonBit 生成コードのカーネル経路が起動し、COM1 シリアルにログ出力可能。
- Phase 2 割り込み基盤: 完了（Step 1-10。実装は Step 9 まで、Step 10 はドキュメント同期）。

## クイックスタート

```sh
make        # boot_512.img をビルドして QEMU で起動
make run    # 既存の boot_512.img を QEMU で起動
make clean  # 生成ファイル (*.o, *.elf, *.img) を削除
```

## Phase 0 C カーネル経路

```sh
make kernel.elf                         # Multiboot カーネル ELF をビルド（host gcc/as の 32bit）
make check-kernel                       # grub-file があれば Multiboot ヘッダを検証
make run-kernel                         # QEMU で kernel.elf を起動
make run-kernel-serial                  # ヘッドレス実行 + COM1 シリアルを端末へ出力
make KERNEL_CROSS=i686-elf- kernel.elf # 任意: クロスツールチェーンを使用
```

## Phase 1 MoonBit カーネル経路

```sh
make moon-gen                           # cmd/moon_kernel から MoonBit の native C を生成
make moon-kernel.elf                    # フリースタンディング MoonBit カーネル ELF をビルド
make check-moon-kernel                  # Multiboot ヘッダ検証
make run-moon-kernel-serial             # ヘッドレス実行 + シリアル出力
```

## Phase 2 検証（Step 9）

```sh
moon check --target native
make kernel.elf && make check-kernel
make moon-kernel.elf && make check-moon-kernel
timeout 6s make run-kernel-serial
timeout 6s make run-moon-kernel-serial
```

例外経路セルフテスト（任意・コンパイル時のみ有効）:

```sh
make clean-kernel
make kernel.elf KCFLAGS='-m32 -std=gnu11 -ffreestanding -O2 -Wall -Wextra -fno-stack-protector -fno-pie -fno-asynchronous-unwind-tables -fno-unwind-tables -MMD -MP -I. -DPHASE2_FAULT_TEST_INT3'
timeout 6s qemu-system-i386 -kernel kernel.elf -serial stdio -display none -monitor none
```

## ドライバ・カーネルメモ

- VGA ドライバ (`drivers/vga.c`) は RAM 上のシャドウバッファを使用。1文字書込みのみ VRAM に直接反映し、スクロール・クリアは一括フラッシュ。
- 共有 hex フォーマッタ (`kernel/fmt.c`) が `put_hex32()` を関数ポインタ経由で提供し、VGA / シリアル双方で利用。
- IDT 基盤 (`arch/x86/idt.c`) で 256 エントリ、`idt_set_interrupt_gate()`、`idt_load()`（`lidt`）を提供。
- `kernel/main.c` に、例外経路を決定的に検証するためのガード付きセルフテストフック（`PHASE2_FAULT_TEST_INT3`）を追加。

## ランタイムメモ

- `runtime/runtime_stubs.c` で `malloc` / `calloc` のオーバーフロー安全チェックを実装。
- `realloc` は既存データを保持する動作に修正済み。
- `free` は現状 no-op（バンプアロケータ前提）で、初期段階のマイルストーン向け実装。

## ドキュメント

- [ドキュメント索引](docs/README.md): 推奨読書順
- [プロテクトモード移行チュートリアル](docs/tutorial-01-protected-mode.md): 16-bit から 32-bit への実装手順
- [詳細ロードマップ（正本・日本語）](docs/ROADMAP.md)
- [ロードマップ英語要約](docs/ROADMAP_EN.md)
- [詳細技術レポート（正本・日本語）](docs/REPORT_JA.md)
- [レポート英語要約](docs/REPORT_EN.md)
