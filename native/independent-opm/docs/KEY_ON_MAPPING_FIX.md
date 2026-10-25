# 0.11b キーオン対象の訂正 / Key-on mapping fix

2026-09-23。版番号と0BSDライセンスを維持。

Copyright (C) 2026 by I.C.KaZe


## 原因と変更

Yamaha OPM Application Manual、2.1.1 KONのビット指定と、オペレーターのパラメータ用レジスタ順は異なる。

| 対象 | M1 | C1 | M2 | C2 |
|---|---:|---:|---:|---:|
| 08hキーオンのビット番号 | 3 | 4 | 5 | 6 |
| パラメータレジスタのオフセット（チャンネル番号は別途加算） | 0 | 16 | 8 | 24 |
| コア内部のslot番号 | 0 | 2 | 1 | 3 |

従来はビット4を内部slot1、ビット5をslot2に直接対応させていたため、C1とM2が逆になっていた。
`src/ym2151.cpp`の08h書き込みで対応を変換し、CSM終了時に使うmanual_keys_も内部slot順で保持する。
アルゴリズムの接続、パラメータレジスタ順、実測出力タイミングのマスク順は変更していない。
C ABIのレジスタ書き込みも同じコアに到達するため、この訂正が適用される。

一次資料: [Yamaha OPM Application Manual](https://map.grauw.nl/resources/sound/yamaha_ym2151_synthesis.pdf)、2.1.1 KON、Fig.2.2/2.3。
他エミュレーターのソースを参照してこの修正を行ったものではない。

## 提示音色の再現条件

ユーザーが訂正した@80、@83、および追加した@11を`tests/voice_regression.hpp`に収録。
提示テキストの4行はM1,C1,M2,C2として読み、レジスタへは上表に従って格納する。
最初に提示された、2行目と3行目が逆のテキストを直接レジスタ順に扱うことはしない。
OP=3は08hへ0x18（ch0）を書き込み、M1とC1をキーオンする。
検証用ロード関数は音色定義のパラメータを直接レジスタへ書くもので、MDXファイルのパーサではない。

4 MHz、KC=0x48、KF=0、ch0、左右出力、2秒間（125,000内部サンプル）、実測タイミングOFFの左出力。
値は正規化浮動小数点音声の絶対ピークとRMS。実機録音との一致率ではない。

| 音色 | ALG | 修正前ピーク | 修正後ピーク | 修正前RMS | 修正後RMS |
|---|---:|---:|---:|---:|---:|
| @80 | 6 | 3.42302454e-11 | 0.0526896559 | 2.42033885e-11 | 0.0201701828 |
| @83 | 7 | 0.0072211605 | 0.0399907975 | 0.00157600861 | 0.00768671246 |
| @11 | 5 | 3.42302454e-11 | 0.0342302454 | 2.42037699e-11 | 0.0220695803 |

修正前の@80/@11は実質無音、@83はC1が欠落していた。
訂正版では3音色とも発音し、実測出力タイミングONでも確認した。
OL（この定義ではTL相当）127はそのオペレーターを約95.25 dB減衰させる。
複数キャリアの一つに127を設定しても、それ以外の有効なキャリアまで無音になる仕様ではない。
対象音色の未使用行はAR=0でもあり、意図したC1ではなくその行にキーオンしていた。

## 回帰検証

- 全8ch × 16キーオンマスク、他chへの非干渉、キーオフ、CSM後の手動キー状態の保持。
- 訂正版3音色、実測出力タイミングOFF/ONの両方で有限値・発音レベルを検査。
- core、precision、output_timing、random、sequence、periodicの既存6テストが成功。
- Linux共有ライブラリ経由のC ABIテスト、直接コアとの4096フレーム一致、3音色の発音が成功。
- 修正前コアへ新しいキーオンテストを適用すると `FAIL: C1 key` となることを確認。
- CMakeおよびbuild_windows.batへ新テストを登録。Windows/MSVCでの実行はこの環境では未検証。

これはレジスタ解釈の不具合修正を検証する試験であり、3音色の音色全体が実機と完全一致することの証明ではない。
ホスト側に別の音色行変換やキーオン変換がある場合、その実装は別途確認が必要。

## 利用方法

DLL利用時はbuild_dll_windows.batでWin32/x64を再ビルドし、使用アプリに合わせてDLLを更新する。
コアを静的に組み込むアプリではsrc/ym2151.cppを更新してアプリを再ビルドする。
音色側のOPやOLを回避策として書き換える必要はない。
既存のDLLビルドスクリプト修正（vswhere非必須、.libを/link後へ渡す処理）は維持した。

## English summary

Register 08h selects M1/C1/M2/C2 using bits 3/4/5/6, whereas the core stores operators in register order M1/M2/C1/C2. The old code swapped C1 and M2 when decoding key-on. The fix maps these bits explicitly and stores the manual key mask in internal order for CSM release. Algorithm routing and attenuation rules are unchanged.

The three corrected user patches are included in the regression fixture. With OP=3, M1 and C1 now sound; setting unused carriers to OL=127 does not mute another active carrier. Tests cover all 128 channel/mask combinations, CSM, the three patches with measured timing enabled and disabled, existing suites, and the shared C ABI. This validates the register-decoding fix, not full hardware waveform equivalence. Windows builds must be rebuilt by the user; native Windows execution was not available for this validation.
