# 現行配布物のライセンス / Current distribution

Copyright (C) 2026 by I.C.KaZe

本配布物のプロジェクトコード、独自解析コード、生成したMDX、説明文には[LICENSE.txt](../LICENSE.txt)のBSD Zero Clause License（SPDX: 0BSD）を適用します。

本バージョンは、公開マニュアルの仕様と、YM2151実機の録音・挙動測定から得た規則に基づく実装です。過去に外部実装を参照したLFO処理については、測定に基づく処理へ置き換えています。実装の根拠、検証範囲および未確認事項は検証資料に記載しています。開発全体について「外部ソースを一度も参照していない」と主張するものではありません。

## 配布内容と外部依存

本配布には第三者エミュレーターの実装、第三者のソースアーカイブ、マニュアル本文やチップROMの抽出データを含めません。従来THIRD_PARTY_NOTICES.txtにあった現行配布物の説明は本節へ統合しました。

C++標準ライブラリ、Python、NumPy、SciPy、Matplotlib、ffmpegなどの外部ツールは別途導入し、それぞれのライセンスに従います。これらを含む実行環境やバイナリを再配布する場合は、その配布内容に応じた確認が必要です。元録音は同梱せず、抽出した測定数値を収録しています。

## 根拠資料と履歴

- [周期LFOと未確認事項](PERIODIC_REPLACEMENT.md)
- [測定系列の推定](INFERRED_SEQUENCE_0.11b.md)
- [過去の移行・訂正記録](history/README.md)

過去の版の許諾や参照経緯を遡って変更するものではありません。測定への適合は、ハードウェア完全一致や権利関係の法的保証を意味しません。

## English

The project code, original analysis code, generated MDX files and documentation in this distribution are offered under 0BSD. This version is based on published specifications and measurements of YM2151 output and behavior. LFO processing previously based on external implementation references has been replaced with measurement-based processing. This does not assert that external source code was never consulted during development. Historical records are separated from current documentation; external tools retain their own licenses.
