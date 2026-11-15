# PDXの形式と再生指定 — 2.12.0

PDXのサンプル表には、ADPCM/P16/P8を確実に識別するタグがありません。拡張子やデータの見た目から自動判定して再生形式を変更しません。PDXEditorで選んだ形式に合わせ、MUSの各PCMトラックで最初の音より前に次を指定します。

| PDXEditorの形式 | MUS | サンプル形式・レート |
| --- | --- | --- |
| ADPCM | F0〜F4（通常F4） | OKI ADPCM。F4は15,625 Hz |
| P16 (16bitPCM) | F5 | 符号付き16bit、ビッグエンディアン、15,625 Hz |
| P8 (8bitPCM) | F6 | 符号付き8bit、15,625 Hz |

例えばP/Q/R/VでP16を使用するなら `PQRV F5`、全PCMトラックなら `PQRSTUVW F5` を、対象トラックの最初の音より前へ追加します。形式を途中で戻す命令がないかも確認してください。PCMの形式指定はFMトラックのF命令とは意味が異なります。形式の異なるPDXへ変更する場合はMUSも合わせて変更します。

P16を既定のF4で読むと各16bitサンプルを4個のADPCMコードとして扱うため、ノイズ・音程/音長の不一致になります。PDXEditorの試聴が正常でも、MUS側の指定が不足していると曲の再生は一致しません。F5の指定はソフトウェア再生、NDSIFへのPCMミックス、WAV/VGM/VGZの生成に共通です。全バンクをP16へ変換した場合は、ボーカル以外のドラム等もF5が必要です。

P16/P8のPDX出力完了時は、PDXEditorのログと完了ダイアログにも必要なF指定を表示します。元のMUSやユーザー設定は自動変更しません。

## English

PDX has no reliable sample-encoding tag. Use F5 on every PCM track reading P16 data, or F6 for P8, before its first note. P16 is signed big-endian 16-bit PCM at 15,625 Hz. Reading it as default F4 ADPCM produces noise and interprets four times as many samples. All converted banks, including percussion, need the matching track command. Export completion shows a reminder; MUS is never rewritten automatically.
