# 0.14 シングルスレッド高速化

## 実装

- 各OPの位相増分をPMのcents値の完全一致時に再利用する。KC/KF、DT1/MUL、DT2の変更時に位相増分キャッシュを無効化する。
- 各OPの指数振幅を最終減衰量（EG/TL/AM）の完全一致時に再利用する。
- Resamplerの履歴参照を循環インデックスで進め、各タップの剰余演算を省く。加算順序は維持する。
- sin、pow、exp2の近似化、fast-math、並列処理は使用しない。
- キャッシュは派生値として保持し、reset/loadで無効化する。clone/copyではコピーする。状態ファイルには含めない。

C ABI 1、公開19関数、DLL保存状態v2、コアとResamplerの状態v1を維持する。0.13のDLL保存状態を読み込める。0.12以前のDLL保存状態v1は引き続き未対応。
演算は同じ式と順序を保つ。呼出途中で浮動小数点丸めモード等を変更する使用は一致保証の対象外。

## 検証（2026-10-11）

比較元は公開0.13コミット `7be1afbdbfa4fa1227361e9e3b488ad8658ca12d`。
Linux x64 / GCC 13.3.0で両版を同じ `-std=c++17 -O3 -DNDEBUG -fPIC -shared -DIOPM_BUILD_DLL` フラグでビルドした。CMakeがない検証環境のためg++/gccで直接ビルドした。

16条件・196,608フレームの通常ステレオfloat、チャンネル別float、保存状態の全バイトが一致した。復元後のfloat/int16出力、clone、resetも一致した。
4出力レート（8/44.1/48/96kHz）、3クロック（3.579545/4/8MHz）、全8ALG、4LFO波形、ノイズ、実測と個別出力遅延、タイマー/CSM、各条件1,000回の固定乱数レジスタ書込みを含む。既存12試験も通過した。

[比較結果JSON](OPTIMIZATION_RESULTS_0.14.json)、[既存試験ログ](OPTIMIZATION_TESTS_0.14.txt)を参照。
全入力を網羅する証明ではない。Windows/MSVCでの実行比較と速度測定は未実施。このソース配布物にはWindows DLLを含まない。

## 速度

Intel Xeon Platinum 8370C / Linux x64。8ch発音、4MHz、48kHz、各回48,000フレーム。初期化と状態保存は計測外。5回、変更前後を交互に測定した中央値。
音色、CPU、コンパイラ、書込み頻度によって効果は変わる。

| LFO | チャンネル波形 | 0.13 | 0.14 | 速度比 |
|---|---|---:|---:|---:|
| OFF | なし | 163.29 ms | 92.25 ms | 1.77倍 |
| OFF | あり | 190.24 ms | 121.71 ms | 1.56倍 |
| ON | なし | 160.50 ms | 87.17 ms | 1.84倍 |
| ON | あり | 205.33 ms | 120.33 ms | 1.71倍 |

## 再現

同じコンパイラ・最適化条件でビルドした両版のライブラリを指定する。

```text
python tools/verify_optimization.py /absolute/path/to/0.13/library /absolute/path/to/0.14/library
```

Python標準ライブラリのみ使用。Windowsでは同じbit数のPythonとDLLを用いる。比較不一致時はRuntimeErrorで停止する。

English: Exact-value caches and a wrapped resampler index preserve arithmetic formulas and summation order. All samples and serialized state bytes in the tested Linux cases matched 0.13. The 12 existing tests passed. Windows/MSVC execution and performance comparisons remain untested.
