# MoonBitのCバックエンドでToy OSを構築する: ベアメタルブートからユーザーランドまで

## この文書の位置づけ

- 本ファイルは「詳細版レポート（正本）」です。
- 英語の要約版は [REPORT_EN.md](./REPORT_EN.md) を参照してください。
- 実装マイルストーンは [ROADMAP.md](./ROADMAP.md)（英語要約: [ROADMAP_EN.md](./ROADMAP_EN.md)）を参照してください。
- 16-bit から 32-bit への移行実装は [tutorial-01-protected-mode.md](./tutorial-01-protected-mode.md) を参照してください。
- 文書全体の読み順は [README.md](./README.md) を参照してください。

## 状況アップデート（2026-02-15）

- 実装済み: Multiboot C カーネル経路（`kernel.elf`）とシリアル診断出力。
- 実装済み: 初期 MoonBit カーネル経路（`moon-kernel.elf`）の起動とシリアルログ出力。
- 実装済み: `runtime/runtime_stubs.c` のアロケータ堅牢化
  （`malloc` オーバーフロー対策、`calloc` 乗算オーバーフロー対策、`realloc` のデータ保持）。
- 実装済み: MoonBit FFI `Bytes` 所有権アノテーション警告を
  `moon_kernel.mbt` の `#borrow` で解消。
- 実装済み: `Makefile` の MoonBit ビルド依存関係を `moon.pkg` ベースに修正。
- 実装済み: Phase 2 割り込み基盤は完了（Step 1-10）
  （実装本体は Step 9 まで: IDT/ISR/PIC/PIT/keyboard 配線、MoonBit ポーリング API、例外セルフテストを含む検証マトリクス。Step 10 はドキュメント同期）。

- 計画確定: Phase 3 メモリ管理仕様書作成（SPEC_PHASE3_MEMORY.md）— 物理ページアロケータ、ページング、free-list ヒープ
- 設計決定: Phase 5 を capability ベースのセキュリティモデルに変更。5a（capability syscall + ELF）/ 5b（Wasm）/ 5c（structured concurrency）に分割
- 計画確定: Phase 5a 仕様書作成（SPEC_PHASE5A_CAPABILITY_SYSCALL.md）— 8 syscall、ハンドルテーブル、委譲モデル

**MoonBitはベアメタルx86開発に適したフリースタンディングCコードにコンパイル可能だが、カスタムメモリアロケータとランタイムスタブの提供が必要。** 最も重要な発見は、MoonBitのCバックエンドが**Perceus参照カウント**を使用していること——トレーシングGCではない——つまり動作に必要なのは`malloc`/`free`のみで、当初の予想よりはるかにOS開発に適している。GRUBのMultibootによるブート、QEMUでのテスト、WSL2上のi686-elfクロスコンパイラを組み合わせれば、MoonBitソースからベアメタルx86実行までの完全なカーネルパイプラインを構築できる。本レポートでは、現在の"Hello World"ブートローダーから、MoonBitユーザーランドアプリケーションが動作する最小限のOSまでの完全なロードマップを提供する。

---

## MoonBitのCバックエンドは意外なほどOS開発に適している

MoonBitのネイティブCバックエンドにおけるコンパイルパイプラインは以下の通り: MoonBitソース → Core IR (ANFスタイル) → MCore IR (単相化、ジェネリクス除去) → CLambda IR (クロージャ除去、RC組み込み命令挿入) → **Cソース出力** → GCC/Clang → ネイティブバイナリ。`moon build --target native`でトリガーし、生成された`.c`ファイルは`target/native/release/build/<package>/`に出力される。

OS開発にとって最も重要な発見はMoonBitのメモリ管理戦略だ。**MoonBitのCバックエンドはBoehm GCやいかなるトレーシングコレクタも使用していない。** コンパイラが最適化した参照カウントを使用しており、これは**Perceusアルゴリズム**（Microsoft Research、Koka言語で考案）にインスパイアされている。コンパイラがコンパイル時に正確な`moonbit_incref`/`moonbit_decref`呼び出しを挿入する。参照カウントがゼロになった瞬間にオブジェクトが解放される——ヒープスキャンなし、Stop-the-Worldポーズなし、ルートセット列挙なし。これは決定論的で予測可能であり、カーネルコードにとって理想的な特性だ。

ランタイムのフットプリントは小さい: オブジェクトレイアウトと型マッピングを定義するヘッダファイル（`~/.moon/include/`の`moonbit.h`）と、ランタイムCファイル（`$MOON_HOME/lib/runtime.c`）のみ。すべてのヒープオブジェクトは参照カウントフィールドを持つオブジェクトヘッダを持ち、データポインタの*前方*に配置される（2025年2月のABI変更により、MoonBitオブジェクトはヘッダではなく最初のデータフィールドを指すようになった）。型ABIはCプリミティブにクリーンにマッピングされる:

| MoonBit型 | C表現 | 備考 |
|---|---|---|
| `Int`, `UInt` | `int32_t`, `uint32_t` | 直接値型 |
| `Int64`, `UInt64` | `int64_t`, `uint64_t` | 直接値型 |
| `Float` / `Double` | `float` / `double` | 標準IEEE 754 |
| `Bool` | `int32_t` | 0または1 |
| `Byte` | `uint8_t` | 直接 |
| `Bytes` | `uint8_t*` | GC管理オブジェクト内を指す |
| `FixedArray[T]` | `T*` | 連続配置、GC管理 |
| `String` | `uint16_t*` (UTF-16) | 複雑なRC管理オブジェクト |
| `extern type T` | `void*` | RC管理**対象外** — MMIOに最適 |
| `FuncRef[T]` | C関数ポインタ | キャプチャなし — ISRコールバックに使用可能 |

ビルドシステムは`moon.pkg`経由でターゲット設定と`native-stub`を定義できる:

```moonbit
options(
  "supported-targets": [ "native" ],
  "native-stub": [ "runtime/moon_kernel_ffi_host.c" ],
)
```

**中心的な課題は、MoonBitに公式の`--freestanding`モードがないこと。** ランタイムは`malloc`/`free`の存在を期待し、`println`には何らかの出力メカニズムが必要だ。これらは自分で提供しなければならない。しかしESP32-C3での先行事例——MoonBitがESP-IDF上のRISC-Vマイクロコントローラでコンウェイのライフゲームを実行——は、ネイティブCバックエンドが制約のある環境で動作することを証明している。違いは: ESP-IDFが`malloc`を含む完全なCライブラリを提供すること。ベアメタルOSカーネルでは、**自分がこれらのプリミティブの提供者になる**。

---

## これを実現する2段階ビルドパイプライン

重要なアーキテクチャ上の洞察は、MoonBitのコンパイルとカーネルのコンパイルが**2つの別々の段階**であることだ。MoonBitがCソースファイルを生成し、クロスコンパイルツールチェーンがそれらのCファイルをベアメタルオブジェクトコードに変換する。完全なパイプラインは以下の通り:

```
ステージ1 (ホスト上):  MoonBitソース → moon build → Cファイル
ステージ2 (クロス):    Cファイル + multiboot_boot.s + stubs.c → i686-elf-gcc → .oファイル → リンカ → kernel.elf
ブート:               GRUBがMultiboot経由でkernel.elfをロード → kernel_main()
```

### WSL2でのi686-elfクロスコンパイラのセットアップ

Ryzen 7 6800Hの8コアならビルドは高速（約15分）。前提パッケージをインストールしてソースからビルドする:

```bash
sudo apt update && sudo apt install build-essential bison flex libgmp3-dev \
  libmpc-dev libmpfr-dev texinfo libisl-dev nasm qemu-system-x86 xorriso grub-pc-bin

export PREFIX="$HOME/opt/cross"
export TARGET=i686-elf
export PATH="$PREFIX/bin:$PATH"

# Binutilsのビルド
mkdir -p ~/src && cd ~/src
wget https://ftp.gnu.org/gnu/binutils/binutils-2.42.tar.gz
tar xzf binutils-2.42.tar.gz
mkdir build-binutils && cd build-binutils
../binutils-2.42/configure --target=$TARGET --prefix="$PREFIX" \
  --with-sysroot --disable-nls --disable-werror
make -j$(nproc) && make install && cd ..

# GCCのビルド（Cのみ、ホストライブラリなし）
wget https://ftp.gnu.org/gnu/gcc/gcc-14.1.0/gcc-14.1.0.tar.gz
tar xzf gcc-14.1.0.tar.gz
cd gcc-14.1.0 && contrib/download_prerequisites && cd ..
mkdir build-gcc && cd build-gcc
../gcc-14.1.0/configure --target=$TARGET --prefix="$PREFIX" \
  --disable-nls --enable-languages=c --without-headers
make -j$(nproc) all-gcc all-target-libgcc
make install-gcc install-target-libgcc
```

代替手段として、Dockerイメージも使用可能: `docker run -it -v "/home/$USER:/root" --rm alessandromrc/i686-elf-tools`

### 全体を統合するMakefile

```makefile
CROSS    = $(HOME)/opt/cross/bin/i686-elf-
CC       = $(CROSS)gcc
AS       = $(CROSS)as
NASM     = nasm
CFLAGS   = -std=gnu11 -ffreestanding -O2 -Wall -fno-stack-protector -fno-exceptions
LDFLAGS  = -ffreestanding -nostdlib -T linker.ld -lgcc

# MoonBit生成C（moon buildの出力からコピー）
MOONBIT_C = moonbit_output.c
# カーネルサポートCファイル群
SUPPORT_C = runtime_stubs.c serial.c vga.c alloc.c idt.c
ASM_SRC   = multiboot_boot.s
NASM_SRC  = isr_stubs.asm

OBJS = boot.o isr_stubs.o $(MOONBIT_C:.c=.o) $(SUPPORT_C:.c=.o)

kernel.elf: $(OBJS)
	$(CC) $(LDFLAGS) $^ -o $@

%.o: %.s
	$(AS) $< -o $@
%.o: %.asm
	$(NASM) -f elf32 $< -o $@
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

run: kernel.elf
	qemu-system-i386 -kernel $< -serial stdio
debug: kernel.elf
	qemu-system-i386 -kernel $< -serial stdio -s -S &
	$(CROSS)gdb $< -ex "target remote :1234" -ex "break kernel_main" -ex "c"
```

---

## フェーズごとの開発ロードマップ

OSDev wikiのBare Bonesチュートリアル、Philipp Oppermannの"Writing an OS in Rust"の構成、Zig Bare Bonesアプローチに基づき、MoonBit向けに適応した完全なマイルストーン進行を示す:

### Phase 0: ツールチェーンと「Cによるベアメタル」概念実証（第1週）

MoonBitに触れる前に、**素のC**がベアメタルで動作することを証明する。これによりMoonBit統合のデバッグ時に変数を排除できる。

Multiboot準拠のアセンブリブートスタブ（`multiboot_boot.s`）を作成し、16 KiBスタックをセットアップして`kernel_main`を呼び出す。**Multiboot仕様**がここで本質的に重要——GRUB（およびQEMUの`-kernel`フラグ）はMultibootヘッダを持つ任意のELFバイナリをロードでき、リアルモード→プロテクトモード遷移、A20ライン、メモリ検出を代行してくれる。ブートアセンブリは約30行で済む:

```gas
.set MAGIC, 0x1BADB002
.set FLAGS, (1<<0 | 1<<1)
.set CHECKSUM, -(MAGIC + FLAGS)

.section .multiboot
.align 4
.long MAGIC
.long FLAGS
.long CHECKSUM

.section .bss
.align 16
stack_bottom: .skip 16384
stack_top:

.section .text
.global _start
_start:
    mov $stack_top, %esp
    push %ebx          /* Multiboot情報ポインタ */
    push %eax          /* Multibootマジック */
    call kernel_main
    cli
1:  hlt
    jmp 1b
```

最小限のCカーネルを作成し、**0xB8000**のVGAテキストバッファに直接書き込む（80×25グリッド、1文字あたり2バイト: ASCIIバイト + カラー属性バイト）。カーネルを1 MiBに配置し、Multibootヘッダを先頭8 KiB内に置くリンカスクリプトを作成する。`grub-file --is-x86-multiboot kernel.elf`で検証し、`qemu-system-i386 -kernel kernel.elf`でテストする。

### Phase 1: MoonBit "Hello Bare Metal"（第2-3週）

ここでMoonBitを統合する。目標: MoonBit生成Cコードが VGA経由で画面にテキストを表示する。

**ステップ1: 最小限のMoonBitカーネルパッケージを作成。** `extern "C"` FFIを使ってCのVGAドライバを呼び出す、MoonBit版のエントリポイント（`moon_kernel_entry`）を書く:

```moonbit
// moon_kernel.mbt
extern "C" fn vga_puts(s : Bytes) -> Unit = "moon_kernel_vga_puts"
extern "C" fn serial_puts(s : Bytes) -> Unit = "moon_kernel_serial_puts"

pub fn moon_kernel_entry() -> Unit {
  vga_puts(b"MoonBit OS booting...\n")
  serial_puts(b"Serial debug output working\n")
}
```

**ステップ2: ランタイムスタブを提供。** MoonBit生成Cは`malloc`、`free`、`memcpy`、`memset`、場合によっては`abort`を呼び出す。`runtime_stubs.c`を作成:

```c
#include <stddef.h>
#include <stdint.h>

// バンプアロケータ（Phase 1 — freeなし）
static uint8_t heap[4 * 1024 * 1024];  // 4 MiB 静的ヒープ
static size_t heap_offset = 0;

void* malloc(size_t size) {
    size = (size + 7) & ~7;  // 8バイトアライン
    if (heap_offset + size > sizeof(heap)) return 0;
    void* ptr = &heap[heap_offset];
    heap_offset += size;
    return ptr;
}
void free(void* ptr) { (void)ptr; }  // バンプアロケータではno-op
void* calloc(size_t n, size_t s) {
    void* p = malloc(n * s);
    if (p) { uint8_t* b = p; for (size_t i = 0; i < n*s; i++) b[i] = 0; }
    return p;
}
void* realloc(void* p, size_t s) { return malloc(s); }
void* memcpy(void* d, const void* s, size_t n) {
    uint8_t* dd = d; const uint8_t* ss = s;
    for (size_t i = 0; i < n; i++) dd[i] = ss[i]; return d;
}
void* memset(void* s, int c, size_t n) {
    uint8_t* p = s; for (size_t i = 0; i < n; i++) p[i] = c; return s;
}
void abort(void) { __asm__ volatile("cli; hlt"); __builtin_unreachable(); }
```

**ステップ3: ビルドとリンク。** `moon build --target native`でCファイルを生成し、`i686-elf-gcc -ffreestanding -nostdlib`でブートアセンブリおよびスタブとリンクする。現在の構成では`Makefile`の`MOON_INCLUDE_DIR`/`MOON_RUNTIME_C`で`~/.moon/include`と`$MOON_HOME/lib/runtime.c`を直接参照するため、リポジトリ内に`moonbit.h`や`runtime.c`を複製しない運用に統一できる。

**ステップ4: デバッグ用シリアル出力。** COM1シリアルドライバ（ポート**0x3F8**）を実装——`outb`/`inb`インラインアセンブリを使って約20行のCだけで済む。QEMUの`-serial stdio`フラグがシリアル出力をターミナルにリダイレクトし、完全なVGAドライバを実装せずに`printf`相当のデバッグが可能になる。

### Phase 2: GDT、IDT、割り込み（第4-6週）

テキスト出力が動作したら、割り込みインフラを構築する。大部分はC/アセンブリの作業で、MoonBitが高レベルロジックを担当する。

**GDTセットアップ**: 5つのセグメント記述子（null、カーネルコード、カーネルデータ、ユーザーコード、ユーザーデータ）とTSSエントリを定義。小さなアセンブリヘルパーで`lgdt`命令を使ってロードする。

**IDTとISRスタブ**: 256個のIDTエントリを作成。32個のCPU例外と16個のハードウェアIRQそれぞれに、全レジスタを保存（`pushad`）してCディスパッチ関数を呼び出し、レジスタを復元（`popad`）して`iret`（`ret`ではない）で戻るアセンブリスタブが必要。Cディスパッチ関数はそこからMoonBitハンドラを呼べる:

```nasm
; isr_stubs.asm — NASMマクロで48個のスタブを全生成
%macro ISR_NOERR 1
global isr%1
isr%1:
    push dword 0        ; ダミーエラーコード
    push dword %1       ; 割り込み番号
    jmp isr_common
%endmacro

isr_common:
    pushad
    cld
    push esp
    call interrupt_dispatch  ; C関数 → MoonBitハンドラを呼び出す
    add esp, 4
    popad
    add esp, 8
    iret
```

**PICリマップ**: IRQ 0-15をベクタ0-15からベクタ**32-47**にリマップ（CPU例外との衝突を回避）。その後、タイマー（IRQ 0 → ベクタ32）とキーボード（IRQ 1 → ベクタ33）を有効化する。

**MoonBitによるキーボードドライバ**: これが最初の本格的なMoonBitカーネルモジュールだ。`extern "C"`で`inb(0x60)`を呼び出してスキャンコードを読み取り、MoonBitのパターンマッチでスキャンコード→ASCIIの変換テーブルを実装する——この言語の優れたユースケースだ。

### Phase 3: メモリ管理（第7-10週）

バンプアロケータを本格的なメモリ管理に置き換える。MoonBit統合にとって最も重要なフェーズ——**Perceus参照カウントは`free()`を絶えず呼び出す**からだ。参照カウントがゼロになるすべてのオブジェクトが即座に解放をトリガーする。

**物理メモリマネージャ**: Multibootメモリマップ（ブート時にGRUBが`ebx`レジスタ経由で渡す）を解析して利用可能なRAMを検出する。4 KiB物理ページフレームを追跡するビットマップアロケータを実装する。MoonBitの`FixedArray[UInt]`はCの`uint32_t*`に直接マッピングされ、ビットマップ操作が自然にできる。

**ページング**: 2レベルページテーブル（ページディレクトリ + ページテーブル）をセットアップして仮想メモリを構築。カーネル領域とVGAバッファを恒等マッピング。`CR3`レジスタを使ってページディレクトリアドレスをロード。CR3の読み書き、`invlpg`（TLBフラッシュ）、`CR0`のページングビット有効化のための小さなアセンブリヘルパーが必要。

**フリーリストヒープアロケータ**: バンプアロケータを、本格的な`free()`をサポートするFirst-fitフリーリストアロケータに置き換える。各割り当てブロックに8バイトヘッダ（サイズ + freeフラグ + nextポインタ）を付与。これでMoonBitの参照カウントが正しく機能するのに十分だ。Toy OS用には4 MiBの初期ヒープが妥当。

```c
typedef struct block_header {
    size_t size;
    int is_free;
    struct block_header* next;
} block_header_t;
```

**なぜPerceus RCがトレーシングGCよりここで優れているか**: トレーシングGCはスタックを走査してヒープをスキャンし、ルートを見つける必要がある——割り込みハンドラがいつでも発火でき、スタックレイアウトが非標準的なカーネルでは悪夢レベルの複雑さだ。参照カウントは`malloc`/`free`だけでよい。唯一の注意点は**参照サイクルが検出されない**こと——MoonBitの関数型設計はサイクルを最小化するが、カーネルデータ構造（例: 双方向リンクのプロセスリスト）では注意が必要。

### Phase 4: プロセス管理とスケジューリング（第11-14週）

**プロセス構造**: PID、ページディレクトリポインタ、カーネルスタック、保存されたレジスタ状態、スケジューリング状態を持つプロセス制御ブロックを定義。MoonBitの代数的データ型がここで威力を発揮する:

```moonbit
enum ProcessState {
  Ready
  Running
  Blocked(BlockReason)
  Terminated(Int)  // 終了コード
}
```

**コンテキストスイッチ**: 全CPUレジスタの保存/復帰とページディレクトリの切り替え。コンテキストスイッチ自体はアセンブリで記述する必要がある（約20行の`esp`、`ebp`、汎用レジスタの保存/復帰、さらにアドレス空間切り替え用の`CR3`スワップ）。

**ラウンドロビンスケジューラ**: Readyプロセスのリンクリストを維持する。各タイマー割り込み（PITから100 Hz）で、現在のプロセス状態を保存し次のReadyプロセスに切り替える。MoonBitのパターンマッチがスケジューラロジックをクリーンにする。

### Phase 5: Ring 3ユーザーランド（第15-18週）

**TSS設定**: タスクステートセグメントに`esp0`（カーネルスタック）と`ss0`（カーネルデータセグメント）を設定。CPUはRing 3 → Ring 0の遷移時にこれらを自動的に使用する。

**Ring 3への遷移**: `iret`トリックを使用——Ring 3セグメントセレクタ（RPL=3）とユーザープログラムのエントリポイントを持つ偽の割り込みフレームをプッシュし、`iret`を実行。CPUがRing 3に遷移しユーザーコードの実行を開始する。

**システムコール**: `int 0x80`にハンドラをインストール。ユーザープログラムは`eax`にシステムコール番号、`ebx`/`ecx`/`edx`に引数をロードし、`int 0x80`を実行してシステムコールを呼び出す。カーネルハンドラが適切な関数にディスパッチする。ここでMoonBitが真に輝く——システムコール番号に対するパターンマッチによるsyscallディスパッチテーブル。

**ELFローダー**: 単純なELFバイナリを解析し、そのセグメントをユーザーアドレス空間にマップして、エントリポイントにジャンプする。ユーザープログラムは最初、OS固有のsyscall ABI向けにスタティックリンクされたMoonBitプログラムとなる。

### Phase 6: シンプルなシェルとユーザープログラム（第19-22週）

**RAMファイルシステム**: フラットなファイルエントリ（名前 + データポインタ + サイズ）を持つ単純なラムディスクを作成。ブート時にMultibootモジュール経由で初期プログラムをラムディスクにロード（GRUBはカーネルと一緒に追加ファイルをロードできる）。

**シェル**: キーボード入力をライン単位で読み取り、コマンドを解析し、ラムディスクからプログラムをfork/exec。`echo`、`help`、`ps`（プロセスリスト）、`clear`などのコマンドでOSに活気が出る。

**MoonBitユーザーランドアプリケーション**: 最終目標——OS独自のsyscallインターフェースをターゲットにしてMoonBitプログラムをコンパイルする。各ユーザープログラムは、MoonBitバイナリをCにコンパイルし、ホスト環境のlibc呼び出しをOS固有のsyscallスタブに置き換えてクロスコンパイルしたもの。ユーザープログラム内のPerceus RCは`malloc`/`free`を呼び出し、それがOS側の`brk`/`sbrk`システムコールを起動してヒープ管理を行う。

---

## QEMU以外のハードウェアオプション

QEMUが主要な開発環境であるべき——**趣味のOS作業の90%はエミュレータ上で行う**。ワークフローは最強: コード編集、`make run`、即座に結果確認、`qemu-system-i386 -s -S`経由の完全なGDBデバッグ付き。ただしQEMUは「甘すぎる」面もある——メモリをゼロ初期化し（未初期化変数バグをマスク）、キャッシュ制御ビットを無視し、特定の操作で実ハードウェアより200倍高速になることもある。

実ハードウェアテスト用の最良オプションをコストパフォーマンス順にランク付け:

- **中古シンクライアント（$50-$70）**: eBayのHP T730やT620がOSDevコミュニティの一番のお勧め。**ネイティブシリアルポート**（重要——シリアルドライバは30行のコードだがUSBは数百行）、PS/2ポート、BIOSでのSMPトグル、フルx86-64 CPUを備える。これが断然最良の選択肢。
- **Radxa X4（約$60）**: 最安の新品x86 SBC。Intel N100クアッドコア、4 GB LPDDR5、Raspberry Piフォームファクタ。ネイティブシリアルポートなし（USB-UARTアダプタ要）だが、UEFI/BIOSのMultibootサポートあり。
- **LattePanda V1（約$90-$120）**: Intel Atom搭載の定番x86 SBC、Arduino共同プロセッサ付き。古いが実績あり。
- **ZimaBlade（約$80）**: Intel Celeron搭載、SO-DIMM RAMとSATAポート付きで拡張可能。NAS向け設計だがOS開発にも使える。

RISC-Vボードは魅力的に安い（**Milk-V Duoが$5**、VisionFive 2が$55）しISAがクリーン（仕様書236ページ対x86の2,000ページ超）だが、OSDevエコシステムは未成熟。チュートリアルが少なく、ボード間でファームウェアが異なり、QEMU RISC-Vエミュレーションも成熟度が低い。**x86で始めて、RISC-Vはセカンドターゲットとして検討**——OSアーキテクチャが安定してから。MoonBitのCバックエンドにより、別アーキテクチャへのリターゲティングは主にクロスコンパイラとアセンブリスタブの差し替えで済む。

---

## Rust、Zig、GoのOSプロジェクトから学ぶこと

**Philipp Oppermannの"Writing an OS in Rust"**（GitHubスター17,300）はチュートリアル構成のゴールドスタンダード。そのフェーズ型アプローチ——フリースタンディングバイナリ→Multibootブート→VGAテキスト→CPU例外→ハードウェア割り込み→ページング→ヒープ割り当て→アロケータ設計→非同期マルチタスキング——は上記のMoonBitロードマップに直接対応する。第2版ではRustの`global_asm!`とカスタム`bootloader`クレートを使って全てのC依存を排除した。重要な教訓: 全ての`unsafe`なハードウェアアクセスを安全な抽象化の背後にカプセル化すること。

**Zigのベアメタルプロジェクト**（Pluto/ZystemOSの660スター、OSDev Wiki Zig Bare Bonesチュートリアル）はMoonBitで実現できるパターンと非常に類似したパターンを示している。Zigは`-target freestanding`でOS前提なしにコンパイルし、カスタムリンカスクリプトとインラインアセンブリでハードウェアアクセスを行う。カーネルモードでSIMD/FPU機能を無効にするZigのアプローチ（`-mno-sse -mno-mmx`）はそのまま適用可能——`cc-flags`に`-mno-sse -mno-mmx -mgeneral-regs-only`を追加する。

**MITのxv6**（RISC-V）はカーネル構造の最良のリファレンスアーキテクチャ。フォーマット済みソースコードわずか99ページで、プロセス、仮想メモリ、ファイルシステム、システムコール、スケジューリング、シェルを実装している。そのモジュール構造はMoonBitパッケージにクリーンにマッピングできる: `kalloc.c` → `alloc.mbt`、`proc.c` → `proc.mbt`、`trap.c` → `interrupts.mbt`、`vm.c` → `paging.mbt`。

**Biscuit（Goカーネル、MIT OSDI'18）** はGC付き言語をカーネルで使う際の最も重要な教訓を提供する: 割り込みハンドラ中のGCポーズがマウスカーソルのラグを引き起こした。しかしMoonBitのPerceus RCはこの問題を完全に回避する——解放は決定論的かつインクリメンタルであり、Stop-the-Worldフェーズではなく通常の実行中にインラインで行われる。

---

## ベアメタルでの最初のMoonBit出力へのクリティカルパス

ベアメタルでMoonBitコードが目に見える出力を生成するための**最小限の到達可能パス**には、正確に以下のコンポーネントが必要:

1. **アセンブリブートスタブ**（30行）— Multibootヘッダ + スタックセットアップ + `call kernel_main`
2. **リンカスクリプト**（20行）— カーネルを1 MiBに配置、Multibootヘッダを先頭に
3. **ランタイムスタブ**（50行）— `malloc`（バンプアロケータ）、`free`（no-op）、`memcpy`、`memset`、`abort`
4. **CのVGAドライバ**（20行）— `0xB8000`に文字を書き込む
5. **MoonBitカーネルエントリ**（10行）— VGAドライバへの`extern "C"` FFI呼び出し
6. **MoonBitランタイム参照**（`~/.moon/include/moonbit.h` と `$MOON_HOME/lib/runtime.c` をビルド時参照）
7. **i686-elfクロスコンパイラ** — すべてをベアメタルx86用にコンパイル

カスタムコードの合計: **C/アセンブリ約150行** + MoonBitソース。"Hello Bare Metal"マイルストーンに到達するのは週末プロジェクトだ。そこから先、各フェーズはインクリメンタルに機能を追加していく。

プロジェクト構造は以下のレイアウトに従うべき:

```
moonbit-os/
├── moon.mod.json
├── moon.pkg               # package-level options (native-stub など)
├── Makefile               # 2段階ビルド: moon build → cross-compile
├── linker.ld
├── boot.s                 # 512-byte ブートセクタ経路
├── moon_kernel.mbt        # ルートMoonBitエントリ（FFI宣言含む）
├── arch/x86/
│   ├── multiboot_boot.s   # Multibootエントリ
│   ├── isr_stubs.asm      # 割り込みラッパー
│   └── gdt.c              # GDT/TSSセットアップ
├── cmd/moon_kernel/
│   ├── main.mbt           # MoonBit main package entry
│   └── moon.pkg           # is-main package 設定
├── runtime/
│   ├── runtime_stubs.c    # malloc, free, memcpy, memset, abort
│   ├── moon_kernel_ffi.c  # MoonBit <-> C 文字列ブリッジ
│   └── moon_kernel_ffi_host.c # MoonBit native codegen 用FFIスタブ
├── drivers/
│   ├── vga.c              # VGAテキストモードドライバ
│   └── serial.c           # COM1シリアル出力
└── kernel/
    ├── main.c             # Cカーネルエントリ (Phase 0)
    ├── moon_entry.c       # MoonBit呼び出しエントリ (Phase 1)
    ├── fmt.c              # 最小フォーマット補助
    ├── console.mbt        # 高レベル出力抽象化（将来）
    ├── alloc.mbt          # メモリアロケータ（後半フェーズ）
    ├── interrupts.mbt     # 割り込みディスパッチ
    └── proc.mbt           # プロセス管理（後半フェーズ）
```

## 結論

このプロジェクトは実現可能であり、**MoonBitのOSカーネルは世界初**の試みとなる。最も近い先例は、RISC-VマイクロコントローラでMoonBitを実行する公式ESP32-C3デモで、Cバックエンドが制約のある環境で動作することを証明している。3つの要因がこれを予想以上に取り組みやすくしている: Perceus参照カウントがカーネル内トレーシングGC問題を完全に排除すること、`extern "C"`と`native-stub`ファイルによるMoonBitのC FFIがクリーンなアセンブリ相互運用を提供すること、そして`extern type`（`void*`にマップされ、RC管理対象外）がMMIOレジスタやハードウェアバッファへのポインタアクセスを提供すること。最も難しい未解決の問題は`moonbit.h`と`runtime.c`のフリースタンディングコンパイルへの適応——これらのファイルのホスト環境依存を調査しスタブを提供する必要がある。そこから始めて、"Hello Bare Metal"を動作させれば、以降の各フェーズはOSDevコミュニティの数十年の知識で十分にカバーされている領域だ。
