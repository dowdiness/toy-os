# MoonBit Toy OS — Implementation Roadmap

> MoonBitのCバックエンドを使用してベアメタルx86カーネルを構築するための実装計画
> ターゲット: QEMU (qemu-system-i386) → 将来的に実機 (中古Thin Client等)

## この文書の位置づけ

- 本ファイルは「詳細版ロードマップ（正本）」です。
- 英語の要約版は [ROADMAP_EN.md](./ROADMAP_EN.md) を参照してください。
- 背景調査・技術的な根拠は [REPORT_JA.md](./REPORT_JA.md)（要約: [REPORT_EN.md](./REPORT_EN.md)）を参照してください。
- ブートシーケンスの実装手順は [tutorial-01-protected-mode.md](./tutorial-01-protected-mode.md) を参照してください。
- 文書全体の読み順は [README.md](./README.md) を参照してください。

## 実装状況（2026-02-15）

- 完了: ブートセクタ経路（16-bit -> 32-bit protected mode 遷移）。
- 完了: Phase 0 Cカーネル経路（`kernel.elf`）と Multiboot 検証。
- 完了: 初期シリアルデバッグ経路（`run-kernel-serial`）。
- 完了: Phase 1 の初期MoonBit経路（`moon-kernel.elf`）の起動とシリアル出力。
- 完了: `runtime/runtime_stubs.c` のアロケータ堅牢化
  （`malloc` オーバーフロー対策、`calloc` 乗算オーバーフロー対策、`realloc` のデータ保持）。
- 完了: MoonBit FFI の `Bytes` 所有権アノテーション警告を
  `moon_kernel.mbt` の `#borrow` で解消。
- 完了: VGA ドライバにシャドウバッファを導入し、スクロール時の MMIO 負荷を軽減。
- 完了: `put_hex32()` 共有フォーマッタを `kernel/fmt.c` に分離（DRY 改善）。
- 完了: QEMU ブートセクタ起動の不要な警告・エラーメッセージを解消。
- 完了: `Makefile` の MoonBit ビルド依存関係を `moon.pkg` ベースに修正。
- 完了: Phase 2 割り込み基盤（Step 1-10）
  （実装本体は Step 9 まで: IDT/ISR/PIC/PIT/keyboard 配線、MoonBit ポーリング API、例外セルフテストを含む検証マトリクス。Step 10 はドキュメント同期）を完了。
- 計画確定: Phase 3 メモリ管理の詳細仕様書を作成（[SPEC_PHASE3_MEMORY.md](./SPEC_PHASE3_MEMORY.md)）
- 計画確定: Phase 5 を 5a（capability syscall + ELF）/ 5b（Wasm）/ 5c（structured concurrency）に分割
- 計画確定: Phase 5a capability syscall の詳細仕様書を作成（[SPEC_PHASE5A_CAPABILITY_SYSCALL.md](./SPEC_PHASE5A_CAPABILITY_SYSCALL.md)）
- 設計決定: capability ベースのセキュリティモデルを採用（グローバル名前空間排除）

---

## アーキテクチャ概要

```
MoonBit (.mbt)
    ↓ moon build --target native
C source (.c)
    ↓ i686-elf-gcc -ffreestanding -nostdlib
Object files (.o) + boot.s (assembly) + runtime_stubs.c
    ↓ i686-elf-ld -T linker.ld
kernel.elf
    ↓ QEMU or GRUB
Bare metal execution
```

### 重要な技術的前提

- MoonBitのCバックエンドは **Perceus参照カウント** を使用（トレーシングGCではない）
- ランタイムが必要とするのは `malloc`/`free` のみ → カーネル開発に適している
- `extern "C"` FFIでC/アセンブリとの相互運用が可能
- `extern type` は `void*` にマップされ、RC管理外 → MMIO/ハードウェアバッファに最適

---

## プロジェクト構成

```
moonbit-os/
├── moon.mod.json
├── moon.pkg                     # ルートMoonBitパッケージ設定（native-stub等）
├── Makefile                    # 2段階ビルド: moon build → cross-compile
├── linker.ld                   # カーネルリンカスクリプト
├── boot.s                      # 512-byte ブートセクタ経路
├── moon_kernel.mbt             # ルートMoonBitエントリ（FFI宣言含む）
├── arch/x86/
│   ├── multiboot_boot.s        # Multibootエントリポイント
│   ├── isr_stubs.asm           # 割り込みハンドララッパー (NASM)
│   └── gdt.c                   # GDT/TSS セットアップ
├── cmd/moon_kernel/
│   ├── main.mbt                # MoonBit main package entry
│   └── moon.pkg                # is-main package 設定
├── runtime/
│   ├── runtime_stubs.c         # malloc, free, memcpy, memset, abort
│   ├── moon_kernel_ffi.c       # MoonBit <-> C 文字列ブリッジ
│   └── moon_kernel_ffi_host.c  # MoonBit native codegen 用FFIスタブ
├── drivers/
│   ├── vga.c                   # VGAテキストモードドライバ
│   ├── serial.c                # COM1シリアル出力
│   └── keyboard.c              # PS/2キーボードドライバ
└── kernel/
    ├── main.c                  # Cカーネルエントリ (Phase 0)
    ├── moon_entry.c            # MoonBit呼び出しエントリ (Phase 1)
    ├── fmt.c                   # 最小フォーマット補助
    ├── alloc.mbt               # メモリアロケータ (Phase 3)
    ├── interrupts.mbt          # 割り込みディスパッチロジック (Phase 2)
    └── proc.mbt                # プロセス管理 (Phase 4)
```

注: `moonbit.h` と `runtime.c` は通常リポジトリ内にコピーせず、
`~/.moon/include` と `$MOON_HOME/lib` を `Makefile` から直接参照する。

---

## Phase 0: ツールチェーン構築 + Cベアメタル動作確認

**ゴール**: MoonBitなしで、純粋なCコードがベアメタルでVGA出力できることを確認する

### タスク 0.1: WSL2にi686-elfクロスコンパイラを構築

```bash
# 必要なパッケージ
sudo apt update && sudo apt install build-essential bison flex libgmp3-dev \
  libmpc-dev libmpfr-dev texinfo libisl-dev nasm qemu-system-x86 xorriso grub-pc-bin

# クロスコンパイラ構築
export PREFIX="$HOME/opt/cross"
export TARGET=i686-elf
export PATH="$PREFIX/bin:$PATH"

# Binutils
cd ~/src
wget https://ftp.gnu.org/gnu/binutils/binutils-2.42.tar.gz
tar xzf binutils-2.42.tar.gz && mkdir build-binutils && cd build-binutils
../binutils-2.42/configure --target=$TARGET --prefix="$PREFIX" \
  --with-sysroot --disable-nls --disable-werror
make -j$(nproc) && make install

# GCC (Cのみ)
cd ~/src
wget https://ftp.gnu.org/gnu/gcc/gcc-14.1.0/gcc-14.1.0.tar.gz
tar xzf gcc-14.1.0.tar.gz && cd gcc-14.1.0 && contrib/download_prerequisites && cd ..
mkdir build-gcc && cd build-gcc
../gcc-14.1.0/configure --target=$TARGET --prefix="$PREFIX" \
  --disable-nls --enable-languages=c --without-headers
make -j$(nproc) all-gcc all-target-libgcc
make install-gcc install-target-libgcc
```

**検証**: `i686-elf-gcc --version` が動作すること

### タスク 0.2: Multibootブートスタブ (`multiboot_boot.s`)

```gas
# arch/x86/multiboot_boot.s
.set MAGIC,    0x1BADB002
.set FLAGS,    (1<<0 | 1<<1)          # ページ境界アライン + メモリマップ要求
.set CHECKSUM, -(MAGIC + FLAGS)

.section .multiboot
.align 4
.long MAGIC
.long FLAGS
.long CHECKSUM

.section .bss
.align 16
stack_bottom: .skip 16384             # 16 KiB カーネルスタック
stack_top:

.section .text
.global _start
_start:
    mov $stack_top, %esp
    push %ebx                          # Multiboot情報ポインタ
    push %eax                          # Multibootマジックナンバー
    call kernel_main
    cli
1:  hlt
    jmp 1b
```

### タスク 0.3: リンカスクリプト (linker.ld)

```ld
/* linker.ld */
ENTRY(_start)
SECTIONS {
    . = 1M;                            /* カーネルは1MiBにロード */

    .text BLOCK(4K) : ALIGN(4K) {
        *(.multiboot)                  /* Multibootヘッダを最初に配置 */
        *(.text)
    }
    .rodata BLOCK(4K) : ALIGN(4K) { *(.rodata) }
    .data   BLOCK(4K) : ALIGN(4K) { *(.data) }
    .bss    BLOCK(4K) : ALIGN(4K) {
        *(COMMON)
        *(.bss)
    }
}
```

### タスク 0.4: VGAテキスト出力 (C)

```c
// drivers/vga.c
#include <stdint.h>
#include <stddef.h>

static uint16_t* const VGA_BUFFER = (uint16_t*)0xB8000;
static const int VGA_WIDTH = 80;
static const int VGA_HEIGHT = 25;
static int vga_row = 0;
static int vga_col = 0;

static uint16_t vga_entry(char c, uint8_t color) {
    return (uint16_t)c | ((uint16_t)color << 8);
}

void vga_clear(void) {
    for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++)
        VGA_BUFFER[i] = vga_entry(' ', 0x07);
    vga_row = 0; vga_col = 0;
}

void vga_putchar(char c) {
    if (c == '\n') { vga_row++; vga_col = 0; return; }
    VGA_BUFFER[vga_row * VGA_WIDTH + vga_col] = vga_entry(c, 0x0F);
    if (++vga_col >= VGA_WIDTH) { vga_col = 0; vga_row++; }
    if (vga_row >= VGA_HEIGHT) vga_row = 0; // 簡易スクロール（ラップ）
}

void vga_puts(const char* s) {
    while (*s) vga_putchar(*s++);
}

void kernel_main(uint32_t magic, uint32_t* mboot_info) {
    (void)magic; (void)mboot_info;
    vga_clear();
    vga_puts("Hello from bare metal C!\n");
    vga_puts("Kernel is running.\n");
}
```

### タスク 0.5: Makefile + QEMUで実行

```makefile
CROSS    = $(HOME)/opt/cross/bin/i686-elf-
CC       = $(CROSS)gcc
AS       = $(CROSS)as
LD       = $(CROSS)ld
CFLAGS   = -std=gnu11 -ffreestanding -O2 -Wall -Wextra -fno-stack-protector
LDFLAGS  = -T linker.ld -nostdlib

all: kernel.elf

kernel.elf: multiboot_boot.o vga.o
	$(CC) $(LDFLAGS) $^ -o $@ -lgcc

multiboot_boot.o: arch/x86/multiboot_boot.s
	$(AS) --32 $< -o $@

vga.o: drivers/vga.c
	$(CC) $(CFLAGS) -c $< -o $@

run: kernel.elf
	qemu-system-i386 -kernel $<

debug: kernel.elf
	qemu-system-i386 -kernel $< -s -S &
	$(CROSS)gdb $< -ex "target remote :1234" -ex "break kernel_main" -ex "c"

clean:
	rm -f *.o kernel.elf

.PHONY: all run debug clean
```

**検証**:
- `make run` でQEMUが起動し "Hello from bare metal C!" が表示される
- `grub-file --is-x86-multiboot kernel.elf` がエラーなしで通る

---

## Phase 1: MoonBit "Hello Bare Metal"

**ゴール**: MoonBitで書いたコードがベアメタルでVGAにテキスト出力する

### タスク 1.1: MoonBitランタイム調査

MoonBitのランタイムファイルを調査し、freestanding環境で必要な修正を特定する。

```bash
# MoonBitランタイムの場所を確認
ls ~/.moon/include/moonbit.h
ls ~/.moon/lib/runtime.c  # または近い場所

# ホスト環境依存の #include を洗い出す
grep -n '#include' ~/.moon/include/moonbit.h
grep -n '#include' ~/.moon/lib/runtime.c

# malloc/free/memcpy等の外部依存関数を洗い出す
grep -n 'malloc\|free\|memcpy\|memset\|abort\|printf\|fprintf\|exit' ~/.moon/lib/runtime.c
```

**成果物**: freestanding化に必要な変更点のリスト

### タスク 1.2: ランタイムスタブ実装

```c
// runtime/runtime_stubs.c
#include <stddef.h>
#include <stdint.h>

// === バンプアロケータ (Phase 1: freeはno-op) ===
static uint8_t heap[4 * 1024 * 1024];  // 4 MiB 静的ヒープ
static size_t heap_offset = 0;

void* malloc(size_t size) {
    size = (size + 7) & ~7;  // 8バイトアライン
    if (heap_offset + size > sizeof(heap)) return (void*)0;
    void* ptr = &heap[heap_offset];
    heap_offset += size;
    return ptr;
}

void free(void* ptr) { (void)ptr; }  // Phase 1ではno-op

void* calloc(size_t n, size_t s) {
    void* p = malloc(n * s);
    if (p) memset(p, 0, n * s);
    return p;
}

void* realloc(void* ptr, size_t new_size) {
    (void)ptr;
    return malloc(new_size);  // Phase 1では常に新規確保
}

// === libc最小実装 ===
void* memcpy(void* dest, const void* src, size_t n) {
    uint8_t* d = dest; const uint8_t* s = src;
    for (size_t i = 0; i < n; i++) d[i] = s[i];
    return dest;
}

void* memset(void* s, int c, size_t n) {
    uint8_t* p = s;
    for (size_t i = 0; i < n; i++) p[i] = (uint8_t)c;
    return s;
}

int memcmp(const void* s1, const void* s2, size_t n) {
    const uint8_t* a = s1; const uint8_t* b = s2;
    for (size_t i = 0; i < n; i++) {
        if (a[i] != b[i]) return a[i] - b[i];
    }
    return 0;
}

void abort(void) {
    __asm__ volatile("cli; hlt");
    __builtin_unreachable();
}
```

### タスク 1.3: シリアル出力ドライバ（デバッグ用）

```c
// drivers/serial.c
#include <stdint.h>

#define COM1 0x3F8

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

void serial_init(void) {
    outb(COM1 + 1, 0x00);  // 割り込み無効
    outb(COM1 + 3, 0x80);  // DLAB有効
    outb(COM1 + 0, 0x03);  // 38400 baud
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x03);  // 8N1
    outb(COM1 + 2, 0xC7);  // FIFO有効
    outb(COM1 + 4, 0x0B);  // RTS/DSR有効
}

void serial_putchar(char c) {
    while ((inb(COM1 + 5) & 0x20) == 0);  // 送信可能まで待機
    outb(COM1, c);
}

void serial_puts(const char* s) {
    while (*s) serial_putchar(*s++);
}
```

**QEMUでの使い方**: `qemu-system-i386 -kernel kernel.elf -serial stdio`

### タスク 1.4: MoonBitカーネルパッケージ作成

```moonbit
// moon.pkg
options(
  "supported-targets": [ "native" ],
  "native-stub": [ "runtime/moon_kernel_ffi_host.c" ],
)
```

```moonbit
// moon_kernel.mbt

// C関数のFFI宣言
extern "C" fn c_vga_clear() = "vga_clear"
extern "C" fn c_vga_puts(s : Bytes) = "vga_puts_bytes"
extern "C" fn c_serial_init() = "serial_init"
extern "C" fn c_serial_puts(s : Bytes) = "serial_puts_bytes"

// カーネルエントリポイント (multiboot_boot.s から呼ばれる)
pub fn moon_kernel_entry() -> Unit {
  c_serial_init()
  c_serial_puts(b"MoonBit OS: Serial initialized\n")
  c_vga_clear()
  c_vga_puts(b"MoonBit OS v0.1\n")
  c_vga_puts(b"Hello from MoonBit on bare metal!\n")
}
```

### タスク 1.5: Cラッパー関数 (Bytes型対応)

MoonBitの `Bytes` はヘッダ付きバッファへのポインタ。Cラッパーで適切に処理する。

```c
// drivers/vga_wrapper.c
#include <stdint.h>

// MoonBitの Bytes 型はオブジェクトヘッダの後にデータが続く
// 正確なレイアウトは moonbit.h を参照して調整する必要あり
extern void vga_puts(const char* s);
extern void serial_puts(const char* s);

void vga_puts_bytes(const uint8_t* bytes) {
    // TODO: moonbit.h のレイアウトに合わせてオフセット調整
    vga_puts((const char*)bytes);
}

void serial_puts_bytes(const uint8_t* bytes) {
    serial_puts((const char*)bytes);
}
```

### タスク 1.6: 2段階ビルドの統合

```makefile
# Makefile (Phase 1用に拡張)
CROSS     = $(HOME)/opt/cross/bin/i686-elf-
CC        = $(CROSS)gcc
AS        = $(CROSS)as
NASM      = nasm
CFLAGS    = -std=gnu11 -ffreestanding -O2 -Wall -fno-stack-protector -mno-sse -mno-mmx
LDFLAGS   = -T linker.ld -nostdlib

# Stage 1: MoonBit → C
MOONBIT_BUILD = target/native/release/build
moonbit-gen:
	moon build --target native --output-c
	# 生成されたCファイルを確認
	ls $(MOONBIT_BUILD)/kernel/*.c

# Stage 2: 全てをクロスコンパイル
MOONBIT_C = $(wildcard $(MOONBIT_BUILD)/kernel/*.c)
C_SRCS    = drivers/vga.c drivers/serial.c drivers/vga_wrapper.c runtime/runtime_stubs.c
ASM_SRCS  = arch/x86/multiboot_boot.s

OBJS = multiboot_boot.o $(notdir $(MOONBIT_C:.c=.o)) $(notdir $(C_SRCS:.c=.o))

kernel.elf: moonbit-gen $(OBJS)
	$(CC) $(LDFLAGS) $(OBJS) -o $@ -lgcc

multiboot_boot.o: arch/x86/multiboot_boot.s
	$(AS) --32 $< -o $@

# MoonBit生成Cファイル用のルール
%.o: $(MOONBIT_BUILD)/kernel/%.c
	$(CC) $(CFLAGS) -I runtime/ -c $< -o $@

%.o: drivers/%.c
	$(CC) $(CFLAGS) -c $< -o $@

%.o: runtime/%.c
	$(CC) $(CFLAGS) -c $< -o $@

run: kernel.elf
	qemu-system-i386 -kernel $< -serial stdio

.PHONY: all moonbit-gen run clean
```

**検証**:
- QEMUで "Hello from MoonBit on bare metal!" が表示される
- シリアル出力がターミナルに表示される
- `moon build` → クロスコンパイル → QEMU起動が自動化されている

---

## Phase 2: GDT + IDT + 割り込み

**ゴール**: ハードウェア割り込み（タイマー、キーボード）が動作し、MoonBitでキーボード入力を処理する

### タスク 2.1: GDT (Global Descriptor Table)

5つのセグメント記述子を定義: null, カーネルコード, カーネルデータ, ユーザーコード, ユーザーデータ

```c
// arch/x86/gdt.c
struct gdt_entry { /* 8バイトのセグメント記述子 */ };
struct gdt_ptr   { uint16_t limit; uint32_t base; } __attribute__((packed));

// 5エントリ: null, kernel code, kernel data, user code, user data
static struct gdt_entry gdt[5];
static struct gdt_ptr gp;

extern void gdt_flush(uint32_t);  // boot.s に lgdt + far jump を追加
void gdt_init(void) { /* エントリ設定 + gdt_flush呼び出し */ }
```

### タスク 2.2: IDT (Interrupt Descriptor Table) + PICリマップ

- 256個のIDTエントリを定義
- PIC (8259) をリマップ: IRQ 0-15 → ベクタ 32-47
- CPU例外 (0-31) とハードウェアIRQ (32-47) を分離

### タスク 2.3: ISRスタブ (NASM)

```nasm
; arch/x86/isr_stubs.asm
%macro ISR_NOERR 1
global isr%1
isr%1:
    push dword 0          ; ダミーエラーコード
    push dword %1         ; 割り込み番号
    jmp isr_common
%endmacro

%macro ISR_ERR 1
global isr%1
isr%1:
    push dword %1         ; エラーコードはCPUがpush済み
    jmp isr_common
%endmacro

isr_common:
    pushad
    cld
    push esp              ; registers_t* を引数として渡す
    call interrupt_dispatch
    add esp, 4
    popad
    add esp, 8            ; エラーコード + 割り込み番号をクリーンアップ
    iret
```

### タスク 2.4: 割り込みディスパッチ (MoonBit)

```moonbit
// kernel/interrupts.mbt

// Cディスパッチ関数から呼ばれる
pub fn handle_interrupt(num : Int) -> Unit {
  match num {
    32 => handle_timer()       // IRQ 0: PIT タイマー
    33 => handle_keyboard()    // IRQ 1: キーボード
    _  => {
      // 未処理の割り込み
      c_serial_puts(b"Unhandled interrupt\n")
    }
  }
}

fn handle_timer() -> Unit {
  // tick カウンタ増加等
}

fn handle_keyboard() -> Unit {
  let scancode = c_inb(0x60)
  let ch = scancode_to_ascii(scancode)
  // VGAに文字を表示
}
```

### タスク 2.5: PS/2キーボードドライバ

MoonBitのパターンマッチでスキャンコード→ASCIIの変換テーブルを実装。

**検証**:
- タイマー割り込みが定期的に発火する（シリアルにtickカウント出力）
- キーボード入力がVGA画面に表示される
- 二重フォルトでクラッシュしない

---

## 設計原則（Phase 3 以降に適用）

- **権限は縮小のみ（capability monotonicity）**: capability の権限ビットは生成後に増やせない。DUP や SPAWN で子に渡す際、親の権限のサブセットのみ許可
- **ハンドルが無ければ不可（no ambient authority）**: プロセスは明示的に渡されたハンドル以外のリソースにアクセスする手段を持たない。グローバル名前空間（パス名、ポート番号）は存在しない
- **委譲はサブセット（delegation subset）**: 子プロセスの権限 ⊆ 親プロセスの権限。親が持たない権限を子に与えることはできない

詳細は各 Phase の仕様書を参照。

---

## Phase 3: メモリ管理

**ゴール**: 物理メモリ管理 + ページング + 適切なヒープアロケータ（Perceus RCが正しく動作する）

**詳細仕様**: [SPEC_PHASE3_MEMORY.md](./SPEC_PHASE3_MEMORY.md) — Phase 3 の正本仕様。以下は要約。

### 実装ステップ（9段階）

| Step | 内容 |
|------|------|
| 3-1 | linker.ld に `__kernel_end` シンボル追加 |
| 3-2 | Multiboot メモリマップ解析（kernel/multiboot.h, .c） |
| 3-3 | ビットマップ物理ページアロケータ（kernel/pmm.h, .c） |
| 3-4 | 恒等マッピング + ページング有効化（kernel/paging.h, .c） |
| 3-5 | ページフォルトハンドラ（ベクタ 14 で CR2 出力） |
| 3-6 | free-list ヒープアロケータ（runtime/heap.h, .c） |
| 3-7 | MoonBit パスに Phase 3 初期化統合 |
| 3-8 | Makefile 更新 + 全ビルドパス回帰 |
| 3-9 | 全検証マトリクス + ドキュメント同期 |

### 受け入れテスト

1. `[mmap] entries: ...` が出力される
2. `[pmm] total=..., free=...` が出力される（いずれも 0 は禁止）
3. `[paging] enabled` 後も `[pit] heartbeat` が継続する
4. `[heap-test]` の全項目が OK
5. MoonBit パスが Phase 2 と同等に起動しクラッシュしない

---

## Phase 4: プロセス管理 + スケジューリング

**ゴール**: 複数プロセスがラウンドロビンで切り替わる

### タスク 4.1: プロセス制御ブロック

```moonbit
// kernel/proc.mbt
enum ProcessState {
  Ready
  Running
  Blocked(BlockReason)
  Terminated(Int)  // 終了コード
}

struct Process {
  pid : Int
  state : ProcessState
  // kernel_stack, page_directory, saved_regs はC側で管理
}
```

**Phase 5 向け予約フィールド（Phase 4 では初期値のまま）:**

- `cap_table`: capability ハンドルテーブル（Phase 5a でアクティブ化。Phase 4 では空テーブル）
- `parent_pid`: 親プロセス ID（Phase 5a の delegation で使用）
- `scope_id`: スコープ ID（Phase 5c でアクティブ化。Phase 4 では常に 0）
- `wait_token`: ブロッキング I/O トークン（Phase 5c でアクティブ化。Phase 4 では常に 0）

これらのフィールドの詳細は [SPEC_PHASE5A_CAPABILITY_SYSCALL.md](./SPEC_PHASE5A_CAPABILITY_SYSCALL.md) Section 8 を参照。

### タスク 4.2: コンテキストスイッチ (アセンブリ)

レジスタ保存/復帰 + CR3切り替え（アドレス空間切り替え）。約20行のアセンブリ。

### タスク 4.3: ラウンドロビンスケジューラ

PITタイマー割り込み (100 Hz) でプロセス切り替え。
MoonBitのパターンマッチでスケジューラロジックを記述。

**検証**:
- 2つ以上のカーネルタスクが交互に実行される
- コンテキストスイッチ後にレジスタが正しく復元される

---

## Phase 5a: Capability Syscall + ELF ユーザーランド

**ゴール**: ハンドルベースの capability syscall で Ring 3 プロセスがリソースにアクセスする

**詳細仕様**: [SPEC_PHASE5A_CAPABILITY_SYSCALL.md](./SPEC_PHASE5A_CAPABILITY_SYSCALL.md) — Phase 5a の正本仕様。

- 8 syscall: NOP, EXIT, WRITE, READ_TICKS, DUP, CLOSE, SPAWN, WAIT
- ハンドルテーブル + 権限ビットによるリソースアクセス制御
- init プロセスがカーネルから固定 capability セットを受け取り、子にサブセットを委譲
- グローバル名前空間排除（VGA unmap, IOPB 全ビット 1）

---

## Phase 5b: Wasm ユーザーランド

**ゴール**: Wasm バイナリをユーザープロセスとしてロード・実行する

- ホスト関数が Phase 5a の syscall をラップ
- import テーブル = capability ハンドルセット
- マニフェストベースのロード時検証

---

## Phase 5c: Structured Concurrency（scope / cancel）

**ゴール**: スコープベースのタスク管理と cancel 伝播

- PCB の scope_id / wait_token がアクティブになる
- 全ブロッキング syscall がスコープ死亡時にエラーを返す

---

## Phase 6: シェル + MoonBitユーザーアプリ

**ゴール**: MoonBitで書いたユーザーアプリがシェルから実行できる

### タスク 6.1: RAMファイルシステム

Multibootモジュールとして初期プログラムをロードするフラットなRAMディスク。

### タスク 6.2: シェル

キーボード入力をライン単位で読み取り、コマンド実行。
`echo`, `help`, `ps`, `clear` 等の組み込みコマンド。

### タスク 6.3: MoonBitユーザーランドアプリ

MoonBitプログラムをOS専用のcapability syscall スタブ付きでコンパイルし、ユーザーランドで実行。

**検証**:
- シェルでコマンドを入力し、結果が表示される
- MoonBitで書いたユーザープログラムがcapability syscall 経由でテキスト出力
- `ps` でプロセス一覧が表示される

---

## ハードウェアオプション (QEMU以降)

開発の90%はQEMUで行い、実機テストは後半フェーズで。

### x86オプション

| オプション | 価格帯 | 特徴 |
|-----------|--------|------|
| **中古Thin Client (HP T730等)** | $50-70 | **x86最推奨** ネイティブシリアルポート、PS/2、SMP対応 |
| **Radxa X4** | ~$60 | Intel N100、Raspberry Pi形状、最安のx86 SBC |
| **LattePanda V1** | ~$90-120 | Intel Atom、Arduino共同プロセッサ付き |
| **ZimaBlade** | ~$80 | Intel Celeron、SO-DIMM RAM拡張可能 |

### ARM オプション (Raspberry Pi)

| オプション | 価格帯 | 特徴 |
|-----------|--------|------|
| **Raspberry Pi 4 Model B (2GB)** | ~$35-45 | **ARM最推奨** Cortex-A72 (ARMv8 64bit)、ベアメタルチュートリアル豊富、QEMUで `raspi4b` エミュ可 |
| **Raspberry Pi 3 Model B+** | ~$25-35 | Cortex-A53、チュートリアルが最も多い、入手しやすい |
| **Raspberry Pi 5** | ~$60-80 | Cortex-A76、高性能だがベアメタル資料がまだ少ない、UARTアクセスにRP1 PCIe経由が必要 |
| **Raspberry Pi Zero 2 W** | ~$15 | Cortex-A53 クアッドコア、最安だがデバッグ用ヘッダなし |

**推奨**: まずQEMUでx86開発を完了 → Raspberry Pi 4で ARM移植（チュートリアル・QEMUサポートのバランスが最良）

**追加で必要なもの** (Raspberry Pi実機テスト用):
- USB-TTLシリアルケーブル (~$5-10) — UART経由のデバッグに必須
- microSDカード (8GB以上)
- (任意) USBキーボード + HDMIモニタ

---

## Appendix A: Raspberry Pi (ARM) への移植パス

> x86でPhase 0-2が完了した後に着手することを推奨。
> MoonBitのCバックエンドは**クロスコンパイラを差し替えるだけ**でアーキテクチャを変更できるため、
> MoonBitコード自体の変更は最小限で済む。

### x86 vs ARM: 何が変わり、何が変わらないか

**変わらないもの** (MoonBit層):
- `moon_kernel.mbt` — カーネルエントリポイント
- `kernel/interrupts.mbt` — 割り込みディスパッチロジック
- `kernel/proc.mbt` — プロセス管理
- `kernel/alloc.mbt` — メモリアロケータロジック
- `runtime/runtime_stubs.c` — malloc/free/memcpy等（ほぼ同一）

**変わるもの** (アーキテクチャ層):
- ブートスタブ (`boot.s`) — 完全に書き直し
- リンカスクリプト (`linker.ld`) — ロードアドレス変更
- 割り込みハンドラ (`isr_stubs`) — ARM例外モデルに変更
- VGAドライバ → UARTドライバ (RPiにはVGAテキストバッファがない)
- GDT/IDT → ARM例外ベクタテーブル
- I/Oポート (`outb`/`inb`) → MMIO (メモリマップドI/O)
- クロスコンパイラ: `i686-elf-gcc` → `aarch64-none-elf-gcc`

### ARM移植 タスク A.1: クロスコンパイラのセットアップ

```bash
# WSL2でAArch64ベアメタルクロスコンパイラをインストール
sudo apt install gcc-aarch64-linux-gnu

# または Arm公式ツールチェーンをダウンロード (推奨)
# https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads
# "AArch64 ELF bare-metal target (aarch64-none-elf)" を選択
wget https://developer.arm.com/-/media/Files/downloads/gnu/13.3.rel1/binrel/arm-gnu-toolchain-13.3.rel1-x86_64-aarch64-none-elf.tar.xz
tar xf arm-gnu-toolchain-*.tar.xz -C ~/opt/
export PATH="$HOME/opt/arm-gnu-toolchain-13.3.rel1-x86_64-aarch64-none-elf/bin:$PATH"

# QEMUでRaspberry Pi 4エミュレーション
sudo apt install qemu-system-aarch64
# テスト: qemu-system-aarch64 -M raspi4b -serial stdio -kernel kernel8.img
```

### ARM移植 タスク A.2: Raspberry Piのブートプロセス理解

Raspberry Piのブートはx86のMultibootとは根本的に異なる:

```
電源ON
  ↓
GPU (VideoCore) が bootcode.bin を実行
  ↓
GPU が start.elf をロード・実行
  ↓
start.elf が config.txt を読む
  ↓
start.elf が kernel8.img を RAM の 0x80000 にロード
  ↓
GPU が ARM CPUのリセットラインをトリガー
  ↓
ARM CPU が 0x80000 から実行開始 ← ここからが自分のコード
```

**重要な違い**:
- Multibootヘッダは不要 — GPUファームウェアが代わりに処理する
- カーネルは `kernel8.img` というファイル名の生バイナリ（ELFではない）
- ロードアドレスは `0x80000`（x86の `0x100000` ではない）
- SDカードにFAT32パーティションが必要（`bootcode.bin`, `start.elf`, `config.txt`, `kernel8.img`）

### ARM移植 タスク A.3: ブートスタブ (AArch64)

```gas
// arch/arm/boot.s (AArch64)
.section ".text.boot"
.global _start

_start:
    // CPU IDを確認 — コア0以外は停止させる
    mrs x0, mpidr_el1
    and x0, x0, #3
    cbz x0, core0_boot
    // コア1-3: 無限ループで待機
halt:
    wfe
    b halt

core0_boot:
    // スタックポインタを設定 (カーネルは0x80000にロードされる)
    ldr x0, =_start
    mov sp, x0

    // BSS領域をゼロクリア
    ldr x0, =__bss_start
    ldr x1, =__bss_end
bss_clear:
    cmp x0, x1
    b.ge bss_done
    str xzr, [x0], #8
    b bss_clear
bss_done:

    // kernel_main を呼び出す
    bl kernel_main

    // kernel_main から戻ったら停止
    b halt
```

### ARM移植 タスク A.4: リンカスクリプト (RPi)

```ld
/* linker.ld (Raspberry Pi AArch64) */
ENTRY(_start)
SECTIONS {
    . = 0x80000;    /* RPiのカーネルロードアドレス */

    .text : {
        KEEP(*(.text.boot))    /* ブートコードを最初に配置 */
        *(.text)
    }
    .rodata : { *(.rodata) }
    .data   : { *(.data) }
    .bss    : {
        __bss_start = .;
        *(.bss)
        *(COMMON)
        __bss_end = .;
    }
}
```

### ARM移植 タスク A.5: UART出力 (VGAの代替)

Raspberry Piにはx86のような `0xB8000` VGAテキストバッファが存在しない。
代わりにUART (PL011) 経由でシリアル出力する。これがRPiベアメタルの主要出力手段。

```c
// drivers/uart_rpi.c (Raspberry Pi 3/4用 Mini UART)
#include <stdint.h>

// BCM2711 (RPi 4) のペリフェラルベースアドレス
// RPi 3の場合は 0x3F000000
#define MMIO_BASE       0xFE000000

#define AUX_ENABLES     ((volatile uint32_t*)(MMIO_BASE + 0x00215004))
#define AUX_MU_IO_REG   ((volatile uint32_t*)(MMIO_BASE + 0x00215040))
#define AUX_MU_IER_REG  ((volatile uint32_t*)(MMIO_BASE + 0x00215044))
#define AUX_MU_IIR_REG  ((volatile uint32_t*)(MMIO_BASE + 0x00215048))
#define AUX_MU_LCR_REG  ((volatile uint32_t*)(MMIO_BASE + 0x0021504C))
#define AUX_MU_MCR_REG  ((volatile uint32_t*)(MMIO_BASE + 0x00215050))
#define AUX_MU_LSR_REG  ((volatile uint32_t*)(MMIO_BASE + 0x00215054))
#define AUX_MU_CNTL_REG ((volatile uint32_t*)(MMIO_BASE + 0x00215060))
#define AUX_MU_BAUD_REG ((volatile uint32_t*)(MMIO_BASE + 0x00215068))

#define GPFSEL1         ((volatile uint32_t*)(MMIO_BASE + 0x00200004))
#define GPPUD           ((volatile uint32_t*)(MMIO_BASE + 0x00200094))
#define GPPUDCLK0       ((volatile uint32_t*)(MMIO_BASE + 0x00200098))

static void delay(int32_t count) {
    __asm__ volatile("__delay_%=: subs %[count], %[count], #1; bne __delay_%="
        : "=r"(count) : [count]"0"(count) : "cc");
}

void uart_init(void) {
    *AUX_ENABLES = 1;        // Mini UART有効化
    *AUX_MU_CNTL_REG = 0;   // TX/RX無効化 (設定中)
    *AUX_MU_IER_REG = 0;    // 割り込み無効
    *AUX_MU_LCR_REG = 3;    // 8ビットモード
    *AUX_MU_MCR_REG = 0;
    *AUX_MU_BAUD_REG = 270;  // 115200 baud

    // GPIO 14, 15 をAlternative Function 5 (Mini UART) に設定
    uint32_t ra = *GPFSEL1;
    ra &= ~(7 << 12);  // GPIO 14
    ra |= 2 << 12;     // ALT5
    ra &= ~(7 << 15);  // GPIO 15
    ra |= 2 << 15;     // ALT5
    *GPFSEL1 = ra;

    // プルアップ/ダウン無効化
    *GPPUD = 0;
    delay(150);
    *GPPUDCLK0 = (1 << 14) | (1 << 15);
    delay(150);
    *GPPUDCLK0 = 0;

    *AUX_MU_CNTL_REG = 3;   // TX/RX有効化
}

void uart_putchar(char c) {
    while (!(*AUX_MU_LSR_REG & 0x20));  // TX FIFO空きまで待機
    *AUX_MU_IO_REG = (uint32_t)c;
}

void uart_puts(const char* s) {
    while (*s) {
        if (*s == '\n') uart_putchar('\r');
        uart_putchar(*s++);
    }
}

char uart_getchar(void) {
    while (!(*AUX_MU_LSR_REG & 0x01));  // RXデータ待機
    return (char)(*AUX_MU_IO_REG & 0xFF);
}
```

### ARM移植 タスク A.6: SDカードの準備

```bash
# SDカードにFAT32パーティションを作成し、以下のファイルをコピー:
# 1. Raspberry Pi ファームウェア (GitHubからダウンロード)
#    https://github.com/raspberrypi/firmware/tree/master/boot
#    必要なファイル: bootcode.bin, start.elf, fixup.dat

# 2. config.txt を作成
cat > config.txt << 'EOF'
# AArch64モードで起動
arm_64bit=1
# Mini UARTを有効化
enable_uart=1
# GPUメモリを最小に
gpu_mem=16
EOF

# 3. 自分のカーネルをビルド
aarch64-none-elf-gcc -ffreestanding -nostdlib -nostartfiles \
  -mgeneral-regs-only -c drivers/uart_rpi.c -o uart_rpi.o
aarch64-none-elf-gcc -ffreestanding -nostdlib -nostartfiles \
  -c arch/arm/boot.s -o boot.o
aarch64-none-elf-gcc -ffreestanding -nostdlib -nostartfiles \
  -T linker.ld boot.o uart_rpi.o -o kernel8.elf
aarch64-none-elf-objcopy -O binary kernel8.elf kernel8.img

# 4. kernel8.img をSDカードにコピー
```

### ARM移植 タスク A.7: QEMUでのテスト

```bash
# Raspberry Pi 4 エミュレーション (QEMU 9.0+)
qemu-system-aarch64 -M raspi4b -serial stdio -kernel kernel8.img

# Raspberry Pi 3 エミュレーション
qemu-system-aarch64 -M raspi3b -serial stdio -kernel kernel8.img

# GDBデバッグ
qemu-system-aarch64 -M raspi4b -serial stdio -kernel kernel8.img -s -S &
aarch64-none-elf-gdb kernel8.elf \
  -ex "target remote :1234" -ex "break kernel_main" -ex "c"
```

### ARM移植 タスク A.8: MoonBit統合 (moon.pkg)

```moonbit
// moon.pkg (ARM用)
options(
  "supported-targets": [ "native" ],
  "native-stub": [ "runtime/uart_rpi_ffi_host.c" ],
)
```

MoonBitカーネルコードはほぼ同一。出力先がVGAからUARTに変わるだけ:

```moonbit
// moon_kernel.mbt (ARM版 — 差分は最小)
extern "C" fn c_uart_init() = "uart_init"
extern "C" fn c_uart_puts(s : Bytes) = "uart_puts_bytes"

pub fn moon_kernel_entry() -> Unit {
  c_uart_init()
  c_uart_puts(b"MoonBit OS v0.1 (ARM/Raspberry Pi)\n")
  c_uart_puts(b"Hello from MoonBit on bare metal ARM!\n")
}
```

### ARM移植: x86との主要な違いまとめ

| 項目 | x86 (i686) | ARM (AArch64 / RPi) |
|------|-----------|---------------------|
| クロスコンパイラ | `i686-elf-gcc` | `aarch64-none-elf-gcc` |
| ブートローダー | GRUB (Multiboot) | GPUファームウェア (bootcode.bin + start.elf) |
| カーネル形式 | ELF (`-kernel kernel.elf`) | 生バイナリ (`kernel8.img`) |
| ロードアドレス | `0x100000` (1 MiB) | `0x80000` (512 KiB) |
| 主要出力 | VGAテキストバッファ (`0xB8000`) | UART (MMIO) |
| I/Oアクセス | ポートI/O (`outb`/`inb`) | MMIO (volatile ポインタ) |
| 割り込み | IDT + PIC 8259 | GIC (Generic Interrupt Controller) + ARM例外ベクタ |
| セグメント | GDT (セグメント記述子) | 不要 (フラットメモリモデル) |
| 特権レベル | Ring 0-3 | EL0-EL3 (Exception Level) |
| MMU | 2レベルページテーブル (CR3) | 多レベルページテーブル (TTBR0/TTBR1) |
| コンテキストスイッチ | `pushad`/`popad` + CR3 | `stp`/`ldp` x0-x30 + TTBR0 |
| QEMU | `qemu-system-i386 -kernel` | `qemu-system-aarch64 -M raspi4b -kernel` |
| 実機デバッグ | シリアルポート (COM1) | USB-TTLケーブル + GPIO 14/15 |

### ARM移植: 推奨する進め方

1. **x86でPhase 0-2を完了する** — OS開発の基礎概念をx86で学ぶ
2. **ARM Phase 0**: ブートスタブ + UART出力 (Cのみ、QEMU上)
3. **ARM Phase 1**: MoonBit統合 — `runtime_stubs.c` はそのまま流用、ドライバだけ差し替え
4. **ARM Phase 2**: GIC + ARM例外ハンドリング + キーボード (USB HIDはx86のPS/2より複雑なので後回し推奨)
5. **Phase 3以降**: MoonBitのカーネルロジック (alloc, proc, syscall) はアーキテクチャ非依存なので共通化

> **⚠️ 将来の課題: ARM環境でのキーボード入力**
>
> Raspberry PiにはPS/2コントローラがないため、キーボード入力にはUSB HIDドライバが必要。
> USB HIDの実装はUSBホストコントローラ (DWC2/xHCI) → USBプロトコルスタック → HIDクラスドライバと多層にわたり、
> x86のPS/2 (`inb(0x60)` で1バイト読むだけ) とは比較にならない複雑さがある。
>
> **当面の回避策**: UART経由のシリアル入力で代替する（USB-TTLケーブル経由でホストPCのターミナルから入力）。
> これならドライバ不要で `uart_getchar()` だけで済む。
>
> **将来の選択肢**:
> - [rsta2/circle](https://github.com/rsta2/circle) のUSBドライバを参考に移植
> - USBスタックだけCで書き、MoonBitからFFI経由で利用
> - USBキーボードが必要になった段階で改めて設計を検討

最終的なプロジェクト構成:

```
moonbit-os/
├── arch/
│   ├── x86/                # x86固有コード
│   │   ├── boot.s
│   │   ├── isr_stubs.asm
│   │   ├── gdt.c
│   │   └── linker.ld
│   └── arm/                # ARM固有コード
│       ├── boot.s
│       ├── exception_stubs.s
│       ├── gic.c
│       └── linker.ld
├── drivers/
│   ├── vga.c              # x86専用
│   ├── serial.c           # x86専用 (COM1)
│   └── uart_rpi.c         # ARM専用 (Mini UART)
├── runtime/               # アーキテクチャ共通
│   └── runtime_stubs.c
└── kernel/                # アーキテクチャ共通 (MoonBit)
    ├── main.mbt
    ├── interrupts.mbt
    ├── alloc.mbt
    └── proc.mbt
```

---

## 参考リソース

### x86 OS開発

- [OSDev Wiki - Bare Bones](https://wiki.osdev.org/Bare_Bones) — Cでの最小カーネルチュートリアル
- [Writing an OS in Rust](https://os.phil-opp.com/) — フェーズ構成のお手本
- [OSDev Wiki - Zig Bare Bones](https://wiki.osdev.org/Zig_Bare_Bones) — 非C言語でのOS開発パターン
- [Bran's Kernel Development](http://www.osdever.net/bkerndev/Docs/idt.htm) — IDT/割り込みの実装ガイド

### ARM / Raspberry Pi ベアメタル

- [Learning OS Development Using Linux Kernel and RPi](https://s-matyukevich.github.io/raspberry-pi-os/) — **最推奨** ARMv8ベアメタルOSチュートリアル (Linux Kernelと対比しながら学ぶ)
- [Writing a Bare Metal OS for RPi 4](https://www.rpi4os.com/) — RPi 4専用チュートリアル (WSL対応)
- [OSDev Wiki - Raspberry Pi Bare Bones](https://wiki.osdev.org/Raspberry_Pi_Bare_Bones) — OSDev流RPiベアメタル入門
- [bztsrc/raspi3-tutorial](https://github.com/bztsrc/raspi3-tutorial) — RPi 3 AArch64ベアメタルチュートリアル集
- [babbleberry/rpi4-osdev](https://github.com/babbleberry/rpi4-osdev) — RPi 4 OS開発チュートリアル
- [dwelch67/raspberrypi](https://github.com/dwelch67/raspberrypi) — RPi全世代のベアメタルサンプル集
- [rsta2/circle](https://github.com/rsta2/circle) — C++ベアメタル環境 (USBサポート含む、参考実装として有用)
- [Raspberry Pi RP1 Peripherals Datasheet](https://datasheets.raspberrypi.com/rp1/rp1-peripherals.pdf) — RPi 5ペリフェラル仕様

### MoonBit / 共通

- [MoonBit C-FFI Guide](https://www.moonbitlang.com/pearls/moonbit-cffi) — FFIのレイアウトとABI
- [MoonBit ESP32-C3 Demo](https://www.moonbitlang.com/blog/moonbit-esp32) — 組み込み環境での先行事例
- [MIT xv6](https://github.com/mit-pdos/xv6-riscv) — カーネルモジュール構成の参考 (99ページ)
