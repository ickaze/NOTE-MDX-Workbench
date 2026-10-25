# 構成 — 2.10.0

`native/gui/`がWin32メインGUI、RichEdit編集、設定・自動保存、文書、監視、描画、出力ジョブを管理します。`native/frontend/`が共通テキスト計算・圧縮・公開・解析キューを担当します。`native/src/workbench_bridge.cpp`はC ABIでGUIと音楽処理を接続します。解析結果はネイティブ所有のJSONとして受信し、世代・改訂・選択範囲で古い結果を拒否します。終了時はワーカーをjoinしてからコアを終了します。

`workbench_core.dll`へNoteMDX、IndependentMDX由来の再生処理、PDXEditorを組み込みます。Independent OPM DLL、任意の32bit X68Sound用ブリッジ、64bit C86CTLを従来の音源選択に使用します。設定の役割・ファイル名はREADME.mdに記載しています。

出力は開始時のソースと設定を固定します。一時データを生成し、成功後に出力先へ反映します。中断と失敗では未反映の既存ファイルを保持し、一時ファイルを削除します。Track WAVの反映途中で失敗した場合、反映済みトラックは残ります。

Windows ReleaseはMSVCの静的ランタイムを使用します。Goランタイム・Go接続層は含みません。移行の比較基準はテキスト資料としてのみ保持します。
