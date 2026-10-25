# チャンネル別波形 / Channel waveform API — 0.13

```c
int32_t iopm_render_channels_f32(iopm_handle h, float* mix, float* channels, uint32_t frames);
int32_t iopm_render_events_channels_f32(iopm_handle h, float* mix, float* channels,
                                     uint32_t frames, const iopm_event* events, uint32_t count);
```

一度のレンダリングで通常のステレオ音声と8チャンネルの波形を生成する。両方のバッファが必要（frames=0ではNULL可）。既存のレンダリングAPIと組み合わせても時間は連続する。同じ区間を取得するために従来APIを続けて呼ぶ必要はない。

- `mix[2*frame + side]`: 通常出力。2×frames個のfloat。
- `channels[16*frame + 2*channel + side]`: 16×frames個のfloat。channel=0～7（表示上ch1～8）、side=0:left / 1:right。
- チャンネルのキャリア合成後、パン適用前、チャンネル間ミキシング前の値。パンを両方OFFにしても波形を取得できる。
- 既存の1/8正規化を維持。正規化なし相当の表示が必要なら呼出側で8倍する。クリッピングなし。
- L/Rを保持し、実測タイミングや明示的な出力遅延マスクを反映。モノラル表示にする場合は利用側でLかRを選択する。
- 通常出力と同じサンプルレート・33タップフィルター。パン変更時は通常出力に過去のパン設定によるフィルター残響があるため、現在のパンだけで再合成した値とは一時的に一致しない。
- イベントの順序・境界・引数検証は既存イベントAPIと同じ。バッファを重ねない。同じhandleへの並行アクセスは禁止。
- レンダリング時の動的メモリ確保はない。全レンダリングAPIで8チャンネルの履歴を更新するため、従来より処理量とインスタンスメモリが増える。

```c
float mix[256 * 2];
float channels[256 * 16];
int32_t rc = iopm_render_channels_f32(handle, mix, channels, 256);
if (rc == IOPM_OK) {
    /* ch1 left at frame 100 */
    float sample = channels[16 * 100];
    (void)sample;
}
```

C++: `chip.last_channel_samples()` は直近の内部サンプル（clock/64 Hz）の8チャンネルL/Rを返す。`advance`のSink内で取得すると全内部フレームを収集できる。値はリサンプル前。reset/load後は次の内部サンプル生成までゼロ（診断用キャッシュで保存対象外）。出力レートの波形が必要ならC APIを使う。

C ABI 1を維持し、関数を2個追加した。旧DLLには追加関数がないため再ビルドが必要。DLL状態形式はv2となり、0.12以前のDLL状態は読込不可。cloneとv2保存/復帰は8チャンネルのフィルター履歴を含む。

English: Both functions render the mix and eight pre-pan stereo channel streams together, advancing time once. Allocate 2*frames floats for mix and 16*frames floats for channels; layout is frame, channel, L/R. Channel streams retain the existing 1/8 normalization, timing delays and resampling. Both buffers are required for nonzero frames, must not overlap, and are unclipped. Events follow the existing API contract. All render paths keep channel histories current. C ABI remains 1; DLL save states use v2 and reject legacy v1. Windows binaries require rebuilding.
