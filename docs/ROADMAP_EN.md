# MoonBit Toy OS — Implementation Roadmap

> Implementation plan for building a bare-metal x86 kernel using MoonBit's C backend.
> Target: QEMU (qemu-system-i386) → Real hardware later (used thin clients, etc.)

---

## Architecture Overview

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

### Key Technical Assumptions

- MoonBit's C backend uses **Perceus reference counting** (not a tracing GC)
- The runtime only requires `malloc`/`free` → well-suited for kernel development
- `extern "C"` FFI enables interop with C/assembly
- `extern type` maps to `void*` and is NOT ref-counted → ideal for MMIO / hardware buffers

---

## Project Structure

```
moonbit-os/
├── moon.mod.json
├── Makefile                    # Two-stage build: moon build → cross-compile
├── linker.ld                   # Kernel linker script
├── arch/x86/
│   ├── boot.s                  # Multiboot entry point
│   ├── isr_stubs.asm           # Interrupt handler wrappers (NASM)
│   └── gdt.c                   # GDT/TSS setup
├── runtime/
│   ├── runtime_stubs.c         # malloc, free, memcpy, memset, abort
│   ├── moonbit.h               # MoonBit header modified for freestanding
│   └── runtime.c               # MoonBit runtime (copied from $MOON_HOME/lib/)
├── drivers/
│   ├── vga.c                   # VGA text mode driver
│   ├── serial.c                # COM1 serial output
│   └── keyboard.c              # PS/2 keyboard driver
└── kernel/
    ├── main.mbt                # Kernel entry point
    ├── moon.pkg.json           # native-stub, cc-flags config
    ├── console.mbt             # High-level output abstraction
    ├── alloc.mbt               # Memory allocator (Phase 3)
    ├── interrupts.mbt          # Interrupt dispatch logic (Phase 2)
    └── proc.mbt                # Process management (Phase 4)
```

---

## Phase 0: Toolchain Setup + Bare-Metal C Proof of Concept

**Goal**: Verify that plain C code can produce VGA output on bare metal, without MoonBit.

### Task 0.1: Build i686-elf Cross-Compiler on WSL2

```bash
# Prerequisites
sudo apt update && sudo apt install build-essential bison flex libgmp3-dev \
  libmpc-dev libmpfr-dev texinfo libisl-dev nasm qemu-system-x86 xorriso grub-pc-bin

# Cross-compiler build
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

# GCC (C only)
cd ~/src
wget https://ftp.gnu.org/gnu/gcc/gcc-14.1.0/gcc-14.1.0.tar.gz
tar xzf gcc-14.1.0.tar.gz && cd gcc-14.1.0 && contrib/download_prerequisites && cd ..
mkdir build-gcc && cd build-gcc
../gcc-14.1.0/configure --target=$TARGET --prefix="$PREFIX" \
  --disable-nls --enable-languages=c --without-headers
make -j$(nproc) all-gcc all-target-libgcc
make install-gcc install-target-libgcc
```

**Verification**: `i686-elf-gcc --version` runs successfully.

### Task 0.2: Multiboot Boot Stub (boot.s)

```gas
# arch/x86/boot.s
.set MAGIC,    0x1BADB002
.set FLAGS,    (1<<0 | 1<<1)          # Page-align + memory map request
.set CHECKSUM, -(MAGIC + FLAGS)

.section .multiboot
.align 4
.long MAGIC
.long FLAGS
.long CHECKSUM

.section .bss
.align 16
stack_bottom: .skip 16384             # 16 KiB kernel stack
stack_top:

.section .text
.global _start
_start:
    mov $stack_top, %esp
    push %ebx                          # Multiboot info pointer
    push %eax                          # Multiboot magic number
    call kernel_main
    cli
1:  hlt
    jmp 1b
```

### Task 0.3: Linker Script (linker.ld)

```ld
/* linker.ld */
ENTRY(_start)
SECTIONS {
    . = 1M;                            /* Kernel loaded at 1 MiB */

    .text BLOCK(4K) : ALIGN(4K) {
        *(.multiboot)                  /* Multiboot header placed first */
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

### Task 0.4: VGA Text Output (C)

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
    if (vga_row >= VGA_HEIGHT) vga_row = 0; // Simple wrap (no scroll)
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

### Task 0.5: Makefile + Run on QEMU

```makefile
CROSS    = $(HOME)/opt/cross/bin/i686-elf-
CC       = $(CROSS)gcc
AS       = $(CROSS)as
LD       = $(CROSS)ld
CFLAGS   = -std=gnu11 -ffreestanding -O2 -Wall -Wextra -fno-stack-protector
LDFLAGS  = -T linker.ld -nostdlib

all: kernel.elf

kernel.elf: boot.o vga.o
	$(CC) $(LDFLAGS) $^ -o $@ -lgcc

boot.o: arch/x86/boot.s
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

**Verification**:
- `make run` launches QEMU and displays "Hello from bare metal C!"
- `grub-file --is-x86-multiboot kernel.elf` passes without errors

---

## Phase 1: MoonBit "Hello Bare Metal"

**Goal**: MoonBit-authored code outputs text to VGA on bare metal.

### Task 1.1: Investigate MoonBit Runtime

Examine MoonBit's runtime files and identify modifications needed for a freestanding environment.

```bash
# Locate MoonBit runtime files
ls ~/.moon/include/moonbit.h
ls ~/.moon/lib/runtime.c  # or nearby

# Find hosted-environment #include directives
grep -n '#include' ~/.moon/include/moonbit.h
grep -n '#include' ~/.moon/lib/runtime.c

# Find external dependencies: malloc/free/memcpy etc.
grep -n 'malloc\|free\|memcpy\|memset\|abort\|printf\|fprintf\|exit' ~/.moon/lib/runtime.c
```

**Deliverable**: A list of changes required for freestanding compilation.

### Task 1.2: Runtime Stubs Implementation

```c
// runtime/runtime_stubs.c
#include <stddef.h>
#include <stdint.h>

// === Bump Allocator (Phase 1: free is a no-op) ===
static uint8_t heap[4 * 1024 * 1024];  // 4 MiB static heap
static size_t heap_offset = 0;

void* malloc(size_t size) {
    size = (size + 7) & ~7;  // 8-byte align
    if (heap_offset + size > sizeof(heap)) return (void*)0;
    void* ptr = &heap[heap_offset];
    heap_offset += size;
    return ptr;
}

void free(void* ptr) { (void)ptr; }  // No-op in Phase 1

void* calloc(size_t n, size_t s) {
    void* p = malloc(n * s);
    if (p) memset(p, 0, n * s);
    return p;
}

void* realloc(void* ptr, size_t new_size) {
    (void)ptr;
    return malloc(new_size);  // Always allocate fresh in Phase 1
}

// === Minimal libc ===
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

### Task 1.3: Serial Output Driver (for debugging)

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
    outb(COM1 + 1, 0x00);  // Disable interrupts
    outb(COM1 + 3, 0x80);  // Enable DLAB
    outb(COM1 + 0, 0x03);  // 38400 baud
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x03);  // 8N1
    outb(COM1 + 2, 0xC7);  // Enable FIFO
    outb(COM1 + 4, 0x0B);  // Enable RTS/DSR
}

void serial_putchar(char c) {
    while ((inb(COM1 + 5) & 0x20) == 0);  // Wait until transmit ready
    outb(COM1, c);
}

void serial_puts(const char* s) {
    while (*s) serial_putchar(*s++);
}
```

**QEMU usage**: `qemu-system-i386 -kernel kernel.elf -serial stdio`

### Task 1.4: MoonBit Kernel Package

```json
// moon.pkg.json
{
  "supported-targets": ["native"],
  "native-stub": [
    "runtime/runtime_stubs.c",
    "drivers/vga.c",
    "drivers/serial.c"
  ],
  "link": {
    "native": {
      "cc": "i686-elf-gcc",
      "cc-flags": "-ffreestanding -O2 -Wall -fno-stack-protector -mno-sse -mno-mmx",
      "cc-link-flags": "-nostdlib -T linker.ld -lgcc"
    }
  }
}
```

```moonbit
// kernel/main.mbt

// C function FFI declarations
extern "C" fn c_vga_clear() = "vga_clear"
extern "C" fn c_vga_puts(s : Bytes) = "vga_puts_bytes"
extern "C" fn c_serial_init() = "serial_init"
extern "C" fn c_serial_puts(s : Bytes) = "serial_puts_bytes"

// Kernel entry point (called from boot.s)
pub fn kernel_entry() -> Unit {
  c_serial_init()
  c_serial_puts(b"MoonBit OS: Serial initialized\n")
  c_vga_clear()
  c_vga_puts(b"MoonBit OS v0.1\n")
  c_vga_puts(b"Hello from MoonBit on bare metal!\n")
}
```

### Task 1.5: C Wrapper Functions (Bytes type handling)

MoonBit's `Bytes` is a pointer to a header-prefixed buffer. C wrappers handle the layout.

```c
// drivers/vga_wrapper.c
#include <stdint.h>

// MoonBit's Bytes type has an object header before the data
// Exact layout must be adjusted by inspecting moonbit.h
extern void vga_puts(const char* s);
extern void serial_puts(const char* s);

void vga_puts_bytes(const uint8_t* bytes) {
    // TODO: Adjust offset based on moonbit.h object layout
    vga_puts((const char*)bytes);
}

void serial_puts_bytes(const uint8_t* bytes) {
    serial_puts((const char*)bytes);
}
```

### Task 1.6: Two-Stage Build Integration

```makefile
# Makefile (extended for Phase 1)
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
	# Verify generated C files
	ls $(MOONBIT_BUILD)/kernel/*.c

# Stage 2: Cross-compile everything
MOONBIT_C = $(wildcard $(MOONBIT_BUILD)/kernel/*.c)
C_SRCS    = drivers/vga.c drivers/serial.c drivers/vga_wrapper.c runtime/runtime_stubs.c
ASM_SRCS  = arch/x86/boot.s

OBJS = boot.o $(notdir $(MOONBIT_C:.c=.o)) $(notdir $(C_SRCS:.c=.o))

kernel.elf: moonbit-gen $(OBJS)
	$(CC) $(LDFLAGS) $(OBJS) -o $@ -lgcc

boot.o: arch/x86/boot.s
	$(AS) --32 $< -o $@

# Rule for MoonBit-generated C files
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

**Verification**:
- QEMU displays "Hello from MoonBit on bare metal!"
- Serial output appears in the terminal
- `moon build` → cross-compile → QEMU launch is fully automated

---

## Phase 2: GDT + IDT + Interrupts

**Goal**: Hardware interrupts (timer, keyboard) work and keyboard input is handled in MoonBit.

### Task 2.1: GDT (Global Descriptor Table)

Define 5 segment descriptors: null, kernel code, kernel data, user code, user data.

```c
// arch/x86/gdt.c
struct gdt_entry { /* 8-byte segment descriptor */ };
struct gdt_ptr   { uint16_t limit; uint32_t base; } __attribute__((packed));

// 5 entries: null, kernel code, kernel data, user code, user data
static struct gdt_entry gdt[5];
static struct gdt_ptr gp;

extern void gdt_flush(uint32_t);  // Add lgdt + far jump to boot.s
void gdt_init(void) { /* Set entries + call gdt_flush */ }
```

### Task 2.2: IDT (Interrupt Descriptor Table) + PIC Remapping

- Define 256 IDT entries
- Remap PIC (8259): IRQ 0-15 → vectors 32-47
- Separate CPU exceptions (0-31) from hardware IRQs (32-47)

### Task 2.3: ISR Stubs (NASM)

```nasm
; arch/x86/isr_stubs.asm
%macro ISR_NOERR 1
global isr%1
isr%1:
    push dword 0          ; Dummy error code
    push dword %1         ; Interrupt number
    jmp isr_common
%endmacro

%macro ISR_ERR 1
global isr%1
isr%1:
    push dword %1         ; Error code already pushed by CPU
    jmp isr_common
%endmacro

isr_common:
    pushad
    cld
    push esp              ; Pass registers_t* as argument
    call interrupt_dispatch
    add esp, 4
    popad
    add esp, 8            ; Clean up error code + interrupt number
    iret
```

### Task 2.4: Interrupt Dispatch (MoonBit)

```moonbit
// kernel/interrupts.mbt

// Called from C dispatch function
pub fn handle_interrupt(num : Int) -> Unit {
  match num {
    32 => handle_timer()       // IRQ 0: PIT timer
    33 => handle_keyboard()    // IRQ 1: Keyboard
    _  => {
      // Unhandled interrupt
      c_serial_puts(b"Unhandled interrupt\n")
    }
  }
}

fn handle_timer() -> Unit {
  // Increment tick counter, etc.
}

fn handle_keyboard() -> Unit {
  let scancode = c_inb(0x60)
  let ch = scancode_to_ascii(scancode)
  // Display character on VGA
}
```

### Task 2.5: PS/2 Keyboard Driver

Implement scancode → ASCII lookup table using MoonBit's pattern matching.

**Verification**:
- Timer interrupts fire periodically (tick count output via serial)
- Keyboard input is displayed on VGA screen
- No double faults / crashes

---

## Phase 3: Memory Management

**Goal**: Physical memory management + paging + proper heap allocator (Perceus RC works correctly).

### Task 3.1: Parse Multiboot Memory Map

Parse the memory map passed by GRUB via the EBX register to identify available RAM regions.

### Task 3.2: Physical Memory Manager (Bitmap)

Bitmap allocator for 4 KiB physical page frames.
MoonBit's `FixedArray[UInt]` maps directly to C's `uint32_t*`, making bit operations natural.

### Task 3.3: Paging (Two-Level Page Tables)

- Page directory + page table setup
- Identity-map the kernel region and VGA buffer
- Assembly helpers for CR3 register read/write

### Task 3.4: Free-List Heap Allocator

**Most critical task**: Perceus reference counting calls `free()` frequently, so a proper allocator is essential.

```c
// runtime/alloc.c
typedef struct block_header {
    size_t size;
    int is_free;
    struct block_header* next;
} block_header_t;

// First-fit free-list allocator
void* malloc(size_t size);  // Find suitable block from free list
void  free(void* ptr);      // Return block to free list + coalesce adjacent blocks
```

**Note**: Reference cycles are NOT detected by Perceus. Kernel data structures (doubly-linked lists, etc.) must handle this manually.

**Verification**:
- malloc/free work correctly (allocate → free → re-allocate test)
- Page faults are handled properly
- MoonBit object allocation/deallocation doesn't leak (monitor heap usage via serial)

---

## Phase 4: Process Management + Scheduling

**Goal**: Multiple processes switch via round-robin scheduling.

### Task 4.1: Process Control Block

```moonbit
// kernel/proc.mbt
enum ProcessState {
  Ready
  Running
  Blocked(BlockReason)
  Terminated(Int)  // exit code
}

struct Process {
  pid : Int
  state : ProcessState
  // kernel_stack, page_directory, saved_regs managed on C side
}
```

### Task 4.2: Context Switch (Assembly)

Save/restore all registers + CR3 switch (address space switch). ~20 lines of assembly.

### Task 4.3: Round-Robin Scheduler

Process switch on PIT timer interrupt (100 Hz).
Scheduler logic written with MoonBit's pattern matching.

**Verification**:
- Two or more kernel tasks alternate execution
- Registers are correctly restored after context switch

---

## Phase 5: Ring 3 Userland

**Goal**: Programs execute in Ring 3 (user mode).

### Task 5.1: TSS Configuration

Set TSS esp0/ss0 for stack switching during Ring 3 → Ring 0 transitions.

### Task 5.2: Transition to Ring 3

Use the `iret` trick with Ring 3 segment selectors (RPL=3) to enter user mode.

### Task 5.3: System Calls (int 0x80)

```moonbit
// kernel/syscall.mbt
pub fn syscall_dispatch(num : Int, arg1 : Int, arg2 : Int, arg3 : Int) -> Int {
  match num {
    1 => sys_exit(arg1)
    2 => sys_write(arg1, arg2, arg3)
    3 => sys_read(arg1, arg2, arg3)
    _ => -1  // ENOSYS
  }
}
```

### Task 5.4: Simple ELF Loader

Map ELF binary segments into user address space and jump to the entry point.

**Verification**:
- User mode programs can output text via syscalls
- Invalid memory access is caught as a page fault

---

## Phase 6: Shell + MoonBit User Applications

**Goal**: MoonBit-authored user applications run from a shell.

### Task 6.1: RAM Filesystem

Flat ramdisk loading initial programs as Multiboot modules.

### Task 6.2: Shell

Read keyboard input line by line and execute commands.
Built-in commands: `echo`, `help`, `ps`, `clear`, etc.

### Task 6.3: MoonBit Userland Applications

Compile MoonBit programs with OS-specific syscall stubs and execute in userland.

**Verification**:
- Enter commands in the shell and see results
- MoonBit user programs output text via syscalls
- `ps` shows process listing

---

## Hardware Options (Beyond QEMU)

Do 90% of development on QEMU; test on real hardware in later phases.

### x86 Options

| Option | Price | Notes |
|--------|-------|-------|
| **Used Thin Client (HP T730 etc.)** | $50-70 | **Best x86 choice.** Native serial port, PS/2, SMP toggle in BIOS |
| **Radxa X4** | ~$60 | Intel N100, Raspberry Pi form factor, cheapest new x86 SBC |
| **LattePanda V1** | ~$90-120 | Intel Atom, includes Arduino co-processor |
| **ZimaBlade** | ~$80 | Intel Celeron, upgradeable SO-DIMM RAM |

### ARM Options (Raspberry Pi)

| Option | Price | Notes |
|--------|-------|-------|
| **Raspberry Pi 4 Model B (2GB)** | ~$35-45 | **Best ARM choice.** Cortex-A72 (ARMv8 64-bit), abundant bare-metal tutorials, QEMU `raspi4b` emulation |
| **Raspberry Pi 3 Model B+** | ~$25-35 | Cortex-A53, most tutorials available, easy to acquire |
| **Raspberry Pi 5** | ~$60-80 | Cortex-A76, high performance but sparse bare-metal docs, UART requires RP1 PCIe bridge |
| **Raspberry Pi Zero 2 W** | ~$15 | Cortex-A53 quad-core, cheapest but no debug headers |

**Recommendation**: Complete x86 development on QEMU first → port to Raspberry Pi 4 (best balance of tutorials and QEMU support).

**Additional hardware needed** (for RPi real hardware testing):
- USB-TTL serial cable (~$5-10) — essential for UART debugging
- microSD card (8GB+)
- (Optional) USB keyboard + HDMI monitor

---

## Appendix A: Porting to Raspberry Pi (ARM)

> Recommended to start after completing Phase 0-2 on x86.
> Since MoonBit's C backend only requires **swapping the cross-compiler**,
> changes to MoonBit source code are minimal.

### x86 vs ARM: What Changes and What Doesn't

**Unchanged** (MoonBit layer):
- `kernel/main.mbt` — Kernel entry point
- `kernel/interrupts.mbt` — Interrupt dispatch logic
- `kernel/proc.mbt` — Process management
- `kernel/alloc.mbt` — Memory allocator logic
- `runtime/runtime_stubs.c` — malloc/free/memcpy etc. (nearly identical)

**Changed** (Architecture layer):
- Boot stub (`boot.s`) — Complete rewrite
- Linker script (`linker.ld`) — Different load address
- Interrupt handlers (`isr_stubs`) — ARM exception model
- VGA driver → UART driver (RPi has no VGA text buffer)
- GDT/IDT → ARM Exception Vector Table
- Port I/O (`outb`/`inb`) → MMIO (Memory-Mapped I/O)
- Cross-compiler: `i686-elf-gcc` → `aarch64-none-elf-gcc`

### ARM Porting Task A.1: Cross-Compiler Setup

```bash
# Install AArch64 bare-metal cross-compiler on WSL2
sudo apt install gcc-aarch64-linux-gnu

# Or download Arm's official toolchain (recommended)
# https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads
# Select "AArch64 ELF bare-metal target (aarch64-none-elf)"
wget https://developer.arm.com/-/media/Files/downloads/gnu/13.3.rel1/binrel/arm-gnu-toolchain-13.3.rel1-x86_64-aarch64-none-elf.tar.xz
tar xf arm-gnu-toolchain-*.tar.xz -C ~/opt/
export PATH="$HOME/opt/arm-gnu-toolchain-13.3.rel1-x86_64-aarch64-none-elf/bin:$PATH"

# QEMU Raspberry Pi 4 emulation
sudo apt install qemu-system-aarch64
# Test: qemu-system-aarch64 -M raspi4b -serial stdio -kernel kernel8.img
```

### ARM Porting Task A.2: Understand Raspberry Pi Boot Process

The Raspberry Pi boot process is fundamentally different from x86 Multiboot:

```
Power ON
  ↓
GPU (VideoCore) executes bootcode.bin
  ↓
GPU loads and executes start.elf
  ↓
start.elf reads config.txt
  ↓
start.elf loads kernel8.img into RAM at 0x80000
  ↓
GPU triggers ARM CPU reset line
  ↓
ARM CPU begins execution at 0x80000 ← Your code starts here
```

**Key differences**:
- No Multiboot header needed — GPU firmware handles bootstrap
- Kernel is a raw binary named `kernel8.img` (not ELF)
- Load address is `0x80000` (not `0x100000` like x86)
- SD card needs FAT32 partition with `bootcode.bin`, `start.elf`, `config.txt`, `kernel8.img`

### ARM Porting Task A.3: Boot Stub (AArch64)

```gas
// arch/arm/boot.s (AArch64)
.section ".text.boot"
.global _start

_start:
    // Check CPU ID — halt all cores except core 0
    mrs x0, mpidr_el1
    and x0, x0, #3
    cbz x0, core0_boot
    // Cores 1-3: infinite wait loop
halt:
    wfe
    b halt

core0_boot:
    // Set stack pointer (kernel loaded at 0x80000)
    ldr x0, =_start
    mov sp, x0

    // Zero out BSS section
    ldr x0, =__bss_start
    ldr x1, =__bss_end
bss_clear:
    cmp x0, x1
    b.ge bss_done
    str xzr, [x0], #8
    b bss_clear
bss_done:

    // Call kernel_main
    bl kernel_main

    // Halt if kernel_main returns
    b halt
```

### ARM Porting Task A.4: Linker Script (RPi)

```ld
/* linker.ld (Raspberry Pi AArch64) */
ENTRY(_start)
SECTIONS {
    . = 0x80000;    /* RPi kernel load address */

    .text : {
        KEEP(*(.text.boot))    /* Boot code placed first */
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

### ARM Porting Task A.5: UART Output (Replaces VGA)

Raspberry Pi has no x86-style `0xB8000` VGA text buffer.
Instead, output goes through UART (PL011) serial. This is the primary output for RPi bare-metal.

```c
// drivers/uart_rpi.c (Raspberry Pi 3/4 Mini UART)
#include <stdint.h>

// BCM2711 (RPi 4) peripheral base address
// For RPi 3: use 0x3F000000
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
    *AUX_ENABLES = 1;        // Enable Mini UART
    *AUX_MU_CNTL_REG = 0;   // Disable TX/RX during setup
    *AUX_MU_IER_REG = 0;    // Disable interrupts
    *AUX_MU_LCR_REG = 3;    // 8-bit mode
    *AUX_MU_MCR_REG = 0;
    *AUX_MU_BAUD_REG = 270;  // 115200 baud

    // Set GPIO 14, 15 to Alternative Function 5 (Mini UART)
    uint32_t ra = *GPFSEL1;
    ra &= ~(7 << 12);  // GPIO 14
    ra |= 2 << 12;     // ALT5
    ra &= ~(7 << 15);  // GPIO 15
    ra |= 2 << 15;     // ALT5
    *GPFSEL1 = ra;

    // Disable pull-up/down
    *GPPUD = 0;
    delay(150);
    *GPPUDCLK0 = (1 << 14) | (1 << 15);
    delay(150);
    *GPPUDCLK0 = 0;

    *AUX_MU_CNTL_REG = 3;   // Enable TX/RX
}

void uart_putchar(char c) {
    while (!(*AUX_MU_LSR_REG & 0x20));  // Wait for TX FIFO space
    *AUX_MU_IO_REG = (uint32_t)c;
}

void uart_puts(const char* s) {
    while (*s) {
        if (*s == '\n') uart_putchar('\r');
        uart_putchar(*s++);
    }
}

char uart_getchar(void) {
    while (!(*AUX_MU_LSR_REG & 0x01));  // Wait for RX data
    return (char)(*AUX_MU_IO_REG & 0xFF);
}
```

### ARM Porting Task A.6: SD Card Preparation

```bash
# Create FAT32 partition on SD card and copy the following files:
# 1. Raspberry Pi firmware (download from GitHub)
#    https://github.com/raspberrypi/firmware/tree/master/boot
#    Required files: bootcode.bin, start.elf, fixup.dat

# 2. Create config.txt
cat > config.txt << 'EOF'
# Boot in AArch64 mode
arm_64bit=1
# Enable Mini UART
enable_uart=1
# Minimize GPU memory
gpu_mem=16
EOF

# 3. Build your kernel
aarch64-none-elf-gcc -ffreestanding -nostdlib -nostartfiles \
  -mgeneral-regs-only -c drivers/uart_rpi.c -o uart_rpi.o
aarch64-none-elf-gcc -ffreestanding -nostdlib -nostartfiles \
  -c arch/arm/boot.s -o boot.o
aarch64-none-elf-gcc -ffreestanding -nostdlib -nostartfiles \
  -T linker.ld boot.o uart_rpi.o -o kernel8.elf
aarch64-none-elf-objcopy -O binary kernel8.elf kernel8.img

# 4. Copy kernel8.img to SD card
```

### ARM Porting Task A.7: Testing on QEMU

```bash
# Raspberry Pi 4 emulation (QEMU 9.0+)
qemu-system-aarch64 -M raspi4b -serial stdio -kernel kernel8.img

# Raspberry Pi 3 emulation
qemu-system-aarch64 -M raspi3b -serial stdio -kernel kernel8.img

# GDB debugging
qemu-system-aarch64 -M raspi4b -serial stdio -kernel kernel8.img -s -S &
aarch64-none-elf-gdb kernel8.elf \
  -ex "target remote :1234" -ex "break kernel_main" -ex "c"
```

### ARM Porting Task A.8: MoonBit Integration (moon.pkg.json)

```json
// moon.pkg.json (ARM target)
{
  "supported-targets": ["native"],
  "native-stub": [
    "runtime/runtime_stubs.c",
    "drivers/uart_rpi.c"
  ],
  "link": {
    "native": {
      "cc": "aarch64-none-elf-gcc",
      "cc-flags": "-ffreestanding -O2 -Wall -nostartfiles -mgeneral-regs-only",
      "cc-link-flags": "-nostdlib -T linker.ld -lgcc"
    }
  }
}
```

MoonBit kernel code stays nearly identical. Only the output target changes from VGA to UART:

```moonbit
// kernel/main.mbt (ARM version — minimal diff)
extern "C" fn c_uart_init() = "uart_init"
extern "C" fn c_uart_puts(s : Bytes) = "uart_puts_bytes"

pub fn kernel_entry() -> Unit {
  c_uart_init()
  c_uart_puts(b"MoonBit OS v0.1 (ARM/Raspberry Pi)\n")
  c_uart_puts(b"Hello from MoonBit on bare metal ARM!\n")
}
```

### ARM Porting: Key Differences Summary

| Item | x86 (i686) | ARM (AArch64 / RPi) |
|------|-----------|---------------------|
| Cross-compiler | `i686-elf-gcc` | `aarch64-none-elf-gcc` |
| Bootloader | GRUB (Multiboot) | GPU firmware (bootcode.bin + start.elf) |
| Kernel format | ELF (`-kernel kernel.elf`) | Raw binary (`kernel8.img`) |
| Load address | `0x100000` (1 MiB) | `0x80000` (512 KiB) |
| Primary output | VGA text buffer (`0xB8000`) | UART (MMIO) |
| I/O access | Port I/O (`outb`/`inb`) | MMIO (volatile pointers) |
| Interrupts | IDT + PIC 8259 | GIC (Generic Interrupt Controller) + ARM Exception Vectors |
| Segmentation | GDT (segment descriptors) | Not needed (flat memory model) |
| Privilege levels | Ring 0-3 | EL0-EL3 (Exception Levels) |
| MMU | 2-level page tables (CR3) | Multi-level page tables (TTBR0/TTBR1) |
| Context switch | `pushad`/`popad` + CR3 | `stp`/`ldp` x0-x30 + TTBR0 |
| QEMU | `qemu-system-i386 -kernel` | `qemu-system-aarch64 -M raspi4b -kernel` |
| Real HW debug | Serial port (COM1) | USB-TTL cable + GPIO 14/15 |

### ARM Porting: Recommended Progression

1. **Complete Phase 0-2 on x86** — Learn OS fundamentals on x86 first
2. **ARM Phase 0**: Boot stub + UART output (C only, on QEMU)
3. **ARM Phase 1**: MoonBit integration — reuse `runtime_stubs.c` as-is, swap only drivers
4. **ARM Phase 2**: GIC + ARM exception handling + keyboard (USB HID is far more complex than PS/2 — defer if possible)
5. **Phase 3 onward**: MoonBit kernel logic (alloc, proc, syscall) is architecture-independent, so it can be shared

> **⚠️ Future Consideration: Keyboard Input on ARM**
>
> Raspberry Pi has no PS/2 controller, so keyboard input requires a USB HID driver.
> USB HID implementation spans multiple layers: USB host controller (DWC2/xHCI) → USB protocol stack → HID class driver.
> This is incomparably more complex than x86 PS/2 (just `inb(0x60)` to read a single byte).
>
> **Short-term workaround**: Use UART serial input instead (type from host PC terminal via USB-TTL cable).
> This requires no driver at all — just `uart_getchar()`.
>
> **Future options when this becomes a real need**:
> - Reference [rsta2/circle](https://github.com/rsta2/circle)'s USB driver for porting
> - Write USB stack in C, call from MoonBit via FFI
> - Revisit the design when USB keyboard support is actually required

Final project structure:

```
moonbit-os/
├── arch/
│   ├── x86/                # x86-specific code
│   │   ├── boot.s
│   │   ├── isr_stubs.asm
│   │   ├── gdt.c
│   │   └── linker.ld
│   └── arm/                # ARM-specific code
│       ├── boot.s
│       ├── exception_stubs.s
│       ├── gic.c
│       └── linker.ld
├── drivers/
│   ├── vga.c              # x86 only
│   ├── serial.c           # x86 only (COM1)
│   └── uart_rpi.c         # ARM only (Mini UART)
├── runtime/               # Architecture-independent
│   └── runtime_stubs.c
└── kernel/                # Architecture-independent (MoonBit)
    ├── main.mbt
    ├── interrupts.mbt
    ├── alloc.mbt
    └── proc.mbt
```

---

## Reference Resources

### x86 OS Development

- [OSDev Wiki - Bare Bones](https://wiki.osdev.org/Bare_Bones) — Minimal kernel tutorial in C
- [Writing an OS in Rust](https://os.phil-opp.com/) — Phase structure reference
- [OSDev Wiki - Zig Bare Bones](https://wiki.osdev.org/Zig_Bare_Bones) — Non-C language OS dev patterns
- [Bran's Kernel Development](http://www.osdever.net/bkerndev/Docs/idt.htm) — IDT / interrupt implementation guide

### ARM / Raspberry Pi Bare Metal

- [Learning OS Development Using Linux Kernel and RPi](https://s-matyukevich.github.io/raspberry-pi-os/) — **Top recommendation.** ARMv8 bare-metal OS tutorial (learn alongside Linux Kernel)
- [Writing a Bare Metal OS for RPi 4](https://www.rpi4os.com/) — RPi 4-specific tutorial (WSL compatible)
- [OSDev Wiki - Raspberry Pi Bare Bones](https://wiki.osdev.org/Raspberry_Pi_Bare_Bones) — OSDev-style RPi bare-metal intro
- [bztsrc/raspi3-tutorial](https://github.com/bztsrc/raspi3-tutorial) — RPi 3 AArch64 bare-metal tutorial collection
- [babbleberry/rpi4-osdev](https://github.com/babbleberry/rpi4-osdev) — RPi 4 OS development tutorial
- [dwelch67/raspberrypi](https://github.com/dwelch67/raspberrypi) — Bare-metal examples for all RPi generations
- [rsta2/circle](https://github.com/rsta2/circle) — C++ bare-metal environment (includes USB support, excellent reference)
- [Raspberry Pi RP1 Peripherals Datasheet](https://datasheets.raspberrypi.com/rp1/rp1-peripherals.pdf) — RPi 5 peripheral specs

### MoonBit / General

- [MoonBit C-FFI Guide](https://www.moonbitlang.com/pearls/moonbit-cffi) — FFI layout and ABI
- [MoonBit ESP32-C3 Demo](https://www.moonbitlang.com/blog/moonbit-esp32) — Embedded environment precedent
- [MIT xv6](https://github.com/mit-pdos/xv6-riscv) — Kernel module structure reference (99 pages)
