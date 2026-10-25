# Windows DLL / C API — 0.14, ABI 1

## DLLの役割 / Scope

外部のMDX再生アプリなどから、YM2151のレジスタを書き込み、ステレオPCMを取得できます。
MDX解析、MXDRVコマンド処理、PDX/ADPCM再生、PCMとのミックス、音声デバイスへの出力は呼び出し側の担当です。
X68Sound.dllのバイナリ互換品ではありません。専用の `independent_opm.h` を使用します。

This DLL is a YM2151 synthesis engine. The host handles MDX/MXDRV sequencing, PDX/ADPCM,
mixing and audio-device output. This is a new C API, not a drop-in X68Sound ABI.

## 32bit / 64bit の作成

Visual Studio 2022 CommunityまたはBuild Toolsの「C++によるデスクトップ開発」をインストールし、
`build_dll_windows.bat` を実行してください。開発者環境・指定パス・標準配置からVS2022を検索し、x86とx64を別々の環境でビルドします。vswhere.exeは任意で、なくても動作します。
各DLLにリンクしたC APIテストも実行し、失敗時は停止します。

自動検出できない独自インストール先では、ルートフォルダーを引数に指定できます：

```bat
build_dll_windows.bat "D:\Apps\Visual Studio\2022\Community"
```

または `vcvarsall.bat` のフルパスを指定します：

```bat
build_dll_windows.bat "D:\Apps\Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat"
```

引数なしの場合は `IOPM_VS`、`VSINSTALLDIR`、`VCINSTALLDIR`、`VCToolsInstallDir`、
Program Files側の2022各エディションを調べます。最後に、存在する場合のみvswhereを試します。
スタートメニューの「Developer Command Prompt for VS 2022」から実行しても構いません。
C++ツール自体が未導入の場合は「C++によるデスクトップ開発」、MSVC x86/x64ツール、Windows SDKが必要です。

vswhere is optional. Pass the VS installation root or the full path to vcvarsall.bat for custom installations,
or run from a VS2022 Developer Command Prompt. The script checks existing developer environment variables
and standard 64-bit/32-bit Program Files layouts before optionally trying vswhere.


|アプリの種類|DLL|インポートライブラリ|
|---|---|---|
|32bit / Win32|bin/Win32/independent_opm.dll|bin/Win32/independent_opm_import.lib|
|64bit / x64|bin/x64/independent_opm.dll|bin/x64/independent_opm_import.lib|

両方のDLLは同名ですが別フォルダーです。呼び出すプロセスのbit数に合わせて配置してください。
64bit Windows上の32bitアプリはWin32版を使います。
`include/independent_opm.h` をインクルードし、対応する `.lib` にリンクします。
DLLはEXEと同じフォルダーなど、Windowsが検索できる場所へ配置します。

CMakeを使う場合（VS2022用、同一buildフォルダーを使い回さない）：

```
cmake -S . -B build-win32 -G "Visual Studio 17 2022" -A Win32
cmake --build build-win32 --config Release
ctest --test-dir build-win32 -C Release --output-on-failure
cmake -S . -B build-x64 -G "Visual Studio 17 2022" -A x64
cmake --build build-x64 --config Release
ctest --test-dir build-x64 -C Release --output-on-failure
```

`IOPM_BUILD_DLL=OFF`でDLLの追加を無効化できます。従来の静的コアも残っています。
出力の静的コアlibとDLL用libは名前を分け、同じディレクトリでも衝突しません。
The batch builds both architectures with VS2022 and runs the linked C tests. CMake builds each
architecture in a separate tree. Match DLL bitness to the host process, not just the OS.

## APIとデータ

- C ABI、`__cdecl`。`.def` で32/64bit共通のエクスポート名を定義。
- `iopm_abi_version()` = 1。バージョン文字列は `0.14 / C ABI 1`。
- `iopm_create(clock_hz, output_rate, &handle)`：100 kHz～10 MHz、8～192 kHz。X68000なら4,000,000 Hz。
- `iopm_write_register(handle, address, value)`：各0～255。BUSYによる拒否を省く統合用書き込み。
- `iopm_render_f32` / `iopm_render_s16`：L,R,L,Rの順、`frames*2`要素の呼び出し側バッファ。
- floatはコアの振幅をそのまま出力しクリップしません。int16は×32768、丸め、−32768～32767に制限。
- ゲインの自動正規化はありません。外部PCMとのミックスにはfloatを推奨します。
- 時間は描画したフレーム数だけ進みます。1内部サンプル=64マスタークロック。
- 既存33タップ・1024位相のリサンプラーを使用。遅延は16内部サンプル。
- `iopm_read_status`：コアのBUSY/タイマー状態とIRQ状態。`iopm_clock_count`：累積マスタークロック。
- `iopm_clone`：レジスタ、発音、タイマー、乱数、フィルター、時間の端数を含む独立コピー。
- `iopm_reset`：同一クロック/出力レートで初期化。実測モードや個別遅延設定も解除。
- `iopm_destroy`：作成したDLL内で解放。NULLは許可。解放済みハンドルの再利用は不可。

成功0、引数不正−1、メモリ確保失敗−2、内部例外−3。C++例外はABI境界から出しません。
有効なポインターと十分なバッファ容量は呼び出し側の責任です。無効な非NULLポインターは検出できません。
同一ハンドルは外部で排他制御してください。別ハンドル同士は独立です。
描画中の動的メモリ確保はありません。create/clone/resetは音声コールバックの外で実行してください。

The API uses caller-owned interleaved buffers and opaque instance handles. Use the same DLL to destroy
its handles. Calls on one handle must be serialized; separate handles are independent. No allocation
occurs during rendering. Creation, cloning and reset may allocate and should run outside real-time callbacks.

## 時刻指定のレジスタ列

`iopm_render_events_f32` は、描画開始時の `iopm_clock_count` からの相対マスタークロックを指定します。
`iopm_event` はuint32_tのoffset/address/valueの3要素、サイズ12バイトです。
昇順に並べ、同一時刻のイベントは配列順に実行します。内部サンプル生成の境界と同時刻なら、
その境界での生成が先、レジスタ更新が後です。先頭offset=0は最初の時間進行前に適用します。
末尾の到達クロックちょうどのイベントも許可。過去/範囲外/順序不正は描画前に拒否し、状態を変えません。
バッファ、イベント配列、ハンドルのメモリを重ねないでください。

出力フレームNまでの累積クロックは `floor(N*clock_hz/output_rate)` です。
ホスト側も整数演算で管理してください。create/reset時にN=0、clone時は時間を引き継ぎます。
毎回の切り捨てによる時間のずれを避けるため、DLL側も端数を次のバッファへ引き継ぎます。
uint32の相対時刻で表せる範囲の小さなバッファに分けてください。

Events use relative master-clock offsets, sorted with stable order at ties. A native sample at the same
clock is generated before the write. Invalid event arrays are rejected before rendering. Fractional host
clock time is retained across buffers, so splitting rendering does not change output.

## 実測左右タイミング

`iopm_set_measured_output_timing(handle, 1)` でALG5/7モデルをON。初期値はOFFです。
`iopm_set_output_delays` は個別chのM1/M2/C1/C2に対応する4ビットマスク。両方0なら個別指定解除。
非ゼロの個別指定は測定モデルより優先します。
このDLL APIにはGMC-OPT04の共通1出力フレーム遅延もYM3012のアナログモデルも含みません。
必要な出力装置の遅延は、呼び出し側の出力段で別途扱ってください。
The measured ALG5/7 mode selects carrier frames only; capture-device and analog-DAC delays are separate.

## 例 / Example

`examples/dll_client.c` はC2単独音をDLLから描画し、48 kHzステレオfloat32の `dll_example.f32` を作ります。
MDXアプリでは音色・音程・キーオン等のレジスタ列を既存ドライバーから渡してください。
`LoadLibrary`/`GetProcAddress`でも `.def` と同じ名称で取得できます。
関数ポインターは `IOPM_CALL` と同じ呼び出し規約を宣言してください。

## この文書に記録した確認範囲

Linuxの共有ライブラリでC APIを実行し、バッファ分割・クローン・イベント時刻・不正引数・リセットを確認。
同じクロックとレジスタ列をC++コアへ直接渡した結果と、DLL API経由4096フレームのfloat出力が一致。
Windows用コンパイラがこの作業環境にないため、Windows DLL実体は未ビルド・未同梱です。
VS2022でWin32/x64をビルドする設定とバッチ、Cテストを同梱しています。Windows上の実行検証済みとはしません。
The Windows binaries are not included: no Windows compiler is available in the build environment.
Linux tests verify the wrapper. The Windows x64 binary has been statically inspected; Windows runtime execution and x86 binary validation have not been performed here.

## Save states (0.11b addition)

`iopm_state_size`, `iopm_save_state`, `iopm_load_state` save/restore the core and resampler together. See [STATE_API.md](STATE_API.md).

## チャンネル別出力 / Channel outputs

[仕様と使用例](CHANNEL_OUTPUT_API.md)。公開関数19個、C ABI 1。DLL保存状態はv2（旧v1は読込不可）。
