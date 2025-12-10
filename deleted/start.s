# start.s (GAS記法)

.global _start  # リンカのために _start シンボルを公開

.section .text
_start:
    # スタックポインタ(esp)を設定。0x90000はメモリ内の適切な場所
    mov $0x90000, %esp

    # C言語の kernel_main 関数を呼び出す
    call kernel_main

    # kernel_mainから戻ってきた場合の無限ループ
    cli
    hlt
