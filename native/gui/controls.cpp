// SPDX-License-Identifier: 0BSD
// Copyright (C) 2026 by I.C.KaZe
#include "app.h"
namespace gui
{
void browsePdx();
static HWND combo(int x, int y, int w, int id, std::initializer_list<const wchar_t *> entries,
                  HWND parent = nullptr)
{
    auto h = create(
        L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL | CBS_DROPDOWNLIST | WS_TABSTOP, x, y, w, 400,
        [&]()
        {
            if (parent)
            {
                return parent;
            }
            return app.hwnd;
        }(),
        id);
    for (const auto *entry : entries)
    {
        send(h, CB_ADDSTRING, 0, LPARAM(translate(entry).c_str()));
    }
    send(h, CB_SETCURSEL, 0);
    return h;
}
static HWND trackbar(int x, int y, int w, int id, int low, int high)
{
    auto h = create(TRACKBAR_CLASSW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP, x, y, w, 32, app.hwnd, id);
    send(h, TBM_SETRANGE, TRUE, MAKELPARAM(low, high));
    SetWindowSubclass(h, sliderProc, 8, 0);
    return h;
}
void createControls()
{
    auto parent = app.hwnd;
    auto label = [&](const wchar_t *s, int x, int y, int w)
    { return create(L"STATIC", s, WS_CHILD | WS_VISIBLE, x, y, w, 24, parent, 0); };
    auto edit = [&](const wchar_t *s, int x, int y, int w, int id)
    {
        return create(L"EDIT", s, WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL, x, y, w, 24, parent,
                      id);
    };
    auto button = [&](const wchar_t *s, int x, int y, int w, int id, DWORD style = BS_PUSHBUTTON)
    { return create(L"BUTTON", s, WS_CHILD | WS_VISIBLE | WS_TABSTOP | style, x, y, w, 28, parent, id); };
    app.modeFile = button(L"ファイル監視", 10, 10, 110, ID_MODE_FILE, BS_AUTORADIOBUTTON | WS_GROUP);
    app.modeEdit = button(L"内蔵エディタ", 125, 10, 110, ID_MODE_EDIT, BS_AUTORADIOBUTTON);
    label(L"Language", 295, 14, 70);
    app.languageControl = combo(370, 10, 135, idLanguage, {L"日本語", L"English"});
    send(app.languageControl, CB_SETCURSEL, int(app.english));
    label(L"テキスト:", 10, 45, 55);
    app.srcEdit = edit(L"", 65, 42, 380, ID_SRC_EDIT);
    button(L"参照", 450, 42, 55, ID_SRC_BROWSE);
    button(L"音色編集", 10, 71, 70, idLiveVoices);
    app.pdxButton = button(L"PDX編集", 84, 71, 70, idPDXEdit);
    label(L"WAVループ回数", 160, 76, 85);
    app.wavLoopsEdit = edit(L"2", 248, 71, 50, ID_WAV_LOOPS_EDIT);
    label(L"フェード(ms)", 310, 76, 75);
    app.fadeMsEdit = edit(L"2000", 388, 71, 70, ID_FADE_MS_EDIT);
    app.watchCheck = button(L"タイムスタンプ監視", 10, 104, 150, ID_WATCH, BS_AUTOCHECKBOX);
    label(L"編集名:", 165, 106, 45);
    app.editorName = edit(L"editor", 210, 102, 125, ID_EDITOR_NAME);
    app.convertBtn = button(L"変換", 345, 101, 70, ID_CONVERT);
    button(L"MML保存", 420, 101, 85, ID_MML_SAVE);
    app.playBtn = button(L"再生(F5)", 10, 145, 75, ID_PLAY);
    app.pauseBtn = button(L"一時停止(F6)", 85, 145, 105, ID_PAUSE);
    app.stopBtn = button(L"停止(ESC)", 195, 145, 90, ID_STOP);
    app.exportMixBtn = button(L"Mix WAV", 290, 145, 90, ID_EXPORT_MIX);
    app.exportTracksBtn = button(L"Track WAV", 385, 145, 100, ID_EXPORT_TRACKS);
    app.timeLabel = label(L"00:00:00", 10, 186, 65);
    app.seek = trackbar(75, 180, 430, ID_SEEK, 0, 1000);
    label(L"全体音量", 10, 216, 65);
    app.masterSlider = trackbar(75, 210, 350, ID_MASTER_VOL, 0, 400);
    app.masterLabel = label(L"100%", 430, 218, 60);
    label(L"FM A-H", 10, 252, 65);
    app.fmGain = trackbar(75, 244, 350, idFMGain, 0, 400);
    app.fmValue = label(L"200%", 430, 252, 60);
    label(L"PCM P-W", 10, 284, 65);
    app.pcmSlider = trackbar(75, 276, 350, ID_PCM_VOL, 0, 400);
    app.pcmLabel = label(L"100%", 430, 284, 60);
    label(L"出力", 10, 316, 55);
    app.outputControl =
        combo(70, 310, 225, idLiveOutput, {L"Independent OPM DLL", L"X68Sound.dll", L"C86CTL.dll"});
    label(L"MIDI入力", 10, 347, 70);
    app.midiControl = combo(80, 342, 215, idLiveMIDI, {});
    refreshMidi();
    label(L"発音モード", 305, 347, 75);
    app.poly = combo(380, 342, 125, idPoly, {L"Polyphonic", L"Monophonic"});
    app.measured = button(L"実測出力タイミング（Independent OPM DLL / ALG5・7）", 10, 373, 495, idMeasured,
                          BS_AUTOCHECKBOX);
    app.dac =
        button(L"YM3012時差モデル: R +7.00 μs (Clock/64 → 48 kHz)", 10, 399, 495, idDAC, BS_AUTOCHECKBOX);
    label(L"C86CTL FM遅延 ms", 10, 434, 110);
    app.fmDelay = edit(L"0", 125, 429, 50, idFMDelay);
    label(L"PCM遅延 ms", 180, 434, 90);
    app.pcmDelay = edit(L"0", 275, 429, 50, idPCMDelay);
    app.aftertouch = button(L"アフタータッチ", 335, 429, 170, idAftertouch, BS_AUTOCHECKBOX);
    label(L"キー（半音）", 10, 469, 90);
    app.key = trackbar(100, 462, 325, idKey, -24, 24);
    app.keyValue = label(L"0", 430, 469, 75);
    label(L"キー変更対象", 10, 498, 90);
    app.keyMode = combo(100, 494, 405, idKeyMode,
                        {L"FMのみ", L"FM＋PCM（再生速度変更）", L"FM＋PCM（長さを保つピッチシフト）"});
    label(L"再生速度 %", 10, 531, 100);
    app.speed = trackbar(110, 522, 315, idSpeed, 0, 200);
    app.speedValue = label(L"100%", 430, 531, 75);
    // PDX overrides are API-compatible backing controls; the built-in PDX editor is the normal entry point.
    app.pdxEdit = create(L"EDIT", L"", WS_CHILD, 0, 0, 1, 1, parent, ID_PDX_EDIT);
    app.lines = create(L"STATIC", L"1\r\n", WS_CHILD | WS_VISIBLE | WS_BORDER | SS_NOTIFY, 520, 10, 120, 470,
                       parent, ID_LINES);
    LoadLibraryW(L"Msftedit.dll");
    app.editor = create(MSFTEDIT_CLASS, L"",
                        WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL | WS_HSCROLL | ES_MULTILINE |
                            ES_AUTOVSCROLL | ES_AUTOHSCROLL,
                        640, 10, 542, 470, parent, ID_EDITOR);
    send(app.editor, EM_SETTEXTMODE, TM_RICHTEXT | TM_MULTILEVELUNDO | TM_MULTICODEPAGE);
    send(app.editor, EM_SETEDITSTYLE, 0, SES_ALLOWBEEPS);
    send(app.editor, EM_SETUNDOLIMIT, 512);
    send(app.editor, EM_EXLIMITTEXT, 0, 4 * 1024 * 1024);
    send(app.editor, EM_SETEVENTMASK, 0, ENM_CHANGE | ENM_SCROLL);
    send(app.editor, EM_SETBKGNDCOLOR, 0, RGB(20, 22, 48));
    app.splitterHwnd = create(L"STATIC", L"", WS_CHILD | WS_VISIBLE | WS_BORDER, 520, 484, 662, 6, parent, 0);
    EnableWindow(app.splitterHwnd, FALSE);
    app.log =
        create(L"EDIT", L"",
               WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY,
               520, 494, 662, 94, parent, ID_LOG);
    for (int i = 0; i < 16; ++i)
    {
        app.trackChecks[i] = create(L"BUTTON", std::wstring(1, names[i]), WS_CHILD | BS_AUTOCHECKBOX, 0, 0, 1,
                                    1, parent, ID_TRACK_BASE + i);
        setChecked(app.trackChecks[i], true);
        app.stepLabels[i] =
            create(L"STATIC", std::wstring(1, names[i]) + L": —", WS_CHILD | WS_VISIBLE, 522 + (i % 8) * 68,
                   602 + (i / 8) * 20, 64, 18, parent, ID_STEP_STATUS_BASE + i);
    }
    app.selectionLabel =
        create(L"STATIC", L"", WS_CHILD | WS_VISIBLE, 1080, 602, 100, 18, parent, ID_SELECTION_STATUS);
    app.mmdspPanel = create(L"MMDSPPanel", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | WS_CLIPCHILDREN, 10, 525,
                            1180, 260, parent, 0);
    auto monitorButton = [&](const wchar_t *s, int x, int w, int id)
    {
        return create(L"BUTTON", s, WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, x, 2, w, 24, app.mmdspPanel, id);
    };
    app.rollButton = monitorButton(L"ピアノロール", 245, 150, idMonitorRoll);
    app.carrierButton = monitorButton(L"キャリア音程", 405, 160, idMonitorCarrier);
    app.waveButton = monitorButton(L"波形表示", 575, 150, idWaves);
    create(L"STATIC", L"MML自動スクロール", WS_CHILD | WS_VISIBLE, 735, 2, 165, 24, app.mmdspPanel, 0);
    app.followControl = combo(905, 2, 90, idEditorFollow,
                              {L"OFF", L"AUTO", L"A", L"B", L"C", L"D", L"E", L"F", L"G", L"H", L"P", L"Q",
                               L"R", L"S", L"T", L"U", L"V", L"W"},
                              app.mmdspPanel);
    app.scroll = create(L"SCROLLBAR", L"", WS_CHILD | SBS_VERT, 0, 0, 18, 100, app.mmdspPanel, 0);
    app.monitorSplitter =
        create(L"STATIC", L"", WS_CHILD | WS_VISIBLE | WS_BORDER, 10, 520, 1180, 6, parent, 0);
    EnableWindow(app.monitorSplitter, FALSE);
    for (auto h : {app.convertBtn, app.exportMixBtn, app.exportTracksBtn})
    {
        SetWindowSubclass(h, buttonProc, 9, 0);
    }
    loadSettings();
    rememberSource();
    SetTimer(parent, 1, 100, nullptr);
    SetTimer(parent, 2, 33, nullptr);
}
void layout(int w, int h)
{
    if (w <= 800 || h <= 500 || !app.editor)
    {
        return;
    }
    auto move = [](HWND c, int x, int y, int width, int height)
    { MoveWindow(c, x, y, width, std::max(1, height), TRUE); };
    int ew = w - 528, monitorH = std::clamp(app.monitorHeight, 220, std::max(220, h - 574));
    app.monitorTop = h - monitorH - 10;
    move(app.monitorSplitter, 10, app.monitorTop - 8, w - 20, 6);
    move(app.mmdspPanel, 10, app.monitorTop, w - 20, monitorH);
    int maxSplit = app.monitorTop - 8 - 10 - 126 - 70 - 24;
    // Layout may shrink the visible editor, but must not overwrite the saved preference.
    app.splitterActual = std::clamp(app.splitterPos, 120, std::max(120, maxSplit));
    move(app.lines, 520, 10, 120, app.splitterActual);
    move(app.editor, 640, 10, ew - 120, app.splitterActual);
    int splitY = 14 + app.splitterActual, logY = splitY + 10,
        logH = std::max(70, app.monitorTop - 8 - logY - 126);
    move(app.splitterHwnd, 520, splitY, ew, 6);
    move(app.log, 520, logY, ew, logH);
    int statusY = logY + logH + 4;
    for (int i = 0; i < 16; ++i)
    {
        move(app.stepLabels[i], 522 + (i % 4) * (ew / 4), statusY + (i / 4) * 18, ew / 4 - 4, 18);
    }
    move(app.selectionLabel, 522, statusY + 74, ew - 4, 48);
    RedrawWindow(app.hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
    InvalidateRect(app.mmdspPanel, nullptr, FALSE);
}
void refreshMidi()
{
    static std::vector<std::wstring> old;
    int selected = int(send(app.midiControl, CB_GETCURSEL));
    std::wstring previous;
    if (selected > 0 && selected < int(old.size()))
    {
        previous = old[selected];
    }
    std::vector<std::wstring> entries{translate(L"無効")};
    for (UINT i = 0; i < midiInGetNumDevs(); ++i)
    {
        MIDIINCAPSW caps{};
        if (midiInGetDevCapsW(i, &caps, sizeof(caps)) == MMSYSERR_NOERROR)
        {
            entries.emplace_back(caps.szPname);
        }
        else
        {
            entries.push_back(L"MIDI " + std::to_wstring(i + 1));
        }
    }
    if (entries == old)
    {
        return;
    }
    send(app.midiControl, CB_RESETCONTENT);
    int next = 0;
    for (std::size_t i = 0; i < entries.size(); ++i)
    {
        send(app.midiControl, CB_ADDSTRING, 0, LPARAM(entries[i].c_str()));
        if (i > 0 && entries[i] == previous && next == 0)
        {
            next = int(i);
        }
    }
    if (selected > 0 && selected < int(entries.size()) && entries[selected] == previous)
    {
        next = selected;
    }
    old = entries;
    send(app.midiControl, CB_SETCURSEL, next);
    if (app.ready && selected > 0 && next != selected)
    {
        app.core.command(12, next);
    }
}
void syncControls()
{
    const auto &s = app.snapshot;
    app.master = int(s.masterPercent);
    app.pcm = int(s.pcmPercent);
    for (auto item : {std::make_pair(app.masterSlider, app.master), std::make_pair(app.pcmSlider, app.pcm),
                      std::make_pair(app.fmGain, s.fmPercent), std::make_pair(app.key, s.key),
                      std::make_pair(app.speed, s.speed)})
    {
        if (send(item.first, TBM_GETPOS) != item.second)
        {
            send(item.first, TBM_SETPOS, TRUE, item.second);
        }
    }
    setText(app.masterLabel, std::to_wstring(app.master) + L"%");
    setText(app.pcmLabel, std::to_wstring(app.pcm) + L"%");
    setText(app.fmValue, std::to_wstring(s.fmPercent) + L"%");
    setText(app.keyValue, std::to_wstring(s.key));
    setText(app.speedValue, std::to_wstring(s.speed) + L"%");
    for (auto item :
         {std::make_pair(app.outputControl, s.output), std::make_pair(app.midiControl, s.midi + 1),
          std::make_pair(app.keyMode, s.keyMode), std::make_pair(app.poly, int(s.polyphonic == 0))})
    {
        if (!send(item.first, CB_GETDROPPEDSTATE) && send(item.first, CB_GETCURSEL) != item.second)
        {
            send(item.first, CB_SETCURSEL, item.second);
        }
    }
    setChecked(app.measured, s.measured != 0);
    setChecked(app.dac, s.dac != 0);
    setChecked(app.aftertouch, s.aftertouch != 0);
    EnableWindow(app.measured, s.output == 0);
    EnableWindow(app.fmDelay, s.output == 2);
    EnableWindow(app.pcmDelay, s.output == 2);
    for (auto item : {std::make_pair(app.fmDelay, s.fmDelay), std::make_pair(app.pcmDelay, s.pcmDelay)})
    {
        if (GetFocus() != item.first)
        {
            bool previous = app.restoring;
            app.restoring = true;
            setText(item.first, std::to_wstring(item.second));
            app.restoring = previous;
        }
    }
    for (int i = 0; i < 16; ++i)
    {
        setChecked(app.trackChecks[i], (s.muteMask & (1u << i)) == 0);
    }
}
void commandControl(int id, int code)
{
    if (!app.ready || app.restoring)
    {
        return;
    }
    if (code == BN_CLICKED)
    {
        if (id == idMeasured)
        {
            app.core.command(15, checked(app.measured));
        }
        if (id == idDAC)
        {
            app.core.command(16, checked(app.dac));
        }
        if (id == idAftertouch)
        {
            app.core.command(24, checked(app.aftertouch));
        }
    }
    if (code == CBN_SELCHANGE)
    {
        if (id == idKeyMode)
        {
            app.core.command(20, int(send(app.keyMode, CB_GETCURSEL)));
        }
        if (id == idPoly)
        {
            app.core.command(23, send(app.poly, CB_GETCURSEL) == 0);
        }
    }
    if (code == EN_CHANGE && (id == idFMDelay || id == idPCMDelay))
    {
        try
        {
            auto str = text(
                [&]()
                {
                    if (id == idFMDelay)
                    {
                        return app.fmDelay;
                    }
                    return app.pcmDelay;
                }());
            std::size_t end;
            int n = std::stoi(str, &end);
            if (end == str.size() && n >= 0 && n <= 2000)
            {
                app.core.command(
                    [&]()
                    {
                        if (id == idFMDelay)
                        {
                            return 17;
                        }
                        return 18;
                    }(),
                    n);
            }
        }
        catch (...)
        {
        }
    }
}
void slider(HWND h, int code)
{
    int v = int(send(h, TBM_GETPOS));
    if (h == app.seek)
    {
        app.seeking = code == TB_THUMBTRACK || code == TB_THUMBPOSITION;
        if (code == TB_ENDTRACK)
        {
            app.seeking = false;
        }
        app.core.command(4, int(app.snapshot.total * uint64_t(v) / 1000 * 1000 / 48000));
    }
    else if (h == app.masterSlider)
    {
        app.master = v;
        app.core.command(9, v);
        setText(app.masterLabel, std::to_wstring(v) + L"%");
        saveSettings();
    }
    else if (h == app.pcmSlider)
    {
        app.pcm = v;
        app.core.command(10, v);
        setText(app.pcmLabel, std::to_wstring(v) + L"%");
        saveSettings();
    }
    else if (h == app.fmGain)
    {
        app.core.command(22, v);
        setText(app.fmValue, std::to_wstring(v) + L"%");
    }
    else if (h == app.key)
    {
        app.core.command(19, v);
        setText(app.keyValue, std::to_wstring(v));
    }
    else if (h == app.speed)
    {
        app.core.command(21, v);
        setText(app.speedValue, std::to_wstring(v) + L"%");
    }
}
} // namespace gui

