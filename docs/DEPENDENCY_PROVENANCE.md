# 依存部品の出自 — 2.10.0

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
