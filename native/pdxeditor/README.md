# PDXEditor 1.1.1 — Workbench integration

PDXEditor_v1.1.1_source_static_runtime.zipを基に、NOTE-MDX-Workbench 2.10.0-alpha.8のworkbench_core.dllへ組み込んだソースです。App.cpp、Audio.cpp、Pdx.cpp、Settings.cppをホストDLLにリンクします。単体main.cppはホストから使用しません。

組み込み用の変更は、専用スレッドの起動・終了通知、ウインドウクラス名、保存失敗時の終了保留、スレッド内の作業フォルダー、OFN_NOCHANGEDIR、実行時のpdx_languages/です。プロジェクト形式.pdxedit、PDX出力、PDXEditor.iniの設定形式と静的C++ランタイム設定を維持します。

操作とホストからの起動はプロジェクトルートのREADME.mdを参照してください。原版の説明はUPSTREAM_README.mdに保持しています。そこに記載された単体EXEの起動引数はWorkbench本体の起動引数ではありません。単体CMake／Visual Studioプロジェクトは必要に応じて使用でき、言語ファイルの出力先はpdx_languages/です。

```sh
python3 tests/run_portable_checks.py
```

このテストはg++／Python 3を使用し、Windows GUIを含まないコア・形式・言語・ビルド設定を検証します。ライセンスはLICENSE（0BSD）、原版の依存関係通知はTHIRD_PARTY_NOTICES.mdを参照してください。

Workbench連携では起動時のMUSディレクトリを保存先として保持します。PDXEDITがなければ同名PDXを確認なしで読み込み、PCM展開先の上書きのみ確認します。PCMはEXE/PDX名、PDXEDITとPDXはMUS横です。手動インポートの置換確認は保持しています。

手動PDXインポートでは設定済みPDX名とプロジェクト保存先を保持し、PCM展開には入力PDX名を使います。保存の再入を防止し、モーダル操作・読み込み・変換中の自動保存を保留します。上書き拒否・保存失敗後は次の編集まで自動再試行を行いません。`python3 tests/run_save_checks.py`は実際の保存メソッドとインポート先決定部分を抽出し、再入・拒否・失敗・再試行・名前保持を検証します。
