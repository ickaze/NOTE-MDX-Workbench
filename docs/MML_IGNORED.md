# MMLの無視範囲と表示 — 2.12.0

`? ... ?` で囲んだMMLは解釈・マクロ展開・音の生成を行いません。範囲内の音長、音量、オクターブ等の命令も状態へ反映しません。閉じる`?`がない場合は、そのトラックの残りをすべて無視します。未閉鎖をエラーにはしません。`!`はその位置からトラックの残りを無視します。どちらも別トラックのMMLやファイル全体を終了する命令ではありません。

```text
A c4 ? d4 e4 ? f4
B c4 ! d4
A ? この後はAトラックの残りを無視
B e4
```

この例ではAはc4/f4、Bはc4/e4を生成します。対になった`?`の範囲内も、2.12.0から命令自体を無視する扱いに統一しています。旧版の「音は出さずに一部の設定だけ変更される」挙動へ依存したMMLは、必要な設定を無視範囲の外へ移してください。

エディタではコメント行に加え、ソースに記述された`?`の範囲と`!`以降のMML本文を暗く表示します。同じトラックの後続行にも状態を引き継ぎます。トラック名部分、音色定義、全体ディレクティブを一律には暗くしません。複数トラック共通行は、いずれかの対象トラックで有効な文字を明るく表示します。マクロ定義内の明示的な範囲も暗く表示します。マクロ呼び出しで発生する状態変更については、呼び出し先の定義も確認してください。

MDX変換ログはLF・CRLF・CRをWindowsの複数行表示用CRLFへ統一します。MUSの保存文字コード・改行コードや編集内容は変更しません。

## English

`? ... ?` skips interpretation, macro expansion and emitted notes, including state-changing commands. An unmatched `?` ignores the remainder of that track like `!`; it is not an error. Other tracks remain active. This makes paired-range behavior a full skip as of 2.12.0. Move required state changes outside the ignored range. Explicit ignored source ranges are dimmed in the editor, with per-track continuation. MDX conversion logs normalize LF/CRLF/CR for multiline Windows display without rewriting source files.
