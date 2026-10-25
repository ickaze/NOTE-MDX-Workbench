> Workbench収録範囲：この文書が挙げるresearch資料・録音・上流PACKAGE_MANIFEST.jsonは非同梱です。[資料の区別](../../UPSTREAM_MATERIALS.md)を参照してください。記載の測定・検証は上流の該当段階の記録です。

# 由来と撤去記録

旧0.10bおよび先行0.11bには、当時提供されたX68Sound src020615を参照したLFO処理がありました。周期LFOの置換時には、名称と許諾表示だけでなく対象の実装と参照比較試験を置き換えています。

- LfoClockを撤去。PeriodicModulatorで実測の反転順の進み幅を実装。
- lfo_valueを撤去。実測波形の数値規則からperiodic_valuesを実装。
- write_register/synthesizeの関連処理を新しい状態・整数深度/感度へ接続。
- tests/reference_lfo.cpp、run_reference_check.pyを撤去。原版の読込/比較は不要。
- test_precision内の参照由来LFO期待値を撤去。test_periodicを追加。
- reference_hashes.json、旧再作成報告、旧コードを含む2本のpatchを周期LFO置換後の配布から除外。

公開マニュアルの数値仕様に基づく他の音源処理、出力タイミング測定モデル、独自ランダムモデルは継続します。系列の17次という性質は測定結果として記録します。

research/recording-analysis.zipのファイル一覧とテキストも点検しています。上流パッケージの配布一覧とハッシュは上流のPACKAGE_MANIFEST.jsonに記録されています。同ファイルとresearch資料はWorkbenchの抜粋には含めません。これは過去の外部参照を隠すための履歴書き換えではありません。
