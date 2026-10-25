# DLLの現行APIと検証記録（0.11b）

Copyright (C) 2026 by I.C.KaZe

文書更新識別子：docs-review-20260925。初回導入時の14関数の記録は[履歴](DLL_VALIDATION_INITIAL.md)へ移動した。

## docs-review-20260925で確認したAPI

`include/independent_opm.h`の公開宣言、`dll/independent_opm.def`の公開名、配布元提供のWindows x64 DLLのエクスポート表が、いずれも同じ17関数であることを静的に確認した。
追加の3関数は `iopm_state_size`、`iopm_save_state`、`iopm_load_state`。
全関数名、基点ZIPと検査DLLのSHA-256は[PRE_CHANNEL_DLL_API_INVENTORY.json](PRE_CHANNEL_DLL_API_INVENTORY.json)に記録する。

この一致はAPI名の照合であり、DLLが同梱ソースから再現ビルドされたことや、Windowsでの動作を証明するものではない。検査DLLは別途提供されたもので、このソースZIPには同梱しない。

## 以前に実行した試験

- C ABI導入時：Linux x64の共有ライブラリとC11クライアントによる試験。分割出力、時刻余り、clone/reset、s16、イベント、引数検証、直接コアとの4096フレーム一致。詳細は初回導入時の履歴。
- KON修正時：C1/M2指定、全chのキー指定、3音色の発音を確認。[検証](../KEY_ON_MAPPING_FIX.md)
- 状態保存API追加時：コアとDLLの保存・復元、フィルターと端数クロック、復元後4096フレーム一致、破損データ拒否。[仕様と範囲](../STATE_API.md)、[当時のログ](../STATE_VALIDATION.txt)

これらは各変更段階での実行記録であり、docs-review-20260925の文書訂正で再実行した試験ではない。

## Windowsビルドと未確認範囲

VS2022用build_dll_windows.batはWin32/x64をビルドし、C ABI・直接コア一致・状態保存の試験を実行する構成。配布元提供のx64 DLLのPE形式と公開名を確認したが、当該記録の環境でWindows上のロード・音声再生・試験実行は行っていない。
VS探索と/link指定の修正経緯は初回導入時の履歴に保持する。現行の実装全体が導入前アーカイブとバイト一致するとは主張しない。
