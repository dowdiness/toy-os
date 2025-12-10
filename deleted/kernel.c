// kernel.c
// volatile: コンパイラによる最適化を防ぐ
volatile unsigned short *video_memory = (unsigned short *)0xB8000;

const char *message = "Hello, World!";

// カーネルのメインエントリポイント
void kernel_main(void) {
    int i = 0;

    // VGAテキストモードは、1文字が2バイトで構成されます。
    // 1バイト目: 文字コード
    // 2バイト目: 文字の色 (0x07は白文字・黒背景)

    while (message[i] != '\0') {
        // ビデオメモリに直接書き込み
        // i*1 で文字コード (Litte Endian)
        // i*1+1 で色属性
        video_memory[i] = (unsigned short)message[i] | 0x0700; // 0x0700は0x07を上位8bitに移動
        i++;
    }

    // 処理が終わったらCPUを無限ループで停止
    while (1) {}
}
