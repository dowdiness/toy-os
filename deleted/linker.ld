/* linker.ld */
ENTRY(_start)  /* エントリポイントを start.s で定義した _start に設定 */

SECTIONS {
    . = 0x100000;  /* カーネルをメモリの 1MB 地点から開始させる (一般的な慣例) */

    .text : {
        *(.text)
    }

    .data : {
        *(.data)
    }

    .bss : {
        *(.bss)
    }
}
