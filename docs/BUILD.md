# ビルド — 2.10.0

Windows版はVisual Studio 2022の「C++によるデスクトップ開発」とCMake 3.20以降を使用します。WindowsのビルドはMSVC専用です。MinGW向け設定は含みません。Go・vcpkg・外部ソース取得は不要です。

`build_windows.bat`を実行します。CMakeのVisual Studio 2022 x64ジェネレーターでReleaseをビルドし、`bin/NOTE-MDX-Workbench.exe`、`bin/workbench_core.dll`、`bin/pdx_languages/ja.lang`と`en.lang`を配置します。既存のJSON・INI・自動保存ファイルを削除・上書きする処理はありません。Visual Studioのインストール先とvswhere.exeは固定しません。

GUIとコアはC++17、MSVC `/MT`、Release `/DEBUG:NONE`です。zlib 1.3.1を静的リンクします。Windows標準DLLを使用し、独立した音源DLL・X68Sound/C86CTLを選択する設計は保持します。Independent OPM 0.14 x64と32bit X68Bridgeは提供バイナリを保持します。X68Sound.dllとC86CTL.dllは含みません。

## 自動テスト

```sh
cmake -S . -B build-tests -DBUILD_TESTING=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build-tests --config Release --parallel 2
ctest --test-dir build-tests -C Release --output-on-failure
```

C++単体テストは9件です。Linuxでは音源ソースを直接リンクし、GUIは作りません。Python 3とGoによる旧実装比較は`-DWB_ENABLE_REFERENCE_TESTS=ON`を明示した検証専用構成です。通常の製品ビルドには登録しません。

Windowsではビルド後にPowerShell 7で`test_frontend_windows.bat`を実行します。EXEとDLLを`build-ci/cpp-gui-ja`と`cpp-gui-en`へコピーし、専用設定で日英のGUIスモークを実行します。実利用中の設定・自動保存に対して`--smoke`を直接実行しないでください。音声機器・MIDI・外部機器の確認範囲はVALIDATION.mdを参照してください。

アイコンは`assets/app.rc`からGUIへリンクします。`scripts/verify_icon.py`と`verify_pe_layout.py`で実際のPEを検査します。Go用sysoやリンク後のセクション削除は使用しません。
