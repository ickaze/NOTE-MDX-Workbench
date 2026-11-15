# 依存部品の出自 — 2.12.0

GUIとworkbench_core.dllはVisual Studio 2022のMSVCで構築したC/C++プログラムです。Visual C++ランタイムとzlibは静的リンクします。Goランタイム、libgcc、libstdc++、MinGW-w64のランタイムはこの2ファイルに含みません。

## 提供32bitブリッジ

IndependentMDX-X68Bridge32.exeはIndependentMDXの既存配布から引き継いだZig 0.13.0製のx86バイナリです。SHA-256は`3534a5524aa07eca5c60f1505c16a079dcaaa31154d5cf77e3a8dedf19f4f7d5`です。

バイナリのファイルオフセット0x47507に`Mingw-w64 runtime failure:`があります。これはZig 0.13.0同梱の[lib/libc/mingw/crt/pseudo-reloc.c](https://github.com/ziglang/zig/blob/0.13.0/lib/libc/mingw/crt/pseudo-reloc.c)にある診断文字列と一致します。同ファイルはEgor Duda、Kai TietzによるPublic Domainのコードとして表示されています。ZigのWindows向けCRTに由来するMinGW-w64部分を、GUI/コアのMSVCランタイムと区別します。個々のリンク対象の全一覧をこの文字列だけから確定するものではありません。

ブリッジの通知は[THIRD_PARTY_NOTICES.txt](../THIRD_PARTY_NOTICES.txt)後半にあるZig/LLVM/compiler-rt/musl等の全文を保持しています。その中の`Zig distribution: lib/libc/mingw/COPYING`は[Zig 0.13.0のCOPYING](https://github.com/ziglang/zig/blob/0.13.0/lib/libc/mingw/COPYING)と一致し、MinGW-w64のZPL 2.1と例外範囲を記載しています。関連するファイル別通知も同じ文書内に保持します。別の包括的なMinGW-w64.txtを重複して収録しません。

## アプリケーション部品と資料

- Independent OPM 0.14：0BSD。提供DLLのハッシュを固定し、ソース側のnative/independent-opm/に来歴を保持します。旧LFO実装の外部参照と置換の記録を削除・改変しません。
- IndependentMDX 0.33.5、NoteMDX 0.8 Revision 5、PDXEditor 1.1.1：各0BSD通知をlicenses/へ収録し、Workbench統合の変更を帰属表に記載します。
- zlib 1.3.1：選択ソースの原著作権・ライセンス・チェックサムを保持します。
- STAGE1.mus：[LICENSE-DATA.md](../LICENSE-DATA.md)と[参照資料の許諾](../licenses/NoteMDX-Reference-Data.txt)に従う無改変のCC BY 4.0資料です。
- アイコン：[assets/README.md](../assets/README.md)に制作来歴と適用範囲を記載します。
- 固定Goテキスト：Workbenchの0BSDテスト資料です。Goランタイムや製品ビルド依存には含まれません。

この文書はソース用の出自記録です。実行用ZIPには同梱せず、必要な許諾・通知はTHIRD_PARTY_NOTICES.txtとlicenses/に収録します。

## NDSIF追加（2026-11-15）

- IndependentMDX 0.34.0: https://github.com/ickaze/IndependentMDX 、基準コミット `b850b5330ee1a80bfb26d26a4f9e4dccfa0eaa22`。ndsif_output.h / ndsif_wire.h、通信模擬試験、P16/P8の確保量改善を取り込み。プレイヤー全体はWorkbench用の音符位置・選択ループ・MRL・音色編集を保持した適応版です。旧0.33.5の由来も引き続き適用します。
- PDXEditor_v1.1.0_NDSIF_source.zip、SHA-256 `f9e18c5641b3c7b6b9edc567aeb21cfd8e13ac30e8eec99d14e49bbd4eb3b6e6`。NdsifAudio.cpp/h、NdsifWire.h、通信試験、出力UIを既存1.1.1適応版へ選択的に取り込み。設定は既存のエディター共通設定へ統合し、従来の編集機能を保持。
- どちらのホストコードも0BSD。NanoDrive8ファームウェアのBSD-3-Clauseとは別物です。NanoDrive8のソース・バイナリは収録しません。

## 2.12.0の適応変更

NDSIFのポート共有、曲とエディタのPCM合成、消費位置コールバックはWorkbench独自の0BSD実装です。IndependentMDX 0.34.0の通信実装へ共有用フックを追加し、PDXEditorにはホスト接続と従来の単独接続を選ぶ内部ルートを追加しています。上流から提供された無改変バイナリは変更しません。
