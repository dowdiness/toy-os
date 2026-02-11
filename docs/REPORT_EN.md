# Building a toy OS with MoonBit's C backend

**MoonBit can compile to freestanding C code suitable for bare-metal x86 development, but it requires providing a custom memory allocator and runtime stubs.** The critical insight: MoonBit's C backend uses **Perceus reference counting** — not a tracing garbage collector — which means it only needs `malloc`/`free` to function, making it far more OS-friendly than initially expected. Combined with GRUB Multiboot for booting, QEMU for testing, and an i686-elf cross-compiler on WSL2, you can build a working kernel pipeline from MoonBit source to bare-metal x86 execution. This report provides the complete roadmap from your current "Hello World" bootloader to a minimal OS with MoonBit userland applications.

---

## MoonBit's C backend is surprisingly OS-friendly

MoonBit's compilation pipeline for the native C backend follows this path: MoonBit source → Core IR (ANF-style) → MCore IR (monomorphization, generics removed) → CLambda IR (closures removed, RC intrinsics inserted) → **C source output** → GCC/Clang → native binary. You trigger this with `moon build --target native`, and the generated `.c` files land in `target/native/release/build/<package>/`.

The most important discovery for OS development is MoonBit's memory management strategy. **MoonBit does NOT use Boehm GC or any tracing collector for its C backend.** It uses compiler-optimized reference counting inspired by the **Perceus algorithm** (Microsoft Research, Koka language). The compiler inserts precise `moonbit_incref` and `moonbit_decref` calls at compile time. Objects are freed the instant their reference count hits zero — no heap scanning, no stop-the-world pauses, no root set enumeration. This is deterministic and predictable, ideal properties for kernel code.

The runtime footprint is small: a header file (`moonbit.h` at `~/.moon/include/`) defining object layout and type mappings, plus a runtime C file (`$MOON_HOME/lib/runtime.c`). Every heap object carries an object header with a reference count field, positioned *behind* the data pointer (as of the February 2025 ABI change, MoonBit objects point to their first data field, not the header). The type ABI maps cleanly to C primitives:

| MoonBit type | C representation | Notes |
|---|---|---|
| `Int`, `UInt` | `int32_t`, `uint32_t` | Direct value types |
| `Int64`, `UInt64` | `int64_t`, `uint64_t` | Direct value types |
| `Float` / `Double` | `float` / `double` | Standard IEEE 754 |
| `Bool` | `int32_t` | 0 or 1 |
| `Byte` | `uint8_t` | Direct |
| `Bytes` | `uint8_t*` | Points into GC-managed object |
| `FixedArray[T]` | `T*` | Contiguous, GC-managed |
| `String` | `uint16_t*` (UTF-16) | Complex RC-managed object |
| `extern type T` | `void*` | NOT RC-managed — perfect for MMIO |
| `FuncRef[T]` | C function pointer | Capture-free — usable for ISR callbacks |

The build system supports custom compiler flags via `moon.pkg.json`:

```json
{
  "supported-targets": ["native"],
  "native-stub": ["kernel_stubs.c"],
  "link": {
    "native": {
      "cc": "i686-elf-gcc",
      "cc-flags": "-ffreestanding -O2 -fno-stack-protector",
      "cc-link-flags": "-nostdlib -T linker.ld -lgcc"
    }
  }
}
```

**The core challenge is that MoonBit has no official `--freestanding` mode.** The runtime expects `malloc`/`free` to exist, and `println` needs some output mechanism. You must provide these yourself. But the ESP32-C3 precedent — where MoonBit runs Conway's Game of Life on a RISC-V microcontroller via ESP-IDF — proves the native C backend works in constrained environments. The difference: ESP-IDF provides a full C library including `malloc`. For a bare-metal OS kernel, **you become the provider** of these primitives.

---

## The two-stage build pipeline that makes this work

The key architectural insight is that MoonBit's compilation and your kernel's compilation are **two separate stages**. MoonBit generates C source files; your cross-compilation toolchain turns those C files into bare-metal object code. Here's the complete pipeline:

```
Stage 1 (on host):  MoonBit source → moon build → C files
Stage 2 (cross):    C files + boot.s + stubs.c → i686-elf-gcc → .o files → linker → kernel.elf
Boot:               GRUB loads kernel.elf via Multiboot → kernel_main()
```

### Setting up the i686-elf cross-compiler on WSL2

Your Ryzen 7 6800H with 8 cores makes this build fast (~15 minutes). Install prerequisites and build from source:

```bash
sudo apt update && sudo apt install build-essential bison flex libgmp3-dev \
  libmpc-dev libmpfr-dev texinfo libisl-dev nasm qemu-system-x86 xorriso grub-pc-bin

export PREFIX="$HOME/opt/cross"
export TARGET=i686-elf
export PATH="$PREFIX/bin:$PATH"

# Build Binutils
mkdir -p ~/src && cd ~/src
wget https://ftp.gnu.org/gnu/binutils/binutils-2.42.tar.gz
tar xzf binutils-2.42.tar.gz
mkdir build-binutils && cd build-binutils
../binutils-2.42/configure --target=$TARGET --prefix="$PREFIX" \
  --with-sysroot --disable-nls --disable-werror
make -j$(nproc) && make install && cd ..

# Build GCC (C only, no hosted libraries)
wget https://ftp.gnu.org/gnu/gcc/gcc-14.1.0/gcc-14.1.0.tar.gz
tar xzf gcc-14.1.0.tar.gz
cd gcc-14.1.0 && contrib/download_prerequisites && cd ..
mkdir build-gcc && cd build-gcc
../gcc-14.1.0/configure --target=$TARGET --prefix="$PREFIX" \
  --disable-nls --enable-languages=c --without-headers
make -j$(nproc) all-gcc all-target-libgcc
make install-gcc install-target-libgcc
```

Alternatively, use a Docker image: `docker run -it -v "/home/$USER:/root" --rm alessandromrc/i686-elf-tools`.

### The Makefile that ties it all together

```makefile
CROSS    = $(HOME)/opt/cross/bin/i686-elf-
CC       = $(CROSS)gcc
AS       = $(CROSS)as
NASM     = nasm
CFLAGS   = -std=gnu11 -ffreestanding -O2 -Wall -fno-stack-protector -fno-exceptions
LDFLAGS  = -ffreestanding -nostdlib -T linker.ld -lgcc

# MoonBit-generated C (copied from moon build output)
MOONBIT_C = moonbit_output.c
# Your kernel support C files
SUPPORT_C = runtime_stubs.c serial.c vga.c alloc.c idt.c
ASM_SRC   = boot.s
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

## Phase-by-phase development roadmap

Based on the OSDev wiki Bare Bones tutorial, Philipp Oppermann's "Writing an OS in Rust" structure, and the Zig Bare Bones approach, here is the complete milestone progression adapted for MoonBit:

### Phase 0: Toolchain and "C-on-bare-metal" proof of concept (Week 1)

Before touching MoonBit, prove you can get **plain C** running on bare metal. This eliminates a variable when debugging the MoonBit integration later.

Write a Multiboot-compliant assembly boot stub (`boot.s`) that sets up a 16 KiB stack and calls `kernel_main`. The **Multiboot specification** is essential here — GRUB (and QEMU's `-kernel` flag) can load any ELF binary with a Multiboot header, handling the Real Mode → Protected Mode transition, A20 line, and memory detection for you. Your boot assembly needs only ~30 lines:

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
    push %ebx          /* Multiboot info pointer */
    push %eax          /* Multiboot magic */
    call kernel_main
    cli
1:  hlt
    jmp 1b
```

Write a minimal C kernel that writes directly to VGA text buffer at **0xB8000** (80×25 grid, 2 bytes per character: ASCII byte + color attribute byte). Create the linker script placing the kernel at 1 MiB with the Multiboot header in the first 8 KiB. Verify with `grub-file --is-x86-multiboot kernel.elf` and test with `qemu-system-i386 -kernel kernel.elf`.

### Phase 1: MoonBit "Hello Bare Metal" (Weeks 2–3)

Now integrate MoonBit. The goal: get MoonBit-generated C code to print text on screen via VGA.

**Step 1: Create a minimal MoonBit kernel package.** Write your `kernel_main` equivalent in MoonBit using `extern "C"` FFI to call your C VGA driver:

```moonbit
// kernel/main.mbt
extern "C" fn vga_puts(s : Bytes) -> Unit = "vga_puts_wrapper"
extern "C" fn serial_puts(s : Bytes) -> Unit = "serial_puts_wrapper"

pub fn kernel_entry() -> Unit {
  vga_puts(b"MoonBit OS booting...\n")
  serial_puts(b"Serial debug output working\n")
}
```

**Step 2: Provide runtime stubs.** MoonBit's generated C will call `malloc`, `free`, `memcpy`, `memset`, and possibly `abort`. Create `runtime_stubs.c`:

```c
#include <stddef.h>
#include <stdint.h>

// Bump allocator (Phase 1 — no free)
static uint8_t heap[4 * 1024 * 1024];  // 4 MiB static heap
static size_t heap_offset = 0;

void* malloc(size_t size) {
    size = (size + 7) & ~7;  // 8-byte align
    if (heap_offset + size > sizeof(heap)) return 0;
    void* ptr = &heap[heap_offset];
    heap_offset += size;
    return ptr;
}
void free(void* ptr) { (void)ptr; }  // No-op for bump allocator
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

**Step 3: Build and link.** Run `moon build --target native` to generate C files. Copy the generated `.c` files, compile them with `i686-elf-gcc -ffreestanding -nostdlib`, link with your boot assembly and stubs. The tricky part is including `moonbit.h` — you may need to modify it to remove any `#include <stdio.h>` or similar hosted headers, replacing them with your freestanding equivalents.

**Step 4: Serial output for debugging.** Implement a COM1 serial driver (port **0x3F8**) — it's only ~20 lines of C using `outb`/`inb` inline assembly. QEMU's `-serial stdio` flag redirects serial output to your terminal, giving you `printf`-equivalent debugging without implementing a full VGA driver.

### Phase 2: GDT, IDT, and interrupts (Weeks 4–6)

With text output working, build the interrupt infrastructure. This is mostly C/assembly work, with MoonBit handling the high-level logic.

**GDT setup**: Define 5 segment descriptors (null, kernel code, kernel data, user code, user data) plus a TSS entry. Load with `lgdt` instruction in a small assembly helper.

**IDT and ISR stubs**: Create 256 IDT entries. Each of the 32 CPU exceptions and 16 hardware IRQs needs an assembly stub that saves all registers (`pushad`), calls a C dispatch function, restores registers (`popad`), and returns with `iret` (not `ret`). The C dispatch function can then call MoonBit handlers:

```nasm
; isr_stubs.asm — NASM macro generates all 48 stubs
%macro ISR_NOERR 1
global isr%1
isr%1:
    push dword 0        ; dummy error code
    push dword %1       ; interrupt number
    jmp isr_common
%endmacro

isr_common:
    pushad
    cld
    push esp
    call interrupt_dispatch  ; C function → calls MoonBit handler
    add esp, 4
    popad
    add esp, 8
    iret
```

**PIC remapping**: Remap IRQs 0–15 from vectors 0–15 to vectors **32–47** (avoiding collision with CPU exceptions). Then enable the timer (IRQ 0 → vector 32) and keyboard (IRQ 1 → vector 33).

**Keyboard driver in MoonBit**: This is your first real MoonBit kernel module. Use `extern "C"` to call `inb(0x60)` for reading scancodes, then implement a scancode-to-ASCII lookup table using MoonBit's pattern matching — an elegant use case for the language.

### Phase 3: Memory management (Weeks 7–10)

Replace the bump allocator with real memory management. This is the most critical phase for MoonBit integration because **Perceus reference counting calls `free()` constantly** — every object whose refcount drops to zero triggers an immediate deallocation.

**Physical memory manager**: Parse the Multiboot memory map (passed by GRUB in the `ebx` register at boot) to discover available RAM. Implement a bitmap allocator tracking 4 KiB physical page frames. MoonBit's `FixedArray[UInt]` maps directly to `uint32_t*` in C, making bitmap operations natural.

**Paging**: Set up two-level page tables (page directory + page tables) for virtual memory. Identity-map the kernel region and the VGA buffer. Use the `CR3` register to load the page directory address. This requires small assembly helpers for `CR3` read/write, `invlpg` (TLB flush), and enabling the paging bit in `CR0`.

**Free-list heap allocator**: Replace the bump allocator with a proper first-fit free-list allocator that supports real `free()`. Each allocated block gets an 8-byte header (size + free flag + next pointer). This is sufficient for MoonBit's reference counting to function correctly. A 4 MiB initial heap is reasonable for a toy OS.

```c
typedef struct block_header {
    size_t size;
    int is_free;
    struct block_header* next;
} block_header_t;
```

**Why Perceus RC is better than tracing GC here**: A tracing GC would need to walk the stack and scan the heap to find roots — nightmare-level complexity in a kernel where interrupt handlers can fire at any point and the stack layout is non-standard. Reference counting just needs `malloc`/`free`. The only caveat is **reference cycles won't be detected** — MoonBit's functional design minimizes cycles, but you must be careful with kernel data structures (e.g., doubly-linked process lists).

### Phase 4: Process management and scheduling (Weeks 11–14)

**Process structure**: Define a process control block with PID, page directory pointer, kernel stack, saved register state, and scheduling state. MoonBit's algebraic data types are excellent here:

```moonbit
enum ProcessState {
  Ready
  Running
  Blocked(BlockReason)
  Terminated(Int)  // exit code
}
```

**Context switching**: Save/restore all CPU registers and switch page directories. The context switch itself must be in assembly (~20 lines saving/restoring `esp`, `ebp`, and general registers, plus swapping `CR3` for address space switches).

**Round-robin scheduler**: Maintain a linked list of ready processes. On each timer interrupt (100 Hz from the PIT), save the current process state and switch to the next ready process. MoonBit's pattern matching makes the scheduler logic clean.

### Phase 5: Ring 3 userland (Weeks 15–18)

**TSS configuration**: Set `esp0` (kernel stack) and `ss0` (kernel data segment) in the Task State Segment. The CPU uses these automatically when transitioning from Ring 3 → Ring 0 on an interrupt.

**Entering Ring 3**: Use the `iret` trick — push a fake interrupt frame with Ring 3 segment selectors (RPL=3) and the user program's entry point, then execute `iret`. The CPU transitions to Ring 3 and begins executing user code.

**System calls**: Install a handler on `int 0x80`. User programs invoke system calls by loading a syscall number into `eax`, arguments into `ebx`/`ecx`/`edx`, and executing `int 0x80`. The kernel handler dispatches to the appropriate function. This is where MoonBit really shines — a syscall dispatch table using pattern matching on the syscall number.

**ELF loader**: Parse simple ELF binaries, map their segments into user address space, and jump to the entry point. User programs could initially be statically linked MoonBit programs compiled for the OS's syscall ABI.

### Phase 6: Simple shell and user programs (Weeks 19–22)

**RAM filesystem**: Create a simple ramdisk with flat file entries (name + data pointer + size). Load initial programs into the ramdisk at boot via Multiboot modules (GRUB can load additional files alongside the kernel).

**Shell**: Read keyboard input line by line, parse commands, fork/exec programs from the ramdisk. Commands like `echo`, `help`, `ps` (list processes), and `clear` make the OS feel alive.

**MoonBit userland applications**: The ultimate goal — compile MoonBit programs targeting your OS's syscall interface. Each user program would be a MoonBit binary compiled to C, then cross-compiled with your OS-specific syscall stubs replacing the hosted libc calls. The Perceus RC in user programs would call `malloc`/`free` which invoke your OS's `brk`/`sbrk` syscall for heap management.

---

## Hardware options beyond QEMU

QEMU should be your primary development environment — **90% of hobby OS work happens in emulators**. The workflow is unbeatable: edit code, `make run`, see results instantly, with full GDB debugging via `qemu-system-i386 -s -S`. But QEMU is "too loose" — it zeros memory (masking uninitialized variable bugs), ignores cache control bits, and can be 200× faster than real hardware for certain operations.

For real hardware testing, the best options ranked by value:

- **Used thin client ($50–$70)**: An HP T730 or T620 from eBay is the OSDev community's top recommendation. These have **native serial ports** (critical — a serial driver is 30 lines of code vs. hundreds for USB), PS/2 ports, SMP toggle in BIOS, and full x86-64 CPUs. This is by far the best option.
- **Radxa X4 (~$60)**: The cheapest new x86 SBC, with an Intel N100 quad-core, 4 GB LPDDR5, and a Raspberry Pi form factor. It lacks a native serial port (needs USB-UART adapter), but has UEFI/BIOS with Multiboot support.
- **LattePanda V1 (~$90–$120)**: Classic x86 SBC with Intel Atom, includes an Arduino co-processor for GPIO. Older but proven.
- **ZimaBlade (~$80)**: Intel Celeron with upgradeable SO-DIMM RAM and SATA ports. Designed for NAS but works for OS dev.

RISC-V boards are temptingly cheap (**Milk-V Duo at $5**, VisionFive 2 at $55) and have a cleaner ISA (236-page spec vs. 2,000+ for x86), but the OSDev ecosystem is immature. Fewer tutorials exist, firmware varies between boards, and QEMU RISC-V emulation is less mature. **Start with x86, consider RISC-V as a second target** once your OS architecture is stable — MoonBit's C backend makes retargeting to a different architecture mainly a matter of swapping the cross-compiler and assembly stubs.

---

## What to learn from Rust, Zig, and Go OS projects

**Philipp Oppermann's "Writing an OS in Rust"** (17,300 GitHub stars) is the gold-standard tutorial structure. Its phased approach — freestanding binary → Multiboot boot → VGA text → CPU exceptions → hardware interrupts → paging → heap allocation → allocator designs → async multitasking — maps directly to the MoonBit roadmap above. The second edition eliminated all C dependencies using Rust's `global_asm!` and a custom `bootloader` crate. Key lesson: encapsulate all `unsafe` hardware access behind safe abstractions.

**Zig's bare-metal OS projects** (Pluto/ZystemOS with 660 stars, OSDev Wiki Zig Bare Bones tutorial) demonstrate a pattern very similar to what MoonBit can do. Zig uses `-target freestanding` to compile without OS assumptions, custom linker scripts, and inline assembly for hardware access. The Zig approach of disabling SIMD/FPU features in kernel mode (`-mno-sse -mno-mmx`) applies directly — add `-mno-sse -mno-mmx -mgeneral-regs-only` to your `cc-flags`.

**MIT's xv6** (RISC-V) is the best reference architecture for kernel structure. At only 99 pages of formatted source, it implements processes, virtual memory, file system, system calls, scheduling, and a shell. Its module structure maps cleanly to MoonBit packages: `kalloc.c` → `alloc.mbt`, `proc.c` → `proc.mbt`, `trap.c` → `interrupts.mbt`, `vm.c` → `paging.mbt`.

**Biscuit (Go kernel, MIT OSDI'18)** provides the strongest cautionary lesson for GC'd languages in kernels: GC pauses during interrupt handlers caused mouse cursor lag. However, MoonBit's Perceus RC avoids this entirely — deallocation is deterministic and incremental, happening inline with normal execution rather than in stop-the-world phases.

---

## The critical path to first MoonBit output on bare metal

The **minimal viable path** to get MoonBit code producing visible output on bare metal requires exactly these components:

1. **Assembly boot stub** (30 lines) — Multiboot header + stack setup + `call kernel_main`
2. **Linker script** (20 lines) — Place kernel at 1 MiB, Multiboot header first
3. **Runtime stubs** (50 lines) — `malloc` (bump allocator), `free` (no-op), `memcpy`, `memset`, `abort`
4. **VGA driver in C** (20 lines) — Write characters to `0xB8000`
5. **MoonBit kernel entry** (10 lines) — `extern "C"` FFI call to VGA driver
6. **MoonBit's `moonbit.h` + `runtime.c`** — Modified for freestanding (remove hosted `#include`s)
7. **i686-elf cross-compiler** — For compiling everything to bare-metal x86

Total custom code: roughly **150 lines of C/assembly** plus your MoonBit source. This is a weekend project to reach the "Hello Bare Metal" milestone. From there, every subsequent phase adds capability incrementally.

The project structure should follow this layout:

```
moonbit-os/
├── moon.mod.json
├── moon.pkg.json          # native-stub, cc-flags, cc-link-flags
├── Makefile               # Two-stage build: moon build → cross-compile
├── linker.ld
├── arch/x86/
│   ├── boot.s             # Multiboot entry
│   ├── isr_stubs.asm      # Interrupt wrappers
│   └── gdt.c              # GDT/TSS setup
├── runtime/
│   ├── runtime_stubs.c    # malloc, free, memcpy, memset, abort
│   ├── moonbit.h          # Modified for freestanding
│   └── runtime.c          # MoonBit runtime (from $MOON_HOME/lib/)
├── drivers/
│   ├── vga.c              # VGA text mode driver
│   └── serial.c           # COM1 serial output
└── kernel/
    ├── main.mbt           # Kernel entry point
    ├── console.mbt        # High-level output abstraction
    ├── alloc.mbt          # Memory allocator (later phases)
    ├── interrupts.mbt     # Interrupt dispatch logic
    └── proc.mbt           # Process management (later phases)
```

## Conclusion

This project is feasible and would be a first-of-its-kind effort — **no MoonBit OS kernel exists yet**. The closest precedent is the official ESP32-C3 demo running MoonBit on a RISC-V microcontroller, which proves the C backend works in constrained environments. Three factors make this more tractable than expected: Perceus reference counting eliminates the tracing-GC-in-a-kernel problem entirely, MoonBit's C FFI with `extern "C"` and `native-stub` files provides clean assembly interop, and the `extern type` (mapped to `void*`, not RC-managed) gives you raw pointer access for MMIO registers and hardware buffers. The hardest unsolved question is adapting `moonbit.h` and `runtime.c` for freestanding compilation — this requires inspecting these files for hosted-environment dependencies and providing stubs. Start there, get "Hello Bare Metal" working, and every subsequent phase is well-charted territory from decades of OSDev community knowledge.