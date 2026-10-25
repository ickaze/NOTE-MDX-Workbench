# 録音名とMDX名 / Recording names and MDX names

単独録音はMDXと同じベース名に統一しました。拡張子は実ファイルのWAV/FLACを維持します。
例: 13CAL.MDX → 13CAL.flac、37AMSAW.MDX → 37AMSAW.flac。
再録の識別はディレクトリで行います。takes/initial/25F00.flac は不完全な初回録音です。

combined/00_07.wav、combined/06_07.wav、combined/09_11.wav は複数MDXの連続録音です。
単独MDXと同一名にすると内容を偽るため、番号範囲で示します。これらの元MDX一式は回収したパッケージにありません。
26A.flac / 26B.flac は分割録音の識別子です。分割MDX実物が回収できていないため、元の正確なベース名との一致は未確認です。
08LONG.wavと12TRANS.wavは検証資料の試験名と対応します。

RECORDING_INDEX.jsonに録音名、MDX対応、SHA-256、収録時間を収録しました。
元録音のバイト列は変更しません。入力を再実行するときは、この名前・配置でコピーしてください。
計測条件・録音の再実行を不要にする変更ではありません。数値データと実装は変更していません。

Single recordings use the MDX basename and retain their real WAV/FLAC extension.
Takes are separated by folders. Combined captures use sequence-number ranges; they are not falsely attributed to a single MDX.
The original split stimuli for 26A/B and full stimuli for the combined captures were not recovered; those exact basename correspondences remain unverified.
The index retains audio byte hashes for traceability without relying on upload filenames.
