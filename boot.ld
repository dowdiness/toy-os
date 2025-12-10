/* boot.ld */
OUTPUT_FORMAT(binary) /* 出力形式をバイナリにする */
OUTPUT_ARCH(i386)     /* 32ビットi386アーキテクチャ */
ENTRY(start)          /* エントリポイントを 'start' に設定 */

SECTIONS
{
    /* MBRがロードされるアドレス */
    . = 0x7c00;

    .text : {
        *(.text)
        *(.data)
        . = 0x1FE; /* 510バイト目 (0x7C00 + 0x1FE) */
        SHORT(0xAA55) /* MBRシグネチャ */
    }
}
