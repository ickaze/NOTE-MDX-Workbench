// SPDX-License-Identifier: 0BSD
// Copyright (C) 2026 by I.C.KaZe
#include "app.h"
namespace gui
{
static const std::map<std::wstring, std::wstring> translations = {
    {L"内蔵PDX編集を開けませんでした", L"Could not open the built-in PDX editor"},
    {L"出力を中断しますか？", L"Cancel the output?"},
    {L"出力中断の確認", L"Confirm cancellation"},
    {L"既存のファイルを上書きしますか？", L"Overwrite the existing files?"},
    {L"上書き確認", L"Confirm overwrite"},
    {L"出力完了", L"Export completed"},
    {L"MML自動スクロール", L"MML auto scroll"},
    {L"波形表示", L"Waveforms"},
    {L"変更した参照ファイルを保存しますか？", L"Save changes to the referenced file?"},
    {L"参照ファイルを保存", L"Save referenced file"},
    {L"ファイル監視", L"File watch"},
    {L"内蔵エディタ", L"Internal editor"},
    {L"テキスト:", L"Text:"},
    {L"参照", L"Browse"},
    {L"タイムスタンプ監視", L"Watch timestamp"},
    {L"編集名:", L"Name:"},
    {L"変換", L"Convert"},
    {L"再生(F5)", L"Play(F5)"},
    {L"一時停止", L"Pause"},
    {L"停止", L"Stop"},
    {L"一時停止(F6)", L"Pause(F6)"},
    {L"停止(ESC)", L"Stop(ESC)"},
    {L"全体音量", L"Master volume"},
    {L"出力", L"Output"},
    {L"WAVループ回数", L"WAV loops"},
    {L"フェード(ms)", L"Fade(ms)"},
    {L"音色表示", L"Instruments"},
    {L"音色編集", L"FM edit"},
    {L"PDX編集", L"PDX edit"},
    {L"PDXEditorを起動できません", L"Could not start PDXEditor"},
    {L"MIDI入力", L"MIDI input"},
    {L"発音モード", L"Voice mode"},
    {L"無効", L"Disabled"},
    {L"アフタータッチ", L"Aftertouch"},
    {L"キー（半音）", L"Key (semitones)"},
    {L"キー変更対象", L"Key mode"},
    {L"再生速度 %", L"Playback speed %"},
    {L"FMのみ", L"FM only"},
    {L"FM＋PCM（再生速度変更）", L"FM + PCM (playback rate)"},
    {L"FM＋PCM（長さを保つピッチシフト）", L"FM + PCM (preserve duration)"},
    {L"実測出力タイミング（Independent OPM DLL / ALG5・7）",
     L"Measured timing: Independent OPM DLL / ALG5,7"},
    {L"YM3012時差モデル: R +7.00 μs (Clock/64 → 48 kHz)",
     L"YM3012 timing model: R +7.00 us (Clock/64 → 48 kHz)"},
    {L"C86CTL FM遅延 ms", L"C86CTL FM delay ms"},
    {L"PCM遅延 ms", L"PCM delay ms"},
    {L"ピアノロール解析: ", L"Piano roll analysis: "},
    {L"ピアノロール", L"Piano roll"},
    {L"キャリア音程", L"Carrier pitch"},
    {L"中央: 現在 / 左: 履歴 / 右: 予定", L"Center: now / Left: history / Right: score"},
    {L"MML/MUSテキストを選択", L"Select MML/MUS text"},
    {L"PDXを選択", L"Select PDX"},
    {L"再生コアが起動していません", L"Playback core is not running"},
    {L"再生コアのABIサイズが不一致です", L"Playback core ABI size mismatch"},
    {L"再生コアのウインドウを作成できません", L"Cannot initialize the playback dispatcher"},
    {L"テキストファイルを指定してください", L"Select a text file"},
    {L"コンパイラ結果がバッファ上限を超えました", L"Compiler result exceeds the buffer limit"},
    {L"選択範囲のMMLを解析できません", L"Cannot compile the selected MML"},
    {L"演奏する音符・休符がありません", L"No notes or rests to play"},
    {L"WAV出力中です", L"WAV export is running"},
    {L"出力するトラックがありません", L"No tracks to export"},
    {L"WAV一括出力開始: ", L"WAV batch export started: "},
    {L"WAV保存完了: ", L"WAV export finished: "},
    {L"更新を検出しました。自動変換します。", L"File change detected. Converting automatically."},
    {L"解析待ち", L"Waiting for analysis"},
    {L"選択範囲: 解析エラー", L"Selection: analysis error"},
    {L"範囲: ", L"Selection: "},
    {L"最長: ", L"Longest: "},
    {L"小節", L" bars"},
    {L"起動エラー", L"Startup error"},
    {L"MMLに音色を追加しました: ", L"Instrument appended to MML: "},
    {L"音色の追加に失敗しました: ", L"Cannot append instrument to MML: "},
    {L"音色の追加位置を確認できません", L"Cannot resolve the instrument insertion point"},
    {L"MML保存", L"Save MML"},
    {L"更新を検出しました。自動変換して再生します。",
     L"File change detected. Converting and playing automatically."}};
std::wstring translate(const std::wstring &text)
{
    if (!app.english)
    {
        return text;
    }
    auto found = translations.find(text);
    if (found != translations.end())
    {
        return found->second;
    }
    std::size_t longest = 0;
    const std::pair<const std::wstring, std::wstring> *match = nullptr;
    for (const auto &entry : translations)
    {
        if (entry.first.size() > longest && entry.first.size() >= 2 &&
            entry.first.substr(entry.first.size() - 2) == L": " && text.rfind(entry.first, 0) == 0)
        {
            match = &entry;
            longest = entry.first.size();
        }
    }
    std::wstring out = text;
    if (match)
    {
        out = match->second + text.substr(longest);
    }
    if (match && match->first == L"範囲: ")
    {
        for (auto pair : {std::make_pair(std::wstring(L"最長: "), std::wstring(L"Longest: ")),
                          std::make_pair(std::wstring(L"小節"), std::wstring(L" bars"))})
        {
            std::size_t p = 0;
            while ((p = out.find(pair.first, p)) != std::wstring::npos)
            {
                out.replace(p, pair.first.size(), pair.second);
                p += pair.second.size();
            }
        }
    }
    return out;
}
void changeLanguage(bool english)
{
    app.english = english;
    for (const auto &caption : app.captions)
    {
        SetWindowTextW(caption.first, translate(caption.second).c_str());
    }
    auto index = send(app.keyMode, CB_GETCURSEL);
    send(app.keyMode, CB_RESETCONTENT);
    for (const auto *name : {L"FMのみ", L"FM＋PCM（再生速度変更）", L"FM＋PCM（長さを保つピッチシフト）"})
    {
        send(app.keyMode, CB_ADDSTRING, 0, LPARAM(translate(name).c_str()));
    }
    send(app.keyMode, CB_SETCURSEL, index);
    refreshMidi();
    if (app.ready)
    {
        app.core.command(26, int(english));
    }
    app.selectionRevision = UINT64_MAX;
    refreshSelection();
    saveSettings();
}
} // namespace gui
