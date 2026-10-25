# 配布通知の照合 — 2.10.0

確認日：2026-10-25。対象は公開用ソースZIPとWindows x64実行用ZIPです。

| 対象 | 確認内容 |
| --- | --- |
| GUI・コア | MSVC Release、静的Visual C++ランタイム（/MT）。Go製EXE・MinGW製コアとする旧記述を除去 |
| 不要な通知 | Go.txt、GCC-Runtime.txt、MinGW-w64.txt、Boost-1.0.txt、GPL/LGPL全文の計8ファイルを両ZIPから除外し、現行通知の参照も除去 |
| 提供ブリッジ | バイナリを無改変で保持。Zig 0.13.0由来の全通知を、MinGW-w64 COPYING・関連ファイルの通知を含めて保持。詳細は[出自記録](DEPENDENCY_PROVENANCE.md) |
| ブリッジ通知の保全 | 旧パッケージのZig以降のライセンス本文を保全。Workbench自身の出自文書への参照だけをソースアーカイブと明記し、他の全文が同一であることと更新後のSHA-256を機械検査 |
| その他の部品 | Independent OPM、IndependentMDX、NoteMDX、PDXEditor、zlibの通知と権利表示を保持 |
| 参照楽曲 | STAGE1.musの無改変ハッシュ・CC BY 4.0・I.C.KaZeとタイトル権利表示を保持 |
| 構成・設定 | 実行用を利用者向け収録物に限定し、個人設定を同梱・削除しない。ソースからの再ビルドとバイナリの依存関係・デバッグ情報を検査 |

現行ライセンスの適用範囲は[THIRD_PARTY_NOTICES.txt](../THIRD_PARTY_NOTICES.txt)を参照してください。この記録は同梱物と通知の照合であり、権利関係の法的評価を証明するものではありません。
