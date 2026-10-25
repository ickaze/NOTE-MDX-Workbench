# 0.13 状態保存・復元 / Save states

コア、ホスト側Resampler、C ABI DLLにバージョン付きの状態保存を追加した。
既存のC ABI 1は維持し、3つの関数を追加エクスポートする。旧DLLには新関数がないため再ビルド・差し替えが必要。

## C++コア

```cpp
std::vector<std::uint8_t> saved = chip.save_state();
// ファイルへの保存はsaved.data(), saved.size()で呼び出し側が行う。
// 演奏を進めたあと、保存地点へ戻す。
if (!chip.load_state(saved.data(), saved.size())) {
    // 不正な状態。chipは変更されない。
}
```

`Ym2151::save_state() const`はバイト列を返す。
`Ym2151::load_state(const void*, std::size_t)`は成功時true、不正データではfalseを返す。
復元先インスタンスの設定より保存データを優先し、マスタークロックも復元する。
メモリ不足はC++の例外として呼び出し側へ伝わる。

保存対象は全256レジスタ、32OPの位相・減衰・エンベロープ段階・キー状態、フィードバック、手動キー、経過クロック、タイマー残数、BUSY、アドレスラッチ、IRQフラグ、AMD/PMD、周期・ランダムLFOの分周器・位相・系列・ラッチ、LFO表示値、CSMの解除待ち、最終出力、左右の遅延マスク、実測タイミング設定、前回OP出力。

`Resampler`にも同じシグネチャのsave_state/load_stateがある。33フレームの履歴、リング位置、1024×33のフィルター係数を全て保存する。係数を再計算せず復元する。
コアを直接使うアプリがResamplerも使用する場合、両方を保存すること。

## DLL / C

```c
uint32_t size = 0, written = 0;
int32_t rc = iopm_state_size(handle, &size);
if (rc == IOPM_OK) {
    void* state = malloc(size); /* #include <stdlib.h> */
    if (state) {
        rc = iopm_save_state(handle, state, size, &written);
        if (rc == IOPM_OK) {
            /* stateのwrittenバイトをファイルに保存可能。
               後で同じバイト列を読み戻し、次の呼び出しで復元する。 */
            rc = iopm_load_state(handle, state, written);
        }
        free(state);
    }
}
```

| 関数 | 内容 |
|---|---|
| `iopm_state_size(handle, uint32_t* size)` | 必要バイト数を取得 |
| `iopm_save_state(handle, void* buffer, uint32_t capacity, uint32_t* written)` | 呼び出し側バッファへ保存。writtenは必要サイズ |
| `iopm_load_state(handle, const void* buffer, uint32_t size)` | 保存地点へ復元。失敗時は元の状態を維持 |

`iopm_save_state(handle, NULL, 0, &size)`でもサイズを取得できる。
容量不足では`IOPM_BUFFER_TOO_SMALL (-4)`、破損・異形式・未対応バージョンは`IOPM_INVALID_STATE (-5)`。
引数不正は既存の`IOPM_INVALID_ARGUMENT (-1)`、メモリ不足は`IOPM_OUT_OF_MEMORY (-2)`。
DLLではコアとResamplerに加え、出力サンプルレートおよび出力クロックの剰余を保存する。
別設定で作成したハンドルへの読み込みでも、保存時のクロック・出力レートに戻る。
DLL状態とコア単体状態は別形式であり、相互にloadすることはできない。

## 形式と利用条件

形式v1はリトルエンディアン固定幅整数、IEEE 754 binary64、明示的な各フィールドで構成する。オブジェクトの生メモリやpadding、ポインターは保存しない。
24バイトのヘッダーに種別・形式バージョン・64bit FNV-1aチェックサムを持つ。チェックサムは偶発的破損検出用であり、認証ではない。
長さ、種別、バージョン、チェックサム、主要な範囲を検証し、一時インスタンスに読み込んでから反映する。
将来、形式を変更する場合はバージョンを更新する。未対応形式を黙って読み込まない。

32/64bitで同じデータ配置となる設計。この文書の検証はLinux x64で行い、Windows Win32/x64相互の実行検証はしていない。コンパイラーやCPUが異なる場合、復元後の浮動小数点演算までビット一致するとは保証しない。同一実行環境では保存地点からの出力一致を検証した。

保存・復元・サイズ問い合わせはメモリを確保するため、リアルタイム音声コールバックを避ける。同一インスタンスのadvance/renderや書き込みを停止し、直列に実行する。Sinkコールバック内からの再入も行わない。
MDXの再生位置、外部イベントキュー、アプリ側の音量・補間器、音声デバイスに既に送ったバッファはエミュレーターの外部状態なので、ホストが別途保存・同期する。

## 検証

`tests/test_state.cpp`で以下を検証した。

- 4種のLFO、タイマー、BUSY、アドレスラッチ、キーオフ、出力履歴を含むコアのバイト列往復一致と、その後の出力一致。
- DLLの端数クロックがある地点を保存し、異なる初期クロック・出力レートのハンドルへ復元。4096ステレオフレームが完全一致。
- 同じハンドルを巻き戻した場合も一致。
- 破損・途中で切れたデータの拒否と、失敗時の状態不変。
- サイズ問い合わせと容量不足エラー。

既存のコア7試験、C ABI試験、DLL/直接コア一致試験も実行。
CMakeのstate_serializationテスト、build_dll_windows.batのWin32/x64の試験工程へ登録した。

## English

Ym2151 and Resampler expose `save_state()` returning a byte vector and `load_state(data,size)` returning bool. The DLL adds `iopm_state_size`, `iopm_save_state`, and `iopm_load_state`. DLL snapshots include the entire core, filter history and coefficients, output rate, and fractional clock remainder. Loading replaces the destination configuration with the saved configuration. Failed loads are atomic.

The versioned little-endian format uses explicit fields and IEEE binary64, not object memory. It is layout-independent between 32/64-bit builds; cross-platform floating-point continuation is not guaranteed bit-identical. Tests on Linux x64 confirm identical continuation within one build. Stop concurrent rendering and do not call these allocating APIs from audio callbacks or core Sink callbacks. Host-owned sequencer, queued audio, and application state must be saved separately.

## 0.13: DLL state v2

8チャンネルのResampler履歴を追加保存する。DLLの種別は3、形式バージョンは2。0.12以前のDLL状態v1は `IOPM_INVALID_STATE` で拒否し、現在のインスタンスを変更しない。コアおよびResampler単体形式v1は維持。デコード時のサイズ上限は4MiB。
