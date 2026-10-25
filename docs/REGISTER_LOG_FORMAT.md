# MDXRLOG 1.0 / 1.1 / 1.2 — MDX logical FM / PCM log

公開バイナリ仕様 / Public interchange specification, 2026-10-12

本資料はIndependentMDXの公開交換仕様です。NOTE-MDX-Workbench v2.9.3はGUIから1.2を出力し、使用PDXを含めます。本文のCLIスイッチや旧形式への切り替えはIndependentMDXの機能であり、Workbenchのコマンドライン機能ではありません。

この文書だけで互換ライター・リーダーを実装できます。特定のMXDRV実装のソース、内部構造、シンボルを必要としません。目的はMDXシーケンサーの命令解釈・ソフトウェアLFO・タイミングの比較です。音源合成の精度を評価する形式ではありません。

> 第1～8節は従来の1.0契約です。1.1では第9節、1.2ではさらに第10節の拡張が優先されます。1.0のレコードとヘッダーの意味は変更しません。0.31の既定出力は1.2、`--mrl-v1`は1.0です。

## 1. 共通規則

- 拡張子 `.mrl`、非圧縮。64バイトヘッダーに16バイト固定長レコードを連続させます。末尾タグ・終了レコード・パディングはありません。
- 整数はすべて符号なし、リトルエンディアン。C/C++の構造体を直接保存せず、明示的にバイト列へ変換してください。
- ファイル長は厳密に `64 + event_count * 16`。読取側は乗算オーバーフロー、切れたレコード、不正値を拒否します。
- 時刻は壁時計・処理に要した時間ではなく、演奏上の仮想経過時間です。`seconds = timestamp / timebase_hz`。
- 記録対象はYM2151への論理レジスタ書き込み。アドレスポート／データポートを分けず、1回のデータ書き込みを1レコードにします。読み出し・BUSY待ち・ホスト転送遅延・音声キュー遅延は対象外です。
- 同値の連続書き込みを削除しません。時刻は非減少、同時刻ではファイル内の順序が実際の論理書き込み順序です。
- レジスタ番号・値は音源DLLや実機転送層向けの補正前に記録します。KONのビット入れ替え、外部音源用音量補正、デバイス初期化によって演奏データを書き換えてはいけません。

## 2. ヘッダー（64バイト）

| Offset (hex) | Bytes | Name | 1.0での意味 |
| --- | ---: | --- | --- |
| 00 | 8 | magic | `4D 44 58 52 4C 4F 47 00` = `MDXRLOG\0` |
| 08 | 2 | major | 1 |
| 0A | 2 | minor | 0 |
| 0C | 4 | header_size | 64 |
| 10 | 4 | record_size | 16 |
| 14 | 4 | flags | 1。bit 0=完全に書き終わったファイル。残りは0 |
| 18 | 8 | timebase_hz | 1秒あたりの時刻単位数。1以上。X68000比較プロファイルでは4000000 |
| 20 | 8 | event_count | 全phaseを含むレコード数 |
| 28 | 8 | end_timestamp | 記録終了時刻。最後のレコードの時刻以上 |
| 30 | 4 | requested_loops | 指定したループ回数。1～1000。実際に完了した回数ではない |
| 34 | 4 | completion | 1=通常終了または指定ループ条件を満たして終了。その他は1.0では予約 |
| 38 | 4 | source_crc32 | 入力MDXファイル全バイトのCRC-32/ISO-HDLC |
| 3C | 4 | timestamp_quantum | 実際の時刻量子化幅。1以上、timebase_hzと同じ単位 |

CRC: reflected polynomial `0xEDB88320`, initial `0xFFFFFFFF`, final XOR `0xFFFFFFFF`; ASCII `123456789` の結果は `0xCBF43926`。CRCは比較対象の取り違えを見つける補助であり、暗号学的同一性証明ではありません。厳密な入力同一性は別途SHA-256等で確認してください。PDX・ファイル名・パスはCRC対象外です。

不完全・中断・時間上限超過の出力を完成ファイルとして公開してはいけません。エラー情報は別ファイルへ出します。将来版の未知major/minor、非ゼロ予約値を1.0読取側が黙って解釈してはいけません。

## 3. イベント（16バイト）

| Relative offset | Bytes | Name | Meaning |
| --- | ---: | --- | --- |
| 00 | 8 | timestamp | 仮想時刻 |
| 08 | 1 | chip | 0=YM2151 #0。1.0では0のみ |
| 09 | 1 | address | YM2151レジスタ番号 0～255 |
| 0A | 1 | value | 書き込み値 0～255 |
| 0B | 1 | phase | 0=演奏、1=演奏前初期化、2=演奏終了後のホスト側後処理 |
| 0C | 4 | reserved | 0 |

phase 1は時刻0で、phase 0より前。phase 2は全phase 0の後。phaseの並びは `1* 0* 2*`（各区間は省略可）。曲の命令やドライバーの通常処理が行うキーオフ・音色設定・LFOリセットはphase 0です。phase 1/2へ移して差分を隠してはいけません。比較ツールは初期化・後処理を除く厳密な演奏比較と、全イベント比較を区別してください。

例: 時刻64、YM2151レジスタ0x60へ0x1Aを書き込む演奏イベント:

```text
40 00 00 00 00 00 00 00  00 60 1A 00  00 00 00 00
```

## 4. X68000論理時刻比較プロファイル

timebase_hz=4000000。時刻0は初めてMDXの演奏シーケンサーを処理する時点で、ファイル読み込み開始や音声デバイス開始ではありません。準備に要する実時間は含めません。

タイマー割り込み駆動の参照ツールでは、同じサービス回で発行された書き込みはその回の仮想開始時刻へ揃え、順序を保持してください。CPU命令の実行時間やI/OのBUSY待ちで個々の時刻をずらす「実バスアクセスログ」は、このプロファイルとは異なります。外部要因で発生する割り込み遅延も混ぜません。MDX内の待機やドライバーが論理的に別時点へ延期した書き込みは、対応する別時刻を保持します。

IndependentMDX 0.28のtimestamp_quantumは64（16マイクロ秒）です。内部62,500Hz時刻を64倍し、44.1kHzへの丸めは行いません。他のライターは自身の実際の精度を記録し、IndependentMDXの精度へ無理に落とさないでください。量子化幅は精度の宣言であり、誤差を自動的に許容する指示ではありません。

初期条件: 通常速度、移調0、マスクなし、音色編集なし、音量補正なし、フェード時間0。ホスト側の全レジスタクリアを演奏に追加しません。曲に含まれる直接レジスタ操作をそのまま記録します。

PCM音声・PDXアクセスは記録しません。ただしPCMトラックの命令・待機・同期・終了・ループ判定を無視してはいけません。PCMトラックの解釈からFM側への作用が起きる場合も、ライターの実際の論理書き込みとして残します。

requested_loopsは各ライターに与えた設定値です。IndependentMDXは終了していない全トラックのループカウンターが指定回数以上になった処理回で停止を決めます。その処理回で既に発行されたイベントを残します。ループしない曲は全トラック終了後のリリース待ち（約0.5秒）を経て終了します。無音の待ち時間はend_timestampに含まれます。末尾の無音時間だけの違いをLFO差と混同しないでください。

参照ドライバーで終了境界が異なる場合は、その境界差を報告し、共通の演奏区間で比較してください。出力を合わせるために終端付近の書き込みを無条件で削除することは避けます。

## 5. 比較手順

1. 入力MDXの同一性、requested_loops、初期条件を確認。
2. まずphase 0をファイル順に比較。レジスタ・値・時刻・順序の最初の不一致と前後のイベントを提示。
3. timebaseが違う場合、浮動小数点ではなく整数の交差積で時刻を比較（オーバーフローしない多倍長整数等を使用）。
4. 時刻だけの違い、イベントの挿入／欠落、値の違いを別々に集計。同値書き込みの削除や並べ替えは既定では行わない。
5. 参照ドライバーがYM2151タイマーでシーケンサーを駆動する場合、タイマー制御レジスタの書き込みが独立したタイマーを使う実装と異なることがあります。厳密比較では残し、比較レポートではタイマー駆動方式の差と音程・音量・LFO制御の差を分けます。CSM等へ作用する可能性があるため、タイマー関連レジスタを一律削除しません。
6. 補助表示としてレジスタ別履歴、時間差の推移、同値書き込みを無視した比較を出しても、厳密比較の結果を置き換えない。

この形式は比較用データの仕様です。IndependentMDXの出力がMXDRVの正解であることを意味しません。他者のMXDRVソースを本プロジェクトの実装・検証に持ち込む必要はありません。

## 6. Pythonでの最小読み出し例

```python
import struct
from pathlib import Path
b = Path("song.mrl").read_bytes()
assert len(b) >= 64
h = struct.unpack_from("<8sHHIIIQQQIIII", b)
magic, major, minor, hs, rs, flags, hz, count, end, loops, completion, crc, quantum = h
assert (magic, major, minor, hs, rs, flags) == (b"MDXRLOG\0", 1, 0, 64, 16, 1)
assert hz > 0 and quantum > 0 and 1 <= loops <= 1000 and completion == 1
assert len(b) == hs + count * rs
previous = 0
stage = 0
for i in range(count):
    t, chip, address, value, phase, reserved = struct.unpack_from("<QBBBBI", b, hs + i * rs)
    assert previous <= t <= end and chip == 0 and reserved == 0
    rank = {1: 0, 0: 1, 2: 2}[phase]
    assert rank >= stage and (phase != 1 or t == 0)
    stage, previous = rank, t
    print(i, t, hz, f"{address:02X}", f"{value:02X}", phase)
```

## 7. English implementation contract

All integers are unsigned little-endian. Header layout is Python struct `<8sHHIIIQQQIIII` (64 bytes), in the order listed above. Records are `<QBBBBI` (16 bytes): timestamp, chip=0, register, value, phase, reserved=0. Magic is `MDXRLOG\0`, version 1.0. File length must equal 64+16*count. Complete files have flags=1 and completion=1; reserved bits must be zero.

Use virtual logical time, not wall time or physical I/O completion time. Preserve duplicate writes and ordering at equal timestamps. Capture before device-specific register remapping or gain correction. Phase 1 is pre-play initialization at zero; phase 0 is all musical driver writes, including musical resets and key-offs; phase 2 is host teardown. The allowed order is 1*,0*,2*.

The X68000 comparison profile uses 4,000,000 ticks per second. Zero is the first sequencer service. Writes in one timer service share that logical service time, retaining their order; intentionally deferred writes retain their later time. IndependentMDX reports a 64-tick timestamp quantum. Other writers must report their own actual quantum. No automatic timing tolerance is implied.

CRC-32/ISO-HDLC covers the exact source MDX bytes. Loops records the requested setting, not achieved loop count. Completion means natural or requested-loop completion; timeout/cancellation must not publish a complete file. PCM rendering is excluded, but PCM tracks must still participate in sequencing, synchronization and loop/end decisions. Compare termination differences separately from musical event differences. The IndependentMDX writer appends no synthetic initialization or cleanup writes and currently emits phase 0 only.

## 8. 同梱の互換性確認用データ

`samples/DEMO.MDX` は本プロジェクトのオリジナルテスト曲です。`samples/DEMO.mrl` は1ループの1.0形式確認用フィクスチャで、588イベント、9472バイトです。0.30.1でオリジナルテスト曲の部分オペレーター指定を訂正し、このフィクスチャの対応するキーON値と元MDX CRC32も更新しています。現行シーケンサーの新規出力とのバイト一致を保証するものではありません。CRCで元MDXを照合し、上記の最小読取コードで検査できます。これは形式確認用であり、他ドライバーの正解出力ではありません。

## 9. MDXRLOG 1.1: title, PDX identity/content and PCM8 controls

### 9.1 共通ヘッダー

最初の64バイトの配置は1.0と同じです。major=1、minor=1、record_size=16、flags=3（bit0=完了、bit1=PCMイベント・拡張メタデータを定義する形式、残り0）、completion=1。PCMイベントが0件でもflags=3です。`header_size`はイベント列の開始位置です。ファイル長は厳密に`header_size + event_count * 16`です。

### 9.2 拡張ヘッダーと可変データ

| Offset (hex) | Bytes | Field | Meaning |
| --- | ---: | --- | --- |
| 40 | 4 | title_bytes | MDX曲名UTF-8のバイト数。NUL終端なし。上限1 MiB |
| 44 | 4 | pdx_name_bytes | 元MDXのPDX参照名（CP932）のバイト数。NUL終端なし。上限65536 |
| 48 | 8 | pdx_size | PDXファイル全体のサイズ。未取得なら0。上限128 MiB |
| 50 | 4 | pdx_crc32 | PDX全バイトのCRC-32/ISO-HDLC。未取得なら0 |
| 54 | 4 | embedded | 0=本体なし、1=本体内蔵。その他禁止 |
| 58 | 8 | event_offset | header_sizeと同じ値 |
| 60 | 32 | reserved | すべて0 |
| 80 | title_bytes | title | UTF-8文字列 |
| 80+title_bytes | pdx_name_bytes | pdx_name | CP932参照名 |
| 80+title_bytes+pdx_name_bytes | embedded ? pdx_size : 0 | pdx_data | 元PDX全バイト、圧縮なし |

`header_size = 128 + title_bytes + pdx_name_bytes + (embedded ? pdx_size : 0)`。この合計が32-bitを超えるものは定義しません。PDXを内蔵しなくても、取得できればサイズとCRCを保存します。サイズ0は「未取得」であり、CRC0という値だけで未取得かを判定しません。元MDX CRCの対象にこのメタデータやPDXは含めません。

### 9.3 FMイベント

chip=0は1.0と同じです。address、value、phase、reserved=0を変更しません。FMだけを比較するツールはchip=0を抽出し、同時刻の相対順序を保持してください。新ライターは音源転送用の16µsキーON遅延等を記録せず、それ以前の論理書き込み時刻・順序を記録します。

### 9.4 PCMイベント

chip=1～8はPCM P～W。FMと同じtimestamp/timebaseと同一のレコード列を使います。phaseは0だけ。offset 09をoperation、0Aをvalue、0Cの4バイトをargumentとして使用します。

| operation | Name | value | argument |
| ---: | --- | --- | --- |
| 0 | START | デコード形式0～6 | PDXサンプル番号0～24575（bank×96+note） |
| 1 | STOP | 0 | 0 |
| 2 | PAN | 0 | 0=無音、1=左、2=右、3=両方 |
| 3 | GAIN | 0 | 線形ゲイン unsigned Q16.16、0～524288（8倍）。1倍=65536 |
| 4 | RATE | 0 | レート選択0～6 |

STARTは先頭からの新規発音です。現在のPAN/GAIN/RATEを使い、ADPCMデコーダー状態とサンプル位置をリセットします。STOPは直ちに再生を止めます。PAN/GAIN/RATEは再生中も即時反映します。STARTのvalueはその発音のデコード形式であり、後続RATEが既にデコードしたサンプル表現を変更するものではありません。サンプルを読み終われば自然停止します。

初期状態: 停止、PAN=3、GAIN=65536、RATE=4。ライターは各新規発音の直前に必要な状態を明示的に記録します。タイによる継続でサンプルを再スタートしない場合、STARTを追加してはいけません。

| 形式／RATE | デコード（START value） | 1秒あたりのデコード済みサンプル数（RATE） |
| ---: | --- | ---: |
| 0 | PDX ADPCM、下位nibble→上位nibble | 3906.25 |
| 1 | 同上 | 4000000/768 |
| 2 | 同上 | 7812.5 |
| 3 | 同上 | 8000000/768 |
| 4 | 同上 | 15625 |
| 5 | signed 16-bit big-endian PCM | 15625 |
| 6 | signed 8-bit PCM | 15625 |

PCM操作の一致はデコーダー、補間、ミキサー、出力音声の一致を保証しません。各ツールはPCM GAINへ元ドライバーの音量コマンドをどう換算したかを明示してください。IndependentMDXは既存MDX PCMゲイン関数の結果をQ16.16へ四捨五入して記録します。GUIのPCM gain・全体音量・マスク・移調は含めません。再生側で別途適用します。元PDXとSTART形式・サンプル番号の対応を検証し、見つからないサンプルを別のサンプルに置き換えてはいけません。

### 9.5 MRZ container

拡張子`.mrz`。ZIP仕様のStored/Deflateで、ちょうど1つの`.mrl`ファイルを格納します。IndependentMDXのライターはUTF-8名`record.mrl`、ZIP64、Deflateを使用し、追加ファイルを格納しません。ZIP名や圧縮結果をMRLの同一性判定に使用せず、展開後のMRLを比較してください。CRCはZIPの通常のCRCに従います。

### 9.6 English contract

MRL 1.1 keeps the first 64-byte layout and the 16-byte record layout. Set major=1, minor=1, flags=3. Header size points to events, not necessarily 64. The additional fixed header occupies bytes 64..127, followed by UTF-8 title bytes, CP932 PDX name bytes and optional raw PDX content. See the exact offsets above. Total length is header_size+16*count, and the 64-bit event_offset must equal header_size. All reserved bytes/bits are zero.

FM chip=0 retains its 1.0 definition. PCM chip=1..8 maps P..W; phase=0; operation occupies byte9, value byte10, and unsigned argument bytes12..15. START(0) selects sample bank*96+note and decoder format value; STOP(1) stops immediately; PAN(2) uses low L/R bits in argument; GAIN(3) is unsigned Q16.16; RATE(4) selects the exact rates above. START resets sample/decoder position but uses current pan/gain/rate. Rate changes do not reinterpret sample encoding. Defaults are stopped, pan3, gain65536 and rate4. End-of-sample stops naturally. All FM/PCM operations share a single ordered timebase. Host gain, masks and key shift are playback settings, not log data.

Use embedded PDX first; otherwise locate the named external bank and verify recorded size/CRC when pdx_size is nonzero. Missing/mismatched PDX leaves PCM unavailable and must not silently substitute another bank. MRZ is a ZIP container with exactly one MRL, generated as ZIP64 Deflate with member name record.mrl. Compare decompressed MRL content.

## 10. MDXRLOG 1.2 — loop metadata

0.31の既定ライターはmajor=1、minor=2、flags=3を出力します。1.1のタイトル・PDX・PCM形式を維持し、固定ヘッダーの予約領域96..127を次の4個のlittle-endian uint64へ変更します。1.0/1.1の意味は変更しません。

| Offset (decimal) | Field | Meaning |
| ---: | --- | --- |
| 96 | loop_start_time | ループ区間の開始。既存timebaseの単位 |
| 104 | loop_end_time | ループ区間の終了（含まない）。0ならループ指定なし |
| 112 | loop_start_event | 開始イベント番号（0起点） |
| 120 | loop_end_event | 終了イベント番号（含まない） |

有効な指定では`0 <= start_time < end_time <= end_timestamp`かつ`0 <= start_event < end_event <= event_count`。区間のイベントは`start_time <= timestamp < end_time`を満たし、start/end eventはそれぞれ時刻に対する最初のイベント番号です。同時刻イベントの一部だけを区間外へ切り離しません。ループ指定なしは4フィールド全て0です。

全イベント数と終了時刻は、指定周回数分を含むファイル全体を表します。ループ再生する読取側は、初期化／導入部分から初回のend_event直前まで再生し、start_eventへ戻って時刻基準を繰り返します。チップ／PCM状態を毎周リセットしません。ループ範囲後の記録は比較用の完全なログとして保持します。ログが末尾まで記録されていることと、再生用区間の長さは別です。

IndependentMDXのライターは主トラックの最初のループ境界を使います。より長い有限トラックがある場合や自然ループがない場合は、取得した演奏全体を区間にします。独立したトラックが互いに割り切れない周期で回る曲では、再記録されたMDXの全状態を完全に再現する保証はありません。

The 1.2 extension replaces the 32 reserved bytes at offset 96 with four uint64 fields: loop start time, exclusive end time, zero-based start event index, and exclusive end event index. Units use the existing timebase. All four zero means no loop metadata. Counts/duration still describe the full captured stream. Replay the intro once and repeat the selected range without resetting synthesis state. Readers that only support 1.0/1.1 must reject minor=2 rather than interpret it as the earlier reserved-byte contract.
