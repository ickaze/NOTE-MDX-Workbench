# C++移行 — 2.10.0

製品のGUI、設定JSON・自動保存、ファイル入出力、監視、編集・選択解析の受信、RichEdit/TOM、モニター描画、出力ジョブの起動・完了・一時停止・中断をC++へ移行しました。音楽処理・圧縮・PDX編集もC/C++です。製品ソースに`.go`、go.mod、Go用sysoを含めず、Windows GUI・コアのビルドにGoを使用しません。

`native/tests/migration/reference_go/*.txt`は独立した比較基準のテキスト資料です。比較スクリプトが一時的なGoモジュールへ展開する検証用途だけに使用し、製品へリンクしません。移行前のソーススナップショットは比較専用であり、製品ビルドには使用しません。

既存のNOTE-MDX-Workbench.json、NOTE-MDX-Workbench.ini、PDXEditor.ini、自動保存名を引き継ぎます。JSONの未知項目も保存時に保持します。シーク位置は保存しません。ビルド・配布物の生成に個人設定を使用しません。

通常再生・各出力形式・編集・設定等の自動検証と、実音声・MIDI・外部機器・目視確認は区別します。後者は全置換後の実機検証対象です。検証範囲は[VALIDATION.md](docs/VALIDATION.md)、手順は[BUILD.md](docs/BUILD.md)を参照してください。
