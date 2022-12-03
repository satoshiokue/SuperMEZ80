# SuperMEZ80

![SuperMEZ80](https://github.com/satoshiokue/SuperMEZ80/blob/main/imgs/IMG_1595.jpeg)  
Z80B 6MHz no wait  

![SuperMEZ80](https://github.com/satoshiokue/SuperMEZ80/blob/main/imgs/IMG_1555.jpeg)  
Z80 Single-Board Computer  

![SuperMEZ80](https://github.com/satoshiokue/SuperMEZ80/blob/main/imgs/IMG_1556.jpeg)  
積載可能  

電脳伝説さん(@vintagechips)のEMUZ80が出力するZ80 CPU制御信号をメザニンボードで組み替え、Z80と64kB RAMを動作させることができます。  
RAMの制御信号とIOアクセスのWAIT信号をPICのCLC(Configurable Logic Cell)機能で作成しています。  
電源が入るとPICはZ80を停止させ、ROMデータをRAMに転送します。データ転送完了後、バスの所有権をZ80に渡してリセットを解除します。  

LH0080BとPIC18F47Q43の組み合わせで動作確認しています。  

動作確認済みCPU  
SGS Z8400B1 2.5MHz  
SHARP LH0080 2.5MHz  
SHARP LH0080B 6MHz  
Zilog Z84C0010PEG 10MHz  

このソースコードは電脳伝説さんのmain.cを元に改変してGPLライセンスに基づいて公開するものです。

## メザニンボード
https://github.com/satoshiokue/MEZ80RAM  

MEZ80RAM専用プリント基板 - オレンジピコショップ  
https://store.shopping.yahoo.co.jp/orangepicoshop/pico-a-062.html

## 回路図
https://github.com/satoshiokue/MEZ80RAM/blob/main/MEZ80RAM.pdf　　

## ファームウェア

EMUZ80で配布されているフォルダemuz80.X下のmain.cと置き換えて使用してください。
* emuz80_z80ram.c

## クロック周波数による注意

クロック周波数を5.6MHz以下にする際はファームウェアの149-150行目を確認してください。
```
//while(!RA0);                // /IORQ <5.6MHz  
  while(!RD7);                // /WAIT >=5.6MHz  
```
Z80がIOアドレスのデータリードを完了するタイミングに適した信号を選択してください。  

両方のwhile文をコメントアウトすると16MHzまで動作するようです。  

## アドレスマップ
```
Memory
RAM   0x0000 - 0xFFFF 64kバイト

I/O
通信レジスタ 0x00
制御レジスタ 0x01
```

## PICプログラムの書き込み
EMUZ80技術資料8ページにしたがってPICに適合するファイルを書き込んでください。  

PIC18F47Q43 SuperMEZ80_xMHz_Q43.hex  

PIC18F47Q83 SuperMEZ80_xMHz_Q8x.hex  
PIC18F47Q84 SuperMEZ80_xMHz_Q8x.hex  


## Z80プログラムの格納
インテルHEXデータを配列データ化して配列rom[]に格納すると0x0000に転送されZ80で実行できます。
ROMデータは最大32kBまで転送できますが、現行のファームウェアは8kバイト(0x0000-0x1FFF)転送しています。


## 謝辞
思い入れのあるCPUを動かすことのできるシンプルで美しいEMUZ80を開発された電脳伝説さんに感謝いたします。

そしてEMUZ80の世界を発展させている開発者の皆さんから刺激を受けてZ80Bを6MHzノーウェイトで動かすところまで来られました。

## 参考）EMUZ80
EUMZ80はZ80CPUとPIC18F47Q43のDIP40ピンIC2つで構成されるシンプルなコンピュータです。

![EMUZ80](https://github.com/satoshiokue/EMUZ80-6502/blob/main/imgs/IMG_Z80.jpeg)

電脳伝説 - EMUZ80が完成  
https://vintagechips.wordpress.com/2022/03/05/emuz80_reference  
EMUZ80専用プリント基板 - オレンジピコショップ  
https://store.shopping.yahoo.co.jp/orangepicoshop/pico-a-051.html
