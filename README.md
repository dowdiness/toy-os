# Toy OS

Hallo World
アセンブリによるベアメタル環境でのHallo World

```sh
make # boot.s をアセンブルし、boot_512.img を作成後、QEMUで起動します。
make run #既に boot_512.img が存在する場合、QEMUを起動します。
make clean #boot.o, boot.elf, boot.img, boot_512.img の全生成ファイルを削除します。
```

参考チャット
https://gemini.google.com/share/2fd5c5c1853c
