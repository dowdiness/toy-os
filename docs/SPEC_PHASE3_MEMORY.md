# Phase 3: メモリ管理 実装仕様書（Rev.2）

> Phase 2（割り込み基盤）完了後に着手。Phase 4（スケジューラ）・Phase 5a（capability syscall）の前提。
> この文書だけでコーディングエージェントが実装を完了できることを目標とする。

## この文書の位置づけ

- **正本**: Phase 3 の全タスクの仕様・コード構造・検証基準
- **前提**: Phase 2 Step 10 まで完了済み（IDT/ISR/PIC/PIT/keyboard 動作確認済み）
- **後続**: Phase 4（プロセス管理）、Phase 5a（capability syscall）

## ゴール

物理メモリ管理 + ページング + free が動くヒープアロケータ。
Phase 完了時に以下が全て成立すること:

1. Multiboot メモリマップを解析し、利用可能な RAM 領域を認識する
2. 物理ページフレーム（4 KiB 単位）をビットマップで管理し、alloc/free できる
3. 2 レベルページテーブルでカーネル空間を恒等マッピングし、CR3 に載せて動作する
4. free-list ヒープアロケータが `malloc`/`free` を正しく処理する（MoonBit Perceus RC 対応）
5. Phase 2 の全機能（タイマー、キーボード、シリアル出力）が引き続き動作する

### 受け入れテスト（最小判定基準）

Phase 3 完了の判定は以下のシリアルログで行う。全て満たすこと:

1. `[mmap] entries: ...` が出力される（0 ならエラー理由が出力される）
2. `[pmm] total=..., free=...` が出力される（いずれも 0 は禁止）
3. `[paging] enabled` が出力され、**その後も** `[pit] heartbeat` が継続する
4. `[heap-test]` の全項目が `OK`（`FAIL` が 0 件）
5. MoonBit パス（`moon-kernel.elf`）が Phase 2 と同等に起動し、クラッシュしない

---

## 0. 現状の把握（エージェント向け）

以下のファイルを最初に読み、現在の構造を理解すること。

| ファイル | 読む理由 |
|---------|---------|
| `linker.ld` | カーネルのロードアドレス（1 MiB）とセクション配置 |
| `arch/x86/multiboot_boot.s` | Multiboot ヘッダのフラグと `kernel_main` への引数渡し（EAX=magic, EBX=info） |
| `kernel/main.c` | C カーネルのエントリ。`multiboot_magic` と `multiboot_info_addr` を受け取るが現在未使用 |
| `kernel/moon_entry.c` | MoonBit カーネルのエントリ。同上（`(void)multiboot_info_addr`） |
| `runtime/runtime_stubs.c` | 現在のバンプアロケータ（`free` が no-op）。Phase 3 で置換対象 |
| `arch/x86/idt.c` | 既存コードのスタイル参照（packed struct、static 配列、init 関数パターン） |
| `Makefile` | ビルドルールの追加パターン（`KERNEL_OBJS` への追加方法） |

### 重要な既存の制約

- **Multiboot ヘッダのフラグ** は `(1<<0) | (1<<1)` が設定済み（`multiboot_boot.s` 行 4）。
  ビット 0 はページ境界アライン要求、ビット 1 は `mem_lower`/`mem_upper` フィールドの
  提供を期待するヒント。**ただしビット 1 はメモリマップ（mmap）の提供を保証しない。**
  mmap の有無は `multiboot_info->flags` のビット 6 (`MULTIBOOT_FLAG_MMAP`) で
  必ず実行時に判定すること。QEMU の `-kernel` 起動では通常 mmap が提供されるが、
  仕様上は保証ではない。
- **カーネルは 1 MiB にロード**される（`linker.ld` 行 4: `. = 1M`）。
- **静的ヒープは BSS に 4 MiB**（`runtime_stubs.c` 行 6: `#define HEAP_SIZE`）。
  Phase 3 でヒープアロケータを置換した後、この静的バッファは不要になる。
- **スタックは BSS に 16 KiB**（`multiboot_boot.s` 行 16: `.skip 16384`）。
- **GDT のカーネルコードセレクタは `0x08`**（`idt.c` 行 5）。
  現時点で GDT は boot.s 側のフラットモデル設定のみ。明示的な `gdt.c` は未実装。

---

## 1. メモリレイアウト（設計決定）

Phase 3 完了後の物理メモリレイアウト:

```
0x00000000 ┌─────────────────────────┐
           │ Real-mode IVT / BDA     │
           │ 低メモリ全域             │  ← 全て予約（単純化のため Phase 3 では使わない）
0x000A0000 ├─────────────────────────┤
           │ VGA / ROM / BIOS        │  予約
0x000B8000 │   VGA テキストバッファ    │  恒等マッピング対象
0x00100000 ├─────────────────────────┤  ← カーネルロードアドレス (1 MiB)
           │ .text .rodata .data     │
           │ .bss (スタック + 旧ヒープ)│
 __kernel_end ├──────────────────────┤  ← リンカシンボル
           │ 物理ページビットマップ    │  ← __kernel_end の直後に配置
           ├─────────────────────────┤
 bitmap_end│ ページディレクトリ (4K)   │
           │ ページテーブル群          │
           ├─────────────────────────┤
           │ ヒープ領域 (連続物理)     │  ← pmm_alloc_contiguous で確保
           ├─────────────────────────┤
           │ フリー物理メモリ          │  ← ビットマップで管理
           │         ...              │
 ram_top    └─────────────────────────┘
```

**予約ルール（Phase 3）:**
- `0x00000000` ~ `0x000FFFFF`（低メモリ 1 MiB）: 全て予約。IVT, BDA, VGA, BIOS ROM を含む。Phase 3 ではこの領域のページを一切割り当てない。
- `0x00100000` ~ `bitmap_end`（カーネル本体 + ビットマップ）: 予約。
- `bitmap_end` 以降の available 領域: PMM で管理対象。

### 仮想アドレス空間（Phase 3 時点）

Phase 3 ではカーネルのみが動作する（ユーザー空間は Phase 5）。
**全物理メモリを恒等マッピング（virt == phys）** する。

```
0x00000000 ~ ram_top    : 恒等マッピング（カーネル空間）
0xFFC00000 ~ 0xFFFFFFFF : 再帰マッピング（ページテーブル自己参照）
```

恒等マッピングの上限: `min(ram_top, 256 MiB)` を推奨。QEMU のデフォルト RAM が
128 MiB なので通常は問題ないが、大容量 RAM 設定時にページテーブル消費を抑えるため。
実際にマッピングした上限はシリアルに出力すること。

再帰マッピング: ページディレクトリの最後のエントリ（インデックス 1023）が
ページディレクトリ自身を指す。これにより、ページテーブルの内容を
`0xFFC00000 + table_index * 4096` で読み書きできる。

---

## 2. linker.ld の変更

`__kernel_end` シンボルをエクスポートし、物理メモリマネージャが
カーネル直後のアドレスを知れるようにする。

```ld
/* linker.ld — Phase 3 差分 */
ENTRY(_start)

SECTIONS {
    . = 1M;

    .text BLOCK(4K) : ALIGN(4K) {
        *(.multiboot)
        *(.text)
    }

    .rodata BLOCK(4K) : ALIGN(4K) {
        *(.rodata)
    }

    .data BLOCK(4K) : ALIGN(4K) {
        *(.data)
    }

    .bss BLOCK(4K) : ALIGN(4K) {
        *(COMMON)
        *(.bss)
    }

    /* Phase 3: カーネル終端シンボル（4K アライン） */
    . = ALIGN(4K);
    __kernel_end = .;
}
```

**変更点**: 最後に `. = ALIGN(4K); __kernel_end = .;` を追加するだけ。
既存のセクション配置は変更しない。

---

## 3. Multiboot メモリマップ解析

### 3.1 Multiboot 情報構造体の定義

```c
/* kernel/multiboot.h */
#ifndef KERNEL_MULTIBOOT_H
#define KERNEL_MULTIBOOT_H

#include <stdint.h>

#define MULTIBOOT_MAGIC 0x2BADB002u

/* multiboot_info.flags のビット */
#define MULTIBOOT_FLAG_MEM      (1u << 0)  /* mem_lower / mem_upper 有効 */
#define MULTIBOOT_FLAG_MMAP     (1u << 6)  /* mmap_length / mmap_addr 有効 */

struct multiboot_info {
    uint32_t flags;
    uint32_t mem_lower;        /* KB単位、flags ビット0有効時 */
    uint32_t mem_upper;        /* KB単位、flags ビット0有効時 */
    uint32_t boot_device;
    uint32_t cmdline;
    uint32_t mods_count;
    uint32_t mods_addr;
    uint32_t syms[4];
    uint32_t mmap_length;      /* メモリマップの合計バイト数 */
    uint32_t mmap_addr;        /* メモリマップ配列の物理アドレス */
    /* 以降のフィールドは Phase 3 では使わない */
} __attribute__((packed));

/* メモリマップの各エントリ */
struct multiboot_mmap_entry {
    uint32_t size;             /* この構造体のサイズ - 4 (size フィールド自体を除く) */
    uint64_t addr;             /* 領域の開始物理アドレス */
    uint64_t len;              /* 領域のバイト長 */
    uint32_t type;             /* 1 = 利用可能 RAM, それ以外 = 予約 */
} __attribute__((packed));

#define MULTIBOOT_MMAP_TYPE_AVAILABLE 1u

#endif
```

### 3.2 メモリマップ走査関数

```c
/* kernel/multiboot.c */
#include "kernel/multiboot.h"
#include "drivers/serial.h"
#include "kernel/fmt.h"

/*
 * メモリマップを走査し、コールバックを呼ぶ。
 * コールバック: fn(base_addr, length, is_available)
 * 戻り値: 走査したエントリ数。flags に MMAP ビットがなければ 0。
 */
uint32_t multiboot_scan_mmap(
    const struct multiboot_info *info,
    void (*callback)(uint32_t base, uint32_t length, int available))
{
    uint32_t offset;
    uint32_t count;
    const struct multiboot_mmap_entry *entry;

    if ((info->flags & MULTIBOOT_FLAG_MMAP) == 0) {
        serial_puts("[mmap] ERROR: no memory map from bootloader\n");
        serial_puts("[mmap] info->flags = ");
        put_hex32(info->flags, serial_puts, serial_putchar);
        serial_puts("\n");
        return 0;
    }

    offset = 0;
    count = 0;

    while (offset < info->mmap_length) {
        /* 防御チェック: entry の size フィールドすら読めない場合は打ち切り */
        if (offset + 4u > info->mmap_length) {
            break;
        }

        entry = (const struct multiboot_mmap_entry *)(uintptr_t)(info->mmap_addr + offset);

        /* 防御チェック: size が 0 なら不正データとして打ち切り */
        if (entry->size == 0) {
            serial_puts("[mmap] WARN: entry with size=0, stopping scan\n");
            break;
        }

        /* 防御チェック: エントリ全体が mmap_length 内に収まるか */
        if (offset + entry->size + 4u > info->mmap_length) {
            serial_puts("[mmap] WARN: entry overflows mmap_length, stopping scan\n");
            break;
        }

        /*
         * 32ビット OS なので 4 GiB 以上の領域は無視する。
         * addr の上位32ビットが非ゼロ、または addr+len が 0xFFFFFFFF を超える場合はスキップ。
         */
        if (entry->addr <= 0xFFFFFFFFull &&
            entry->addr + entry->len <= 0x100000000ull)
        {
            uint32_t base = (uint32_t)entry->addr;
            uint32_t length = (uint32_t)entry->len;
            int available = (entry->type == MULTIBOOT_MMAP_TYPE_AVAILABLE) ? 1 : 0;
            callback(base, length, available);
        }

        /* 次のエントリ: size フィールド(4バイト) + size の値 */
        offset += entry->size + 4u;
        count++;
    }

    return count;
}
```

### 3.3 デバッグ出力

```c
/* kernel/multiboot.c 内に追加 */
static void mmap_debug_print(uint32_t base, uint32_t length, int available) {
    serial_puts("  ");
    put_hex32(base, serial_puts, serial_putchar);
    serial_puts(" - ");
    put_hex32(base + length, serial_puts, serial_putchar);
    serial_puts(available ? " [available]" : " [reserved]");
    serial_puts("\n");
}

void multiboot_dump_mmap(const struct multiboot_info *info) {
    serial_puts("[mmap] Memory map:\n");
    uint32_t count = multiboot_scan_mmap(info, mmap_debug_print);
    serial_puts("[mmap] entries: ");
    put_hex32(count, serial_puts, serial_putchar);
    serial_puts("\n");
}
```

---

## 4. 物理ページフレームアロケータ（ビットマップ）

### 4.1 設計

- ページサイズ: 4 KiB (4096 バイト)
- 管理単位: 1ビット = 1ページ（0 = 空き、1 = 使用中）
- ビットマップは `__kernel_end` の直後に配置
- 最大対応 RAM: 4 GiB（ビットマップサイズ = 128 KiB = 1048576 ページ / 8）
- 実際のビットマップサイズは検出した RAM 量に応じて決定

### 4.2 データ構造とインターフェース

```c
/* kernel/pmm.h */
#ifndef KERNEL_PMM_H
#define KERNEL_PMM_H

#include <stdint.h>

struct multiboot_info;  /* forward declaration */

#define PMM_PAGE_SIZE 4096u

/*
 * 初期化。kernel_end はリンカシンボル __kernel_end の値。
 * multiboot_info からメモリマップを読み、ビットマップを配置する。
 * 戻り値: 検出した RAM の最上位アドレス (ram_top)。0 なら初期化失敗。
 */
uint32_t pmm_init(uint32_t kernel_end, const struct multiboot_info *info);

/*
 * 1ページ（4 KiB）の物理アドレスを確保。失敗時は 0 を返す。
 * 返すアドレスは PMM_PAGE_SIZE アライン済み。
 */
uint32_t pmm_alloc_page(void);

/* 物理アドレス addr のページを解放する。addr は pmm_alloc_page で取得したもの。 */
void pmm_free_page(uint32_t addr);

/*
 * count ページの物理的に連続した領域を確保する。
 * 返すアドレスは PMM_PAGE_SIZE アライン済み。失敗時は 0。
 * ヒープの初期領域確保に使う。
 */
uint32_t pmm_alloc_contiguous(uint32_t count);

/* 統計取得 */
uint32_t pmm_total_pages(void);
uint32_t pmm_free_pages(void);

/* デバッグ: 統計をシリアルに出力 */
void pmm_dump_stats(void);

#endif
```

**API 設計の根拠:**
- `pmm_init` は `ram_top` を返す。`paging_init` が恒等マッピングの上限として必要とするため。
- 統計情報（total/free）は別関数で取得する。初期化の成否判定は `ram_top == 0` で行う。
- `pmm_alloc_contiguous` は `heap_init` の前提（連続物理メモリ）を満たすために必須。

### 4.3 実装の要点

```c
/* kernel/pmm.c */
#include "kernel/pmm.h"
#include "kernel/multiboot.h"
#include "drivers/serial.h"
#include "kernel/fmt.h"

static uint32_t *bitmap;          /* ビットマップ配列の先頭アドレス */
static uint32_t bitmap_size;      /* ビットマップの uint32_t 要素数 */
static uint32_t total_page_count; /* 管理対象の総ページ数 */
static uint32_t free_page_count;  /* 現在の空きページ数 */

/* ページをアドレスからビットマップインデックスに変換 */
static inline uint32_t page_to_index(uint32_t addr) {
    return addr / PMM_PAGE_SIZE;
}

/* ビットを立てる（使用中にマーク） */
static inline void bitmap_set(uint32_t page_index) {
    bitmap[page_index / 32u] |= (1u << (page_index % 32u));
}

/* ビットを落とす（空きにマーク） */
static inline void bitmap_clear(uint32_t page_index) {
    bitmap[page_index / 32u] &= ~(1u << (page_index % 32u));
}

/* ビットを読む（0 = 空き、非0 = 使用中） */
static inline int bitmap_test(uint32_t page_index) {
    return (bitmap[page_index / 32u] >> (page_index % 32u)) & 1u;
}

/*
 * 指定範囲のページを使用中にマークする。
 * start_addr, end_addr は PMM_PAGE_SIZE アライン不要（切り上げ/切り下げで処理）。
 */
static void pmm_mark_reserved(uint32_t start_addr, uint32_t end_addr) {
    uint32_t start_page = start_addr / PMM_PAGE_SIZE;
    uint32_t end_page = (end_addr + PMM_PAGE_SIZE - 1u) / PMM_PAGE_SIZE;
    uint32_t i;

    if (end_page > total_page_count) end_page = total_page_count;
    for (i = start_page; i < end_page; ++i) {
        if (!bitmap_test(i)) {
            bitmap_set(i);
            if (free_page_count > 0) free_page_count--;
        }
    }
}

/*
 * 指定範囲のページを空きにマークする。
 */
static void pmm_mark_free(uint32_t start_addr, uint32_t end_addr) {
    uint32_t start_page = start_addr / PMM_PAGE_SIZE;
    uint32_t end_page = (end_addr + PMM_PAGE_SIZE - 1u) / PMM_PAGE_SIZE;
    uint32_t i;

    if (end_page > total_page_count) end_page = total_page_count;
    for (i = start_page; i < end_page; ++i) {
        if (bitmap_test(i)) {
            bitmap_clear(i);
            free_page_count++;
        }
    }
}
```

### 4.4 `pmm_init` の処理手順

1. Multiboot メモリマップを走査し、`ram_top`（利用可能 RAM の最上位アドレス）を決定
2. `total_page_count = ram_top / PMM_PAGE_SIZE` を計算
3. `bitmap_size = (total_page_count + 31) / 32` を計算
4. `bitmap = (uint32_t *)kernel_end` に配置
5. ビットマップ全体を `0xFF` で初期化（**全ページを「使用中」にマーク**）
6. メモリマップの `available` 領域だけを `pmm_mark_free` で「空き」にマーク
7. 以下の領域を `pmm_mark_reserved` で「使用中」に再マーク（予約領域の保護）:
   - `0x00000000` ~ `0x000FFFFF`（低メモリ 1 MiB 全体。IVT, BDA, VGA, BIOS ROM を含む）
   - `0x00100000` ~ `bitmap_end`（カーネル本体 + ビットマップ自体）
8. `free_page_count` を集計
9. `ram_top` を返す

`bitmap_end` の計算:
```c
uint32_t bitmap_bytes = bitmap_size * sizeof(uint32_t);
uint32_t bitmap_end = kernel_end + bitmap_bytes;
/* 4K アライン */
bitmap_end = (bitmap_end + PMM_PAGE_SIZE - 1u) & ~(PMM_PAGE_SIZE - 1u);
```

### 4.5 `pmm_alloc_page` の実装方針

線形スキャン（first-fit）。ビットマップ配列を先頭からスキャンし、
0 のビットを見つけたらそのページのアドレスを返す。

最適化: `bitmap[i] == 0xFFFFFFFF` で32ページ分を一括スキップ。

```c
uint32_t pmm_alloc_page(void) {
    uint32_t i, bit;

    for (i = 0; i < bitmap_size; ++i) {
        if (bitmap[i] == 0xFFFFFFFFu) continue;  /* 全ビット使用中 */
        for (bit = 0; bit < 32u; ++bit) {
            uint32_t page_index = i * 32u + bit;
            if (page_index >= total_page_count) return 0;
            if (!bitmap_test(page_index)) {
                bitmap_set(page_index);
                free_page_count--;
                return page_index * PMM_PAGE_SIZE;
            }
        }
    }
    return 0;  /* メモリ不足 */
}
```

### 4.6 `pmm_alloc_contiguous` の実装方針

ビットマップを走査し、`count` 個の連続した空きビットを見つける。

```c
uint32_t pmm_alloc_contiguous(uint32_t count) {
    uint32_t run_start = 0;
    uint32_t run_length = 0;
    uint32_t i;

    for (i = 0; i < total_page_count; ++i) {
        if (bitmap_test(i)) {
            run_length = 0;
            run_start = i + 1;
        } else {
            run_length++;
            if (run_length == count) {
                /* 連続領域を確保 */
                uint32_t j;
                for (j = run_start; j < run_start + count; ++j) {
                    bitmap_set(j);
                    free_page_count--;
                }
                return run_start * PMM_PAGE_SIZE;
            }
        }
    }
    return 0;  /* 連続領域が見つからない */
}
```

### 4.7 統計とデバッグ

```c
uint32_t pmm_total_pages(void) { return total_page_count; }
uint32_t pmm_free_pages(void)  { return free_page_count; }

void pmm_dump_stats(void) {
    serial_puts("[pmm] total=");
    put_hex32(total_page_count, serial_puts, serial_putchar);
    serial_puts(", free=");
    put_hex32(free_page_count, serial_puts, serial_putchar);
    serial_puts("\n");
}
```

---

## 5. ページング（2 レベルページテーブル）

### 5.1 x86 ページング概要（実装に必要な知識）

- **ページディレクトリ (PD)**: 1024 エントリ × 4 バイト = 4 KiB。各エントリがページテーブルを指す。
- **ページテーブル (PT)**: 1024 エントリ × 4 バイト = 4 KiB。各エントリが物理ページを指す。
- **仮想アドレスの分解**: `[PD index: 10ビット][PT index: 10ビット][offset: 12ビット]`
- **CR3 レジスタ**: ページディレクトリの物理アドレスを保持

### 5.2 エントリフラグ

```c
/* kernel/paging.h */
#ifndef KERNEL_PAGING_H
#define KERNEL_PAGING_H

#include <stdint.h>

#define PAGE_SIZE         4096u

/* ページディレクトリ / ページテーブル エントリのフラグ */
#define PTE_PRESENT       (1u << 0)   /* ページが存在する */
#define PTE_WRITABLE      (1u << 1)   /* 書き込み可能 */
#define PTE_USER          (1u << 2)   /* Ring 3 からアクセス可能（Phase 5 用） */
#define PTE_WRITE_THROUGH (1u << 3)
#define PTE_CACHE_DISABLE (1u << 4)
#define PTE_ACCESSED      (1u << 5)
#define PTE_DIRTY         (1u << 6)   /* PT エントリのみ */
#define PTE_PAGE_SIZE     (1u << 7)   /* PD エントリ: 4 MiB ページ（使わない） */

/* アドレスマスク（上位20ビット） */
#define PTE_ADDR_MASK     0xFFFFF000u

/* 再帰マッピング用インデックス */
#define RECURSIVE_PD_INDEX 1023u

/* 再帰マッピング経由でページテーブルにアクセスするアドレス計算 */
/* PD 自体: 0xFFFFF000 */
/* PT[i]:   0xFFC00000 + i * 4096 */
#define RECURSIVE_PD_VADDR    0xFFFFF000u
#define RECURSIVE_PT_BASE     0xFFC00000u

/*
 * 初期化: 恒等マッピングを構築し CR3 にロードする。
 * ram_top: 恒等マッピングの上限アドレス（pmm_init の返り値）。
 * 実際のマッピング上限は min(ram_top, 256 MiB) になる場合がある。
 */
void paging_init(uint32_t ram_top);

/*
 * 仮想アドレス vaddr を物理アドレス paddr にマッピングする。
 * flags: PTE_PRESENT | PTE_WRITABLE 等。
 * 必要に応じてページテーブルを pmm_alloc_page で確保する。
 * 戻り値: 0 = 成功、-1 = 失敗（メモリ不足）。
 */
int paging_map_page(uint32_t vaddr, uint32_t paddr, uint32_t flags);

/*
 * 仮想アドレス vaddr のマッピングを解除する。
 * TLB のフラッシュ（invlpg）を行う。
 */
void paging_unmap_page(uint32_t vaddr);

/*
 * CR3 を再ロードして TLB を全フラッシュする。
 */
void paging_flush_tlb(void);

#endif
```

### 5.3 アセンブリヘルパー

```c
/* kernel/paging.c 内の static インライン関数 */

static inline void cr3_write(uint32_t pd_phys) {
    __asm__ volatile("mov %0, %%cr3" : : "r"(pd_phys) : "memory");
}

static inline uint32_t cr3_read(void) {
    uint32_t val;
    __asm__ volatile("mov %%cr3, %0" : "=r"(val));
    return val;
}

static inline void paging_enable(void) {
    uint32_t cr0;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= (1u << 31);  /* PG ビット */
    __asm__ volatile("mov %0, %%cr0" : : "r"(cr0) : "memory");
}

static inline void invlpg(uint32_t vaddr) {
    __asm__ volatile("invlpg (%0)" : : "r"(vaddr) : "memory");
}
```

### 5.4 `paging_init` の処理手順

1. `pmm_alloc_page()` でページディレクトリ（4 KiB）を1ページ確保し、全て 0 クリア
2. 恒等マッピングの上限を決定: `uint32_t map_top = ram_top;`（大きすぎる場合は上限を設けてもよい）
3. 恒等マッピングを構築:
   - `0x00000000` ~ `map_top` の各 4 MiB 区間ごとにページテーブルを `pmm_alloc_page()` で確保
   - ページテーブルの各エントリに `phys_addr | PTE_PRESENT | PTE_WRITABLE` を設定
   - ページディレクトリの対応エントリに `pt_phys | PTE_PRESENT | PTE_WRITABLE` を設定
   - VGA バッファ (`0xB8000`) を含む区間も恒等マッピング対象（低メモリ区間に含まれるので自動的に対象）
4. 再帰マッピングの設定:
   ```c
   page_directory[RECURSIVE_PD_INDEX] = pd_phys | PTE_PRESENT | PTE_WRITABLE;
   ```
5. `cr3_write(pd_phys)` で CR3 にセット
6. `paging_enable()` で CR0.PG をオン
7. シリアルに `[paging] enabled, identity-mapped N MiB` を出力

**⚠️ 注意: CR0.PG をオンにした瞬間**、恒等マッピングが正しくなければ即トリプルフォルトで
QEMU がリセットする。デバッグ手順:

1. まず CR3 にセットするだけで PG をオンにしない状態でビルド・ブートが通ることを確認
2. 次に PG をオンにして、heartbeat が出るか確認
3. heartbeat が出なければ、ページテーブルのダンプ（CR3 ロード前に PD/PT の内容をシリアル出力）で調査

### 5.5 ページフォルトハンドラ

Phase 2 の割り込みディスパッチャに接続する。CPU 例外ベクタ 14 がページフォルト。
Phase 3 では情報を出力して halt する（Phase 5 でユーザー空間導入後に graceful handling に拡張）。

**変更内容**: 割り込みディスパッチの例外ハンドラ（ベクタ < 32 で panic する箇所）に、
ベクタ 14 の場合のみ CR2（フォルト発生仮想アドレス）を追加出力する。
具体的にどのファイルの何行目かは、エージェントが割り込みディスパッチのソースを読んで判断すること。

```c
/* 例外パニック処理内で、vector == 14 の場合に追加: */
static inline uint32_t cr2_read(void) {
    uint32_t val;
    __asm__ volatile("mov %%cr2, %0" : "=r"(val));
    return val;
}

/* パニック出力に追加: */
if (frame->vector == 14) {
    serial_puts(" cr2=");
    put_hex32(cr2_read(), serial_puts, serial_putchar);
}
```

---

## 6. ヒープアロケータ（free-list）

### 6.1 設計方針

- `runtime/runtime_stubs.c` の `malloc`/`free` を free-list アロケータで **置換** する
- MoonBit の Perceus RC は `free` を頻繁に呼ぶため、free が動作することが必須
- フリーリストは単方向リンクリスト。ブロックヘッダに「サイズ」と「次の空きブロック」。
- free 時に **後方コアレッシング**（次の隣接ブロックとのマージ）を行う
- **前方マージ（前のブロックとの結合）は Phase 3 では行わない**。
  単方向リストでの前方マージは O(n) で、実装の複雑さに見合わない。
  ただし、Perceus RC の頻繁な free により断片化が早期に発生する可能性がある。
  `heap_malloc` が NULL を返し始めたら、最初に疑うのは断片化。
  Phase 4 以降で双方向リストまたは segregated free list にアップグレードする。
- ヒープ領域はページングが有効になった後、`pmm_alloc_contiguous` で初期確保する

### 6.2 ブロックヘッダ

```c
/* runtime/heap.h */
#ifndef RUNTIME_HEAP_H
#define RUNTIME_HEAP_H

#include <stddef.h>
#include <stdint.h>

struct heap_block {
    uint32_t       size;    /* ヘッダを除いたペイロードのバイト数 */
    uint32_t       is_free; /* 0 = 使用中、1 = 空き */
    struct heap_block *next;  /* 次のブロック（アドレス順） */
};

#define HEAP_BLOCK_HEADER_SIZE  sizeof(struct heap_block)
#define HEAP_MIN_ALLOC          8u  /* 最小アロケーションサイズ */
#define HEAP_ALIGN              8u  /* アラインメント */

/*
 * ヒープ初期化。base_addr と size_bytes で初期ヒープ領域を設定。
 * base_addr は 8 バイトアライン済みであること。
 * size_bytes は HEAP_BLOCK_HEADER_SIZE + HEAP_MIN_ALLOC 以上であること。
 */
void heap_init(void *base_addr, uint32_t size_bytes);

void *heap_malloc(size_t size);
void  heap_free(void *ptr);
void *heap_calloc(size_t count, size_t size);
void *heap_realloc(void *ptr, size_t new_size);

/* デバッグ: フリーリストの状態をシリアルに出力 */
void heap_dump(void);

#endif
```

### 6.3 `heap_malloc` の処理手順

1. `size` を `HEAP_ALIGN` の倍数に切り上げ。`size < HEAP_MIN_ALLOC` なら `HEAP_MIN_ALLOC` にする
2. フリーリストを先頭から走査（first-fit）
3. `block->is_free == 1 && block->size >= size` のブロックを見つける
4. ブロックが十分大きい場合（`block->size >= size + HEAP_BLOCK_HEADER_SIZE + HEAP_MIN_ALLOC`）は分割:
   - 現ブロックのサイズを `size` に縮小
   - 残りの領域に新しいブロックヘッダを書き込み、`is_free = 1` でリストに挿入
5. ブロックを `is_free = 0` にマークし、`(block + 1)` をユーザーポインタとして返す
6. 見つからない場合は `NULL` を返す

### 6.4 `heap_free` の処理手順

1. `ptr` が NULL なら何もしない
2. `ptr` から `HEAP_BLOCK_HEADER_SIZE` を引いてブロックヘッダを取得
3. `is_free = 1` にマーク
4. **後方コアレッシング**: 次のブロック (`block->next`) が存在し、かつ
   物理的に隣接（`(uint8_t *)block + HEAP_BLOCK_HEADER_SIZE + block->size == (uint8_t *)block->next`）
   し、かつ `block->next->is_free == 1` なら:
   - `block->size += HEAP_BLOCK_HEADER_SIZE + block->next->size`
   - `block->next = block->next->next`

### 6.5 `heap_realloc` の処理手順

1. `ptr == NULL` なら `heap_malloc(new_size)` を返す
2. `new_size == 0` なら `heap_free(ptr)` して NULL を返す
3. ブロックヘッダから `old_size` を取得
4. `new_size <= old_size` なら（縮小）: そのまま `ptr` を返す（余りの分割はオプション）
5. `new_size > old_size` なら（拡張）: `heap_malloc(new_size)` → データコピー → `heap_free(ptr)`

### 6.6 `runtime_stubs.c` の変更

既存の `malloc`/`free`/`calloc`/`realloc` を `heap_*` 関数への委譲に置き換える。

**変更前**: バンプアロケータの実装（`heap_storage`, `heap_offset` 等）
**変更後**:

```c
/* runtime/runtime_stubs.c — Phase 3 差分 */

#include "runtime/heap.h"

/* ===== 旧バンプアロケータのコードを全て削除 =====
 * (heap_storage, heap_offset, heap_begin, heap_end, alloc_header,
 *  malloc, free, calloc, realloc の旧実装)
 */

void *malloc(size_t size)                    { return heap_malloc(size); }
void  free(void *ptr)                        { heap_free(ptr); }
void *calloc(size_t count, size_t size)      { return heap_calloc(count, size); }
void *realloc(void *ptr, size_t new_size)    { return heap_realloc(ptr, new_size); }
```

残りの関数（`memset`, `memcpy`, `memmove`, `memcmp`, `strlen`, `strcmp`,
`strncmp`, `putchar`, `write`, `abort`, `exit`）は変更なし。

### 6.7 ヒープ領域の確保

ヒープの初期領域は `pmm_alloc_contiguous` で連続物理ページを確保する。

```c
/* kernel/main.c の初期化シーケンス内 */
#define INITIAL_HEAP_PAGES  256u  /* 256 * 4K = 1 MiB */

uint32_t heap_base = pmm_alloc_contiguous(INITIAL_HEAP_PAGES);
if (heap_base == 0) {
    serial_puts("[heap] ERROR: cannot allocate contiguous heap pages\n");
    /* フォールバック無し — halt */
    return;
}
/* 恒等マッピングなので物理アドレス == 仮想アドレス */
heap_init((void *)(uintptr_t)heap_base, INITIAL_HEAP_PAGES * PMM_PAGE_SIZE);
serial_puts("[heap] initialized (1 MiB at ");
put_hex32(heap_base, serial_puts, serial_putchar);
serial_puts(")\n");
```

将来のヒープ拡張: `heap_expand` 関数を追加して `pmm_alloc_page` で追加ページを確保し、
フリーリストの末尾に連結する。これは Phase 4 以降で必要に応じて実装する。

---

## 7. 初期化シーケンスの統合

### 7.1 kernel/main.c の変更

```c
/* kernel/main.c — Phase 3 の初期化フロー */

#include "kernel/multiboot.h"
#include "kernel/pmm.h"
#include "kernel/paging.h"
#include "runtime/heap.h"

extern uint32_t __kernel_end;  /* リンカシンボル */

#define INITIAL_HEAP_PAGES 256u

void kernel_main(uint32_t multiboot_magic, uint32_t multiboot_info_addr) {
    const struct multiboot_info *mbi =
        (const struct multiboot_info *)(uintptr_t)multiboot_info_addr;

    /* ===== Phase 2 と同じ（変更なし） ===== */
    serial_init();
    serial_puts("COM1 serial initialized.\n");
    idt_init();
    serial_puts("IDT loaded (256 entries).\n");
    pic_remap(0x20u, 0x28u);
    serial_puts("PIC remapped to vectors 0x20-0x2F.\n");
    irq_baseline_masking();
    pit_init(100u);
    keyboard_init();
    serial_puts("PIT IRQ0 + Keyboard IRQ1 enabled.\n");

    /* ===== Phase 3 追加 ===== */

    /* Multiboot マジック検証 */
    if (multiboot_magic != MULTIBOOT_MAGIC) {
        serial_puts("[boot] ERROR: invalid multiboot magic\n");
        vga_puts("ERROR: Invalid multiboot magic.\n");
        return;
    }

    /* Step 1: メモリマップ解析 */
    multiboot_dump_mmap(mbi);

    /* Step 2: 物理ページアロケータ初期化 */
    uint32_t kernel_end = (uint32_t)(uintptr_t)&__kernel_end;
    uint32_t ram_top = pmm_init(kernel_end, mbi);
    if (ram_top == 0) {
        serial_puts("[pmm] ERROR: init failed\n");
        return;
    }
    pmm_dump_stats();

    /* Step 3: ページング有効化 */
    paging_init(ram_top);

    /* Step 4: ヒープアロケータ初期化 */
    uint32_t heap_base = pmm_alloc_contiguous(INITIAL_HEAP_PAGES);
    if (heap_base == 0) {
        serial_puts("[heap] ERROR: cannot allocate contiguous heap pages\n");
        return;
    }
    heap_init((void *)(uintptr_t)heap_base, INITIAL_HEAP_PAGES * PMM_PAGE_SIZE);
    serial_puts("[heap] initialized (1 MiB at ");
    put_hex32(heap_base, serial_puts, serial_putchar);
    serial_puts(")\n");

    /* Step 5 (optional): ヒープセルフテスト */
#if defined(PHASE3_HEAP_TEST)
    heap_selftest();
#endif

    /* ===== 以降は Phase 2 と同じ ===== */
    vga_clear();
    vga_puts("Kernel running with paging + heap.\n");

    enable_interrupts();
    cpu_idle_forever();
}
```

### 7.2 kernel/moon_entry.c の変更

`moon_entry.c` も同様の初期化シーケンスを追加する。
`multiboot_info_addr` は現在 `(void)` でキャストされているので、これを解除して使用する。
初期化フローは `kernel/main.c` と同一。MoonBit の `main()` 呼び出しの前に
pmm_init → paging_init → heap_init を完了させること。

---

## 8. ファイル構成と Makefile 変更

### 8.1 新規ファイル一覧

| ファイル | 内容 |
|---------|------|
| `kernel/multiboot.h` | Multiboot 構造体定義 |
| `kernel/multiboot.c` | メモリマップ走査 + デバッグ出力 |
| `kernel/pmm.h` | 物理ページアロケータ インターフェース |
| `kernel/pmm.c` | ビットマップ物理ページアロケータ |
| `kernel/paging.h` | ページング インターフェース |
| `kernel/paging.c` | 2レベルページテーブル + CR3 操作 |
| `runtime/heap.h` | free-list ヒープアロケータ インターフェース |
| `runtime/heap.c` | free-list ヒープアロケータ実装 |

### 8.2 Makefile への追加

既存の Makefile を以下のように変更する。変更箇所を明示する。

**C カーネルパス: `KERNEL_OBJS` の差分** (行 31 付近を置換)

```makefile
KERNEL_OBJS  = arch/x86/multiboot_boot.o arch/x86/isr_stubs.o arch/x86/isr_dispatch.o arch/x86/idt.o \
               arch/x86/pic.o arch/x86/pit.o arch/x86/keyboard.o \
               drivers/vga.o drivers/serial.o kernel/fmt.o \
               kernel/multiboot.o kernel/pmm.o kernel/paging.o \
               runtime/heap.o \
               kernel/main.o
```

**MoonBit カーネルパス: `MOON_KERNEL_OBJS` の差分** (行 52 付近を置換)

```makefile
MOON_KERNEL_OBJS = arch/x86/multiboot_boot.o arch/x86/isr_stubs.o arch/x86/isr_dispatch.o arch/x86/idt.o \
                   arch/x86/pic.o arch/x86/pit.o arch/x86/keyboard.o \
                   drivers/vga.o drivers/serial.o kernel/fmt.o \
                   kernel/multiboot.o kernel/pmm.o kernel/paging.o \
                   runtime/heap.o \
                   runtime/runtime_stubs.o runtime/moon_kernel_ffi.o runtime/moon_runtime.o \
                   kernel/moon_entry.o $(MOON_GEN_O)
```

**新規 `.o` のビルドルール** (Phase 0 targets セクション内、`kernel/main.o` ルールの前に追加)

```makefile
kernel/multiboot.o: kernel/multiboot.c kernel/multiboot.h
	$(KCC) $(KCFLAGS) -c $< -o $@

kernel/pmm.o: kernel/pmm.c kernel/pmm.h kernel/multiboot.h
	$(KCC) $(KCFLAGS) -c $< -o $@

kernel/paging.o: kernel/paging.c kernel/paging.h kernel/pmm.h
	$(KCC) $(KCFLAGS) -c $< -o $@

runtime/heap.o: runtime/heap.c runtime/heap.h
	$(KCC) $(KCFLAGS) -c $< -o $@
```

**clean ターゲット**: 既存の `$(KERNEL_OBJS)` と `$(MOON_KERNEL_OBJS)` 変数を
参照しているので、変数に追加した `.o` は自動的にクリーン対象になる。変更不要。

**注**: `kernel/multiboot.o` 等は C カーネルパスと MoonBit カーネルパスの両方で使用する。
`KCFLAGS` と `MOON_KCFLAGS` の差分は `-DMOONBIT_NATIVE_NO_SYS_HEADER` と
`-I$(MOON_INCLUDE_DIR)` のみで、`multiboot.c`, `pmm.c`, `paging.c`, `heap.c` は
これらに依存しない。したがって同一の `.o` を両パスで共有してよい。

---

## 9. 検証マトリクス（Phase 3 完了基準）

Phase 2 Step 9 の方式に合わせる。

### 9.1 ビルド・回帰チェック

```bash
moon check --target native
make kernel.elf && make check-kernel
make moon-kernel.elf && make check-moon-kernel
make boot_512.img  # レガシーパス回帰
```

全て成功すること。

### 9.2 メモリマップ出力チェック

```bash
timeout 6s make run-kernel-serial 2>&1 | grep '\[mmap\]'
```

期待出力（アドレスは QEMU のデフォルト RAM 128 MiB の場合）:

```
[mmap] Memory map:
  0x00000000 - 0x0009FC00 [available]
  0x0009FC00 - 0x000A0000 [reserved]
  0x000F0000 - 0x00100000 [reserved]
  0x00100000 - 0x07FE0000 [available]
  ...
[mmap] entries: 0x00000006
```

`[available]` エントリが少なくとも1つ存在し、1 MiB 以上の RAM が検出されること。

### 9.3 物理ページアロケータチェック

```bash
timeout 6s make run-kernel-serial 2>&1 | grep '\[pmm\]'
```

期待出力:

```
[pmm] total=0x0000XXXX, free=0x0000XXXX
```

`total` と `free` のいずれも 0 でないこと。

### 9.4 ページング有効化チェック

```bash
timeout 6s make run-kernel-serial 2>&1 | grep -E '\[paging\]|\[pit\]'
```

期待出力:

```
[paging] enabled, identity-mapped XX MiB
[pit] heartbeat
[pit] heartbeat
...
```

ページング有効化後にカーネルがクラッシュしないこと（タイマー heartbeat が継続出力される）。

### 9.5 ヒープ malloc/free チェック

`kernel/main.c` に以下のセルフテストを追加（`#ifdef PHASE3_HEAP_TEST` ガード）:

```c
#if defined(PHASE3_HEAP_TEST)
static void heap_selftest(void) {
    serial_puts("[heap-test] start\n");

    /* テスト 1: malloc + free + 再 malloc */
    void *p1 = malloc(128);
    if (!p1) { serial_puts("[heap-test] FAIL: malloc(128) returned NULL\n"); return; }
    serial_puts("[heap-test] malloc(128) OK: ");
    put_hex32((uint32_t)(uintptr_t)p1, serial_puts, serial_putchar);
    serial_puts("\n");

    free(p1);
    serial_puts("[heap-test] free OK\n");

    void *p2 = malloc(128);
    if (!p2) { serial_puts("[heap-test] FAIL: re-malloc returned NULL\n"); return; }
    serial_puts("[heap-test] re-malloc(128) OK: ");
    put_hex32((uint32_t)(uintptr_t)p2, serial_puts, serial_putchar);
    serial_puts("\n");

    /* テスト 2: free した領域が再利用されている（p2 <= p1 のアドレスなら再利用） */
    if ((uint32_t)(uintptr_t)p2 <= (uint32_t)(uintptr_t)p1) {
        serial_puts("[heap-test] free-list reuse: OK\n");
    } else {
        serial_puts("[heap-test] WARN: no reuse (may be OK if layout differs)\n");
    }

    /* テスト 3: calloc のゼロ初期化 */
    uint8_t *p3 = (uint8_t *)calloc(64, 1);
    if (!p3) { serial_puts("[heap-test] FAIL: calloc returned NULL\n"); return; }
    int all_zero = 1;
    for (int i = 0; i < 64; ++i) {
        if (p3[i] != 0) { all_zero = 0; break; }
    }
    serial_puts(all_zero ? "[heap-test] calloc zero-init: OK\n"
                         : "[heap-test] FAIL: calloc not zeroed\n");
    free(p3);

    /* テスト 4: realloc のデータ保持 */
    uint8_t *p4 = (uint8_t *)malloc(32);
    if (!p4) { serial_puts("[heap-test] FAIL: malloc(32) returned NULL\n"); return; }
    for (int i = 0; i < 32; ++i) p4[i] = (uint8_t)i;
    uint8_t *p5 = (uint8_t *)realloc(p4, 64);
    if (!p5) { serial_puts("[heap-test] FAIL: realloc returned NULL\n"); return; }
    int data_ok = 1;
    for (int i = 0; i < 32; ++i) {
        if (p5[i] != (uint8_t)i) { data_ok = 0; break; }
    }
    serial_puts(data_ok ? "[heap-test] realloc data preserve: OK\n"
                        : "[heap-test] FAIL: realloc data lost\n");
    free(p5);

    /* テスト 5: 大量 alloc/free (Perceus RC パターンのシミュレーション) */
    serial_puts("[heap-test] stress: 1000x malloc(64)+free ... ");
    int stress_ok = 1;
    for (int i = 0; i < 1000; ++i) {
        void *p = malloc(64);
        if (!p) { stress_ok = 0; break; }
        free(p);
    }
    serial_puts(stress_ok ? "OK\n" : "FAIL\n");

    /* テスト 6: free(NULL) が安全であること */
    free((void *)0);
    serial_puts("[heap-test] free(NULL): OK\n");

    serial_puts("[heap-test] all tests done\n");
}
#endif
```

実行コマンド:

```bash
make clean-kernel
make kernel.elf KCFLAGS='-m32 -std=gnu11 -ffreestanding -O2 -Wall -Wextra -fno-stack-protector -fno-pie -fno-asynchronous-unwind-tables -fno-unwind-tables -MMD -MP -I. -DPHASE3_HEAP_TEST'
timeout 10s qemu-system-i386 -kernel kernel.elf -serial stdio -display none -monitor none
```

全テストが `OK` を出力し、`FAIL` が 0 件であること。

### 9.6 Phase 2 回帰チェック

ページング有効化後も以下が引き続き動作すること:

- `[pit] heartbeat` がシリアルに出力される（タイマー割り込み動作）
- QEMU monitor で `sendkey a` → `[kbd] scancode=...` がシリアルに出力される（キーボード割り込み動作）
- VGA 出力が正常（恒等マッピングで `0xB8000` にアクセスできている）

### 9.7 MoonBit パス回帰チェック

```bash
timeout 6s make run-moon-kernel-serial 2>&1
```

`[moon] moon_kernel_entry start` が出力され、Phase 2 と同等の動作をすること。
特に、MoonBit の `malloc`/`free` がクラッシュしないこと（ヒープアロケータ置換の回帰確認）。

---

## 10. 実装ステップ（Phase 2 方式）

| Step | 内容 | 検証 |
|------|------|------|
| 3-1 | `linker.ld` に `__kernel_end` シンボル追加 | `make kernel.elf && make check-kernel` 成功。`nm kernel.elf \| grep __kernel_end` でシンボル確認 |
| 3-2 | `kernel/multiboot.h` + `kernel/multiboot.c` 実装。`kernel/main.c` でメモリマップをシリアル出力 | Section 9.2 合格 |
| 3-3 | `kernel/pmm.h` + `kernel/pmm.c` 実装。`pmm_init` + `pmm_alloc_page` + `pmm_free_page` + `pmm_alloc_contiguous` | Section 9.3 合格 + 単体テスト（alloc→free→re-alloc でアドレス再利用確認） |
| 3-4 | `kernel/paging.h` + `kernel/paging.c` 実装。恒等マッピング + 再帰マッピング + CR3 + CR0.PG | Section 9.4 合格（heartbeat 継続が必須） |
| 3-5 | 例外パニック処理でベクタ 14（#PF）の場合に CR2 出力を追加 | 意図的な不正アクセス（例: `*(volatile uint32_t *)0xDEAD0000 = 0;`）で panic + CR2 出力を確認 |
| 3-6 | `runtime/heap.h` + `runtime/heap.c` 実装。`runtime/runtime_stubs.c` の malloc/free を委譲に変更 | Section 9.5 の全項目 OK |
| 3-7 | `kernel/moon_entry.c` に Phase 3 初期化シーケンス統合 | Section 9.7 合格 |
| 3-8 | Makefile 更新（Section 8.2 の通り）+ 全ビルドパス回帰 | Section 9.1 の全ビルドチェック |
| 3-9 | 全検証マトリクス実行 + ドキュメント同期（README, TODO.md, docs/ROADMAP*.md の状況更新） | Section 9 の全テスト合格 |

---

## 11. 既知の設計判断と将来の変更点

### Phase 4 で変わること

- 恒等マッピングに加えて**プロセスごとのアドレス空間**が必要になる。
  `paging_init` の返すページディレクトリをプロセス単位で持つ。
- コンテキストスイッチ時に CR3 を切り替える。

### Phase 5a で変わること

- ユーザー空間のページテーブルエントリに `PTE_USER` フラグを付ける。
- カーネル空間のエントリには `PTE_USER` を付けない（Ring 3 からアクセス不可）。
- VGA バッファ (`0xB8000`) をユーザー空間からは unmap する（capability 強制）。

### ヒープの拡張

Phase 3 では初期 1 MiB の固定サイズ。将来 `heap_expand` 関数を追加して
`pmm_alloc_page` で追加ページを確保し、フリーリストの末尾に連結する。

### ヒープの断片化対策

Phase 3 のヒープは後方コアレッシングのみ。Perceus RC の alloc/free パターンに
より断片化が蓄積する可能性がある。対策の優先順:

1. ストレステストで断片化の程度を計測（`heap_dump` で空きブロック数・最大空きブロックサイズを確認）
2. 双方向リスト化して前方マージを追加（Phase 4）
3. サイズクラス別フリーリスト（segregated fit）の導入（必要に応じて）

---

## 参考

- [OSDev Wiki — Paging](https://wiki.osdev.org/Paging) — x86 2レベルページテーブルの解説
- [OSDev Wiki — Memory Map (x86)](https://wiki.osdev.org/Memory_Map_(x86)) — 物理メモリマップ
- [OSDev Wiki — Writing a memory manager](https://wiki.osdev.org/Writing_a_memory_manager) — ヒープアロケータパターン
- [Multiboot Specification](https://www.gnu.org/software/grub/manual/multiboot/multiboot.html) — メモリマップ構造体
- 本リポジトリ `runtime/runtime_stubs.c` — 置換対象のバンプアロケータ
- 本リポジトリ `arch/x86/idt.c` — コードスタイルの参照
