> Workbench収録範囲：この文書が挙げるresearch資料・録音・上流PACKAGE_MANIFEST.jsonは非同梱です。[資料の区別](UPSTREAM_MATERIALS.md)を参照してください。記載の測定・検証は上流の該当段階の記録です。

# Independent OPM 0.14（シングルスレッド高速化版）

YM2151のレジスタ入力からステレオ音声を生成するC++17コアです。本配布は **0BSD**。0.14ではシングルスレッドのまま音程・振幅計算をキャッシュして高速化しました。[変更内容と比較結果](docs/OPTIMIZATION_0.14.md)。0.13で追加したチャンネル別波形APIも利用できます。詳細は[API仕様](docs/CHANNEL_OUTPUT_API.md)を参照してください。

Copyright (C) 2026 by I.C.KaZe


## 変更

- コアとDLLに全内部状態の保存・復元APIを追加。[使用方法](docs/STATE_API.md)。

- 08hキーオンのC1/M2取り違えを修正。ALG5/6/7のOP=3音色の発音を訂正。[原因・検証結果](docs/KEY_ON_MAPPING_FIX.md)。DLLまたは組み込みコアを再ビルドしてください。

- 周期LFOを実機録音の追加段列・波形・深度から再実装。
- 矩形AM最大値255、PMの整数感度処理へ変更。
- 乱数の1回更新をsequence_stepへ改名。計算式と16回更新の最適化は維持。
- 参照比較テストを撤去し、実録音による回帰試験へ移行。

[実装と暫定仕様](docs/PERIODIC_REPLACEMENT.md)、[開発・訂正履歴](docs/history/README.md)、[ライセンス](docs/LICENSING.md)、[検証ログ](docs/VALIDATION_REPLACEMENT.txt) を同梱しています。

8ch/32OP、8アルゴリズム、タイマ・BUSY・IRQ、ホスト側リサンプラに対応。ALG5/ALG7の実測出力タイミングはset_measured_output_timing(true)で有効にできます。

## Windows 11 / Visual Studio 2022

「x64 Native Tools Command Prompt for VS 2022」でこのフォルダを開き、実行します。

```bat
build_windows.bat
```

bin/opm_render.exe と bin/opm_tests.exe を生成し、テスト後に demo.wav を生成します。
Windows DLLと実行ファイルは同梱していません。DLLはこの0.14ソースから再ビルドしてください。
C++によるデスクトップ開発ワークロードが必要です。

CMakeでもビルドできます。

```bat
cmake -S . -B build -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
build\Release\opm_render.exe --demo demo.wav
```

## WAV生成

```bat
bin\opm_render.exe --demo demo.wav
bin\opm_render.exe examples\a440.trace a440.wav 2 3579545 48000 1
```

引数は「入力または--demo」「出力WAV」「秒数」「チップクロックHz」「出力Hz」「ゲイン」です。
最後の4つの既定値は6秒、3579545Hz、48000Hz、1です。
44100〜192000Hz等の出力に対応します（許容範囲8000〜192000Hz）。
出力は16bitステレオPCM WAV。クリップ数を終了時に表示します。
ゲインはチップの実アナログ出力電圧とは対応しません。

trace形式は examples/a440.trace を参照してください。
絶対マスタークロック、レジスタ番号、書込値の3列です。
数値は10進、または0xを付けた16進。先頭0の数値はC++の変換規則により8進です。
同時刻の書込は記載順に処理します。時刻が逆行する入力はエラーです。
CLIは最大600秒、時刻は32bitまで。実行中に指定範囲以降のイベントがある場合はエラーです。
trace入力はBUSY待ちを省いた直接レジスタAPIを使います。
MDX/PDX/VGMのファイル読込は音源コア単体のツールにはありません。

## 組み込み

include/ym2151.hpp、include/measured_lfo.hpp、src/ym2151.cpp、src/envelope_times.hpp、src/state_codec.hppで利用できます。
音源コアはC++標準ライブラリのみを使用します。

```cpp
#include "ym2151.hpp"
using namespace independent_opm;
Ym2151 chip(3579545);
chip.write_register(0x20, 0xc7);
chip.write_register(0x28, 0x4a);
chip.write_register(0x40, 1);
chip.write_register(0x80, 0xdf);
chip.write_register(8, 8);
chip.advance(64);
Stereo sample = chip.last_sample();
```

advance()はマスタークロックを進め、64クロックごとに音声を生成します。
全サンプルが必要な場合はadvance(clocks, callback, context)を使います。
callback内で同じchipを変更・再帰呼出ししないでください。
バスとして接続する場合はwrite_address()/write_data()/status()/irq()を使用します。
write_data()はBUSY中にfalseを返し、書込みを適用しません。
write_register()は統合用の直接APIであり、BUSY拒否を省略します。

Resamplerを使用すると、33タップの窓付きsincでホストのサンプルレートへ変換できます。
src/main.cppに、イベント時刻を維持した使用例があります。
フィルタ遅延はネイティブ16サンプルです。
保存・復元はsave_state()/load_state()でバージョン付きバイナリへ行えます。DLLではResamplerと時刻余りも保存します。[状態保存API](docs/STATE_API.md)を参照してください。

## 実装範囲

- 8チャンネル、32オペレータ、8アルゴリズム、フィードバック
- KC/KF、MUL、DT1/DT2、TL、KS、AR/D1R/D2R/D1L/RR
- LFO 4波形、PMD/AMDの独立保持、PMS/AMS、LFOリセット
- チャンネル8のC2ノイズ、タイマA/B、IRQ、BUSY、CT1/CT2
- CSMの簡易キーオンパルス
- ステレオPCM、ホスト側リサンプラ、trace→WAV、内蔵デモ

## 精度の制限

実機とのビット一致・サイクル一致は未達成です。解析的sin、浮動小数音程、エンベロープの一部や内部パイプラインは近似です。LFOの未確定状態遷移、ランダムのFREQ下位4ビット、深いAMSの丸めなどは置き換え仕様を参照してください。

実録音による検証は実施済みですが、全レジスタ・全音程の完全一致を意味しません。Windows/MSVCでの実行は未検証です。

## 0.11b 検証資料更新

docs/evidence/index.html からLFO・AL5/7左右タイミングの日本語/英語資料を開けます。録音名はMDXのベース名へ統一。連続録音と26A/Bの未確認対応は docs/RECORDING_NAMES.md を参照。元録音は tools/prepare_recordings.py でハッシュ照合して配置できます。

## Windows DLL (Win32 / x64)

外部アプリ向けのC ABIを追加しました。`build_dll_windows.bat` でVS2022から32/64bitをビルドできます。
API: `include/independent_opm.h`、仕様: `docs/DLL_API.md`、C使用例: `examples/dll_client.c`。
MDX/PDXの解析器は含まず、レジスタ入力とPCM出力を提供します。Windowsバイナリは未同梱。
Build both architectures with VS2022 using `build_dll_windows.bat`. See `docs/DLL_API.md`.

## 開発方針と文書の読み方

公開マニュアルの仕様と実機の録音・挙動測定に基づく実装です。過去に外部実装を参照したLFO処理は、測定に基づく処理へ置き換えています。現行配布物の説明は[LICENSING.md](docs/LICENSING.md)、途中段階の作業・訂正記録は[履歴資料](docs/history/README.md)に分離しています。


### ノイズ音量の実測補正（0.12 / 2026-09-29）

ノイズ出力を従来の1/4に修正しました。詳細と確認範囲は
ノイズ検証資料（上流の `research/noise-validation-43-46/analysis/NOISE_VALIDATION.html`、この抜粋には非同梱。[非同梱資料について](UPSTREAM_MATERIALS.md)）を参照してください。
DLLも同じコアから再ビルドすると修正が反映されます。Windows DLLバイナリは本ソース更新に含みません。
