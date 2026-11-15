# NDSIF / NanoDrive8 — 2.12.0

NanoDrive8をNDSIFモードにし、他のシリアル端末を閉じます。Workbenchの出力行でCOMポートとPCM出力先を選んでから、出力をNDSIFへ切り替えます。COM一覧は開くたびに更新します。接続エラー時はログを確認してください。COMを再選択するか、別の出力からNDSIFへ切り替えると再接続します。追加DLLやファームウェアは同梱しません。

| PCM出力先 | FM | PCM |
| --- | --- | --- |
| PCM: Windows | NanoDrive8 YM2151、4 MHz | Windowsのステレオ出力。FM/PCM遅延を調整可能 |
| PCM: NanoDrive8 | YM2151、PCM消費位置に対応した書き込み | 8チャンネルをモノラル合成し、15,625 Hzの連続ADPCMとしてMSM6258へ転送 |

本体PCMではFM/PCM個別遅延は適用せず、本体が消費した位置を再生表示へ反映します。停止・シーク・一時停止で本体をリセットし、再開時に状態を復元します。ライブMIDIにも同じ出力先を使用します。波形表示は内部計算値で、実機からの録音ではありません。WAV保存はソフトウェア出力です。VGM/VGZ保存は従来どおりステレオを保持し、本体PCMのモノラル化とは独立しています。

PDX編集では上部の音声出力でNDSIFとCOMを選択し、必要に応じて「更新・接続」を押します。WAV試聴・鍵盤・MIDIによる複数音は本体の単一ADPCMストリームへミックスします。WorkbenchとPDXEditorで同じCOMを選ぶと、ホスト内で1本の接続を共有します。曲のFMをYM2151へ送り、曲のPCMとエディタの試聴・鍵盤・MIDIをモノラル合成してMSM6258へ転送します。本体側・エディタ側のどちらを先に接続しても共有へ切り替わります。接続方式の切り替えでは試聴中の音を停止するため、必要に応じて弾き直してください。

共有中はPCM出力欄に「PCM: shared mix」を表示し、Windows PCM設定でも実効出力を本体PCMへ集約します。保存済みのPCM出力先は変更しません。エディタをWindows Audioへ変更・閉じる・別COMへ変更すると、本体側は保存済みのPCM出力先へ戻ります。本体を別の音源へ変更すると、エディタ側は単独のNDSIF接続へ戻ります。別COMを選んだ場合はそれぞれ独立した接続です。

本体の曲を停止してもエディタの試聴は使用できます。停止・シーク・一時停止で本体をリセットした際、試聴中の音は最後に確認した消費位置から続けます。接続変更を伴わない通常の音の追加ではストリームをリセットしません。合成音が16bitの範囲を超える場合は飽和処理します。大きすぎる場合は曲のPCM音量またはエディタの視聴音量を下げてください。出力先とCOMはPDXEditor.iniに保存し、サンプル編集のUndoでは変更しません。


接続時に能力とクロックを問い合わせ、不足・不一致、CRC不正、応答タイムアウト、転送停止をエラーとして処理します。通信模擬試験はフレーム・CRC・分割応答・能力・転送位置・シーク・終了・多重音・再接続を対象とし、実機での音質・USB転送持続性・クロック精度・MIDI遅延を証明しません。

## English

Choose the device COM port and PCM destination, then select NDSIF. Windows PCM preserves stereo; device PCM mixes eight channels to mono ADPCM at 15,625 Hz. Stop and seek reset hardware. Device PCM follows consumed-byte progress and ignores separate FM/PCM delay settings. WAV and VGM/VGZ exports retain their existing software/stereo behavior. The embedded PDX editor shares one host connection when both clients choose the same COM. Song PCM and editor preview/key/MIDI voices are mixed to device mono PCM. Shared mode temporarily overrides PC PCM without changing the saved preference. Closing or disconnecting the editor restores the saved song PCM destination. Separate COM ports remain independent. Connection changes stop current preview notes; stop/seek on an existing shared connection resumes previews from consumed position. No firmware is bundled or updated.

## 出自・ライセンス

IndependentMDX 0.34.0の独自ホスト実装（0BSD）とPDXEditor 1.1.0 NDSIF追加部分（0BSD）を、既存のWorkbench向け適応実装へ取り込んでいます。基準コミットと入力ハッシュはソースアーカイブのdocs/DEPENDENCY_PROVENANCE.mdに記録します。

通信仕様：[NDSIF API](https://github.com/Fujix1/NanoDrive8/blob/main/NDSIF_API.md)、[NanoDrive8 profile](https://github.com/Fujix1/NanoDrive8/blob/main/NDSIF_ND8.md)。NanoDrive8自身の自作コードは個別の例外を除きBSD-3-Clause、Copyright (c) 2024-2026, Fujixです（[LICENSE](https://github.com/Fujix1/NanoDrive8/blob/main/LICENSE)）。この配布物にNanoDrive8のソースやファームウェアは含まれません。ホスト実装の0BSDとは適用範囲が異なります。
