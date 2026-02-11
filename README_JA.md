# Toy OS (日本語)

[EN](README.md) | [日本語](README_JA.md)

最小構成の x86 ベアメタルブートローダープロジェクトです。  
現在の状態: 16-bit リアルモードのブートセクタで `Hello, World!` を表示します。

## クイックスタート

```sh
make        # boot_512.img をビルドして QEMU で起動
make run    # 既存の boot_512.img を QEMU で起動
make clean  # 生成ファイル (*.o, *.elf, *.img) を削除
```

## ドキュメント

- [ドキュメント索引](docs/README.md): 推奨読書順
- [プロテクトモード移行チュートリアル](docs/tutorial-01-protected-mode.md): 16-bit から 32-bit への実装手順
- [詳細ロードマップ（正本・日本語）](docs/ROADMAP.md)
- [ロードマップ英語要約](docs/ROADMAP_EN.md)
- [詳細技術レポート（正本・日本語）](docs/REPORT_JA.md)
- [レポート英語要約](docs/REPORT_EN.md)
