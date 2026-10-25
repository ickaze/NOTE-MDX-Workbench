// SPDX-License-Identifier: 0BSD
// Copyright (C) 2026 by I.C.KaZe
#include "app.h"
namespace gui
{
static void pumpUntil(const std::function<bool()> &done, unsigned milliseconds)
{
    uint64_t until = GetTickCount64() + milliseconds;
    while (!done())
    {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
        {
            if (message.message == WM_QUIT)
            {
                throw std::runtime_error("GUI quit during smoke");
            }
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        if (GetTickCount64() > until)
        {
            throw std::runtime_error("GUI smoke timeout");
        }
        MsgWaitForMultipleObjects(0, nullptr, FALSE, 10, QS_ALLINPUT);
    }
}
int runSmoke(bool frontend)
{
    app.smoke = true;
    int checks = 0;
    Json report = Json::object();
    std::vector<std::string> failures;
    auto check = [&](bool condition, const char *message)
    {
        ++checks;
        Json checkpoint = Json::object();
        checkpoint["checks"] = checks;
        checkpoint["last"] = message;
        checkpoint["ok"] = condition;
        write(executableDirectory() / L"GUI_SMOKE_PROGRESS.json", checkpoint.dump());
        if (!condition)
        {
            throw std::runtime_error(message);
        }
    };
    std::size_t wavBytes = 0;
    try
    {
        check(sizeof(NativeState) == 16936, "state ABI");
        // Record actual restored controls before the smoke changes document/settings.
        Json startup = Json::object();
        startup["master_volume"] = app.master;
        startup["pcm_volume"] = app.pcm;
        startup["font_name"] = utf8(app.fontName);
        startup["font_size"] = app.fontSize;
        startup["splitter_pos"] = app.splitterPos;
        startup["wav_loop_count"] = app.loops;
        startup["wav_fade_ms"] = app.fade;
        startup["monitor_height"] = app.monitorHeight;
        startup["piano_roll"] = app.roll;
        startup["carrier_pitch"] = app.carrier;
        startup["pitch_height"] = app.pitchHeight;
        startup["editor_follow_mode"] = app.follow;
        startup["waveforms"] = app.waves;
        startup["gutter_note_lengths"] = app.gutterLengths;
        startup["editor_text"] = utf8(text(app.editor));
        startup["future_nested"] = app.settings["future_nested"];
        startup["track_enabled"] = Json::array();
        for (auto control : app.trackChecks)
        {
            startup["track_enabled"].items.emplace_back(checked(control));
        }
        report["startup_settings"] = startup;
        int preferredSplitter = app.splitterPos;
        layout(1220, 780);
        check(app.splitterPos == preferredSplitter, "small layout preserves splitter preference");
        layout(1220, 1500);
        check(app.splitterActual == preferredSplitter, "large layout restores preferred splitter");
        RECT client{};
        GetClientRect(app.hwnd, &client);
        layout(client.right, client.bottom);
        saveSettings();
        check(Json::parse(read(settingsPath()))["splitter_pos"].integer() == preferredSplitter,
              "layout persists preferred splitter instead of clamped display position");


        CHARFORMAT2W cf{};
        cf.cbSize = sizeof(cf);
        send(app.editor, EM_GETCHARFORMAT, SCF_DEFAULT, LPARAM(&cf));
        check(cf.crTextColor == RGB(178, 178, 255), "RichEdit default color");
        setChecked(app.modeFile, false);
        setChecked(app.modeEdit, true);
        updateMode();
        RECT before{}, after{};
        GetWindowRect(app.lines, &before);
        for (int i = 0; i < 100; ++i)
        {
            syncGutter();
        }
        GetWindowRect(app.lines, &after);
        check(EqualRect(&before, &after), "gutter geometry stable");
        std::wstring voice = L"@0={31,0,0,8,0,48,0,1,0,0,0,31,0,0,8,0,48,0,1,0,0,0,31,0,0,8,0,48,0,1,0,0,0,"
                             L"31,0,0,8,0,48,0,1,0,0,0,7,0,15}";
        setText(app.editor, L";日本語テスト\n" + voice + L"\nA @0 c4 L [d8/e16]3\nB @0 r8 c8\n");
        auto original = text(app.editor);
        send(app.editor, EM_EMPTYUNDOBUFFER);
        SetFocus(app.editor);
        send(app.editor, EM_SETSEL, 7, 3);
        auto revision = app.revision;
        int32_t spans[] = {0, 2, int32_t(RGB(80, 30, 45))};
        check(app.core.WB_EditorBackground(app.editor, spans, std::size_t(1)) != 0 &&
                  app.revision == revision && text(app.editor) == original,
              "formatting preserves document and revision");
        DWORD begin = 0, end = 0;
        send(app.editor, EM_GETSEL, WPARAM(&begin), LPARAM(&end));
        check(begin == 3 && end == 7, "formatting retains selection");
        check(send(app.editor, EM_CANUNDO) == 0, "formatting does not change undo");
        app.core.WB_EditorBackground(app.editor, static_cast<int32_t *>(nullptr), std::size_t(0));
        check(compileFull(true), "full compilation");
        check(app.full["tracks"][std::size_t(0)]["total"].u64() == 144, "repeat total");
        check(app.full["tracks"][std::size_t(0)]["loop"].u64() == 96, "loop total");
        check(app.lineSteps[3][0] == 144, "row steps");
        check(text(app.lines).find(L"3  144") != std::wstring::npos, "gutter steps");
        for (const auto &event : app.full["events"].items)
        {
            check(event["char"].integer(-1) >= 0 && event["char"].u64() < original.size(),
                  "event UTF-16 character");
        }
        auto at = original.find(L"d8");
        send(app.editor, EM_SETSEL, at, at + 7);
        refreshSelection(true);
        check(app.selection["ok"].boolean(), "selection compilation");
        check(app.selection["tracks"][std::size_t(0)]["total"].u64() == 36, "selection total without repeat");
        check(app.selection["tracks"][std::size_t(1)]["total"].u64() == 0, "unselected track silent");
        check(app.selection["tracks"][std::size_t(0)]["loop"].kind == Json::Null,
              "selection loop marker removed");
        check(text(app.selectionLabel).find(L"36") != std::wstring::npos, "selection statistics");
        auto compiled = app.compiledRevision;
        tick();
        check(app.compiledRevision == compiled, "timer no synchronous recompile");
        auto wave = app.directory / L"smoke.wav";
        app.core.WB_ExportStart();
        check(app.core.WB_SaveWav(app.fullPath.c_str(), L"", app.sourcePath.c_str(), wave.c_str(), 0, 1, 0,
                                  100, 100) != 0,
              "WAV rendering");
        auto wav = read(wave);
        wavBytes = wav.size();
        bool nonzero = false;
        for (std::size_t i = 44; i < wav.size(); ++i)
        {
            nonzero = nonzero || wav[i] != 0;
        }
        check(wav.size() > 44 && wav.substr(0, 4) == "RIFF" && nonzero, "WAV has nonzero PCM");
        auto previous = app.snapshot;
        std::vector<std::pair<int, int>> settings{{15, 0}, {16, 1},   {17, 37},  {18, 43}, {19, 7},
                                                  {20, 0}, {21, 125}, {22, 245}, {23, 0},  {24, 1}};
        for (const auto &item : settings)
        {
            app.core.command(item.first, item.second);
        }
        app.core.WB_State(&app.snapshot, sizeof(app.snapshot));
        const auto state = app.snapshot;
        check(state.measured == 0 && state.dac == 1 && state.fmDelay == 37 && state.pcmDelay == 43 &&
                  state.key == 7 && state.keyMode == 0 && state.speed == 125 && state.fmPercent == 245 &&
                  !state.polyphonic && state.aftertouch,
              "native playback controls");
        for (const auto &item : std::vector<std::pair<int, int>>{{15, previous.measured},
                                                                 {16, previous.dac},
                                                                 {17, previous.fmDelay},
                                                                 {18, previous.pcmDelay},
                                                                 {19, previous.key},
                                                                 {20, previous.keyMode},
                                                                 {21, previous.speed},
                                                                 {22, previous.fmPercent},
                                                                 {23, previous.polyphonic},
                                                                 {24, previous.aftertouch}})
        {
            app.core.command(item.first, item.second);
        }
        auto service = FindWindowExW(HWND_MESSAGE, nullptr, L"NOTE-MDX-Workbench.Core.Dispatch", nullptr);
        check(service && !IsWindowVisible(service), "service window invisible");
        check(!GetWindow(service, GW_CHILD), "service has no child GUI");
        setChecked(app.rollButton, true);
        send(app.mmdspPanel, WM_COMMAND, idMonitorRoll, LPARAM(app.rollButton));
        setChecked(app.carrierButton, true);
        send(app.mmdspPanel, WM_COMMAND, idMonitorCarrier, LPARAM(app.carrierButton));
        check(app.roll && app.carrier && app.centered, "monitor controls");
        bool gutter = app.gutterLengths;
        send(app.lines, WM_RBUTTONDOWN);
        send(app.lines, WM_RBUTTONUP);
        check(app.gutterLengths != gutter, "gutter right click");
        send(app.lines, WM_RBUTTONUP);
        check(app.gutterLengths == gutter, "gutter toggle restore");
        app.settings["future_setting"] = Json("retain");
        int master = app.master, pcm = app.pcm;
        app.master = 137;
        app.pcm = 83;
        saveSettings();
        auto persisted = Json::parse(read(settingsPath()));
        check(persisted["language"].text()==([&]() { if (app.english) { return "en"; } return "ja"; }()),"settings language");
        check(persisted["master_volume"].integer() == 137 && persisted["pcm_volume"].integer() == 83,
              "settings volume");
        check(persisted["future_setting"].text() == "retain", "unknown settings preserved");
        app.master = master;
        app.pcm = pcm;
        saveSettings();
        // Exercise the real delayed UI receiver rather than only its core API.
        send(app.editor, EM_SETSEL, 0, 0);
        setText(app.editor, L"A l0");
        auto changed = app.revision;
        check(app.checkedRevision != changed, "edit invalidates stale statistics");
        pumpUntil([&] { return app.checkedRevision == changed; }, 5000);
        check(!app.checkedDocument["ok"].boolean() && !app.errors.empty(), "background error diagnostics");
        setText(app.editor, L"A c12 d12 e12\nB r12 c12\n");
        changed = app.revision;
        pumpUntil([&] { return app.checkedRevision == changed; }, 5000);
        check(app.checkedDocument["ok"].boolean(), "background valid edit");
        check(app.lineSteps[1][0] == 48, "triplet row length");
        check(workbench::noteRemainder(48).find("64.") == std::string::npos, "triplet display");
        // Real document save/open paths, with file chooser answers supplied by the harness.
        auto sourceFile = app.directory / L"日本語-source.mus";
        auto saveAs = app.directory / L"日本語-copy.mus";
        const std::string cp932 = ";\x93\xfa\x96\x7b\r\nA c4\r\n";
        write(sourceFile, cp932);
        app.fileChoices.push_back(sourceFile);
        browseSource();
        check(text(app.editor) == decode(cp932), "CP932 source open");
        check(Path(text(app.srcEdit)) == sourceFile, "reference source path");
        setText(app.editor, L";日本\nA d4\n");
        app.fileChoices.push_back(saveAs);
        check(saveSource(true), "Save As accepted");
        check(read(saveAs) == std::string(";\x93\xfa\x96\x7b\r\nA d4\r\n"),
              "Save As preserves CP932 and CRLF");
        setText(app.editor, L"A e4\n");
        app.dialogAnswers.push_back(IDCANCEL);
        check(!saveSource(false), "referenced save Cancel retains document");
        check(read(saveAs) == std::string(";\x93\xfa\x96\x7b\r\nA d4\r\n"), "save Cancel preserves file");
        setText(app.editor, L"#title PDX Smoke\nA c4\n");
        setText(app.editorName, L"GUI_SMOKE_PDX");
        launchPdx();
        check(app.core.WB_PdxIsOpen() != 0, "embedded PDX editor opened");
        check(text(app.editor).find(L"#pcmfile") != std::wstring::npos, "PDX launch inserts directive");
        check(gui::exists(app.directory / L"GUI_SMOKE_PDX.pdxedit"), "PDX project beside MUS");
        check(app.core.WB_ClosePdx() != 0 && !app.core.WB_PdxIsOpen(), "embedded PDX editor closes");
        check(encodeSource(L";日本\nA c4\n", "\xef\xbb\xbf;Japanese\r\n").rfind("\xef\xbb\xbf", 0) == 0,
              "UTF-8 BOM retained");
        bool refused = false;
        try
        {
            encodeSource(L"😀", cp932);
        }
        catch (const std::exception &)
        {
            refused = true;
        }
        check(refused, "CP932 rejects unrepresentable text");
        check(app.fileChoices.empty(), "all file chooser answers consumed");
        setText(app.srcEdit, L"");
        // Frozen-output orchestration, publication, overwrite cancellation and cleanup.
        setText(app.editor, L"#title \"GUI Export\"\n" + voice + L"\nA @0 c16\n");
        setText(app.editorName, L"GUI_SMOKE_EXPORT");
        send(app.editor, EM_SETSEL, 0, 0);
        for (int format : {2, 3, 4, 5, 6, 7})
        {
            app.dialogs.clear();
            app.dialogAnswers.push_back(IDYES);
            startExport(format);
            check(app.outputBusy, "output worker started");
            pumpUntil([&] { return !app.outputBusy; }, 30000);
            if (!app.outputError.empty())
            {
                throw std::runtime_error(utf8(app.outputError));
            }
            check(app.outputError.empty(), "output worker success");
            check(!app.dialogs.empty() && app.dialogs.back().first == L"出力完了",
                  "output completion dialog");
            auto output = Path(app.dialogs.back().second);
            check(output.is_absolute() && gui::exists(output),
                  "completion contains existing absolute output");
            auto bytes = read(output);
            check(!bytes.empty(), "output file nonempty");
            if (format == 2)
            {
                check(bytes.find("Gd3 ") != std::string::npos, "VGM GD3 metadata");
                auto title = std::wstring(L"GUI Export");
                std::string titleBytes(reinterpret_cast<const char *>(title.data()), title.size() * 2);
                auto comment = std::wstring(L"Exported from NOTE MDX Workbench.");
                std::string commentBytes(reinterpret_cast<const char *>(comment.data()), comment.size() * 2);
                check(bytes.find(titleBytes) != std::string::npos, "GD3 track title");
                check(bytes.find(commentBytes) != std::string::npos, "GD3 English comment");
            }
            if (format == 3)
            {
                check(bytes.size() > 2 && uint8_t(bytes[0]) == 31 && uint8_t(bytes[1]) == 139,
                      "VGZ gzip signature");
            }
            if (format == 5)
            {
                check(bytes.substr(0, 2) == "PK", "MRZ ZIP signature");
            }
            app.dialogAnswers.clear();
            app.dialogAnswers.push_back(IDNO);
            startExport(format);
            check(!app.outputBusy && read(output) == bytes, "overwrite declined preserves output");
            app.dialogAnswers.clear();
            gui::remove(output);
        }
        // Cancel actual worker jobs for every long-running format. Their current-directory
        // reservations and partially rendered data must disappear before completion.
        setText(app.editor, voice + L"\nA @0 [c1]200\nB @0 [d1]200\nC @0 [e1]200\nD @0 [f1]200\n");
        setText(app.editorName, L"GUI_SMOKE_CANCEL");
        app.loops = 1;
        app.fade = 0;
        auto temporarySet = []
        {
            std::set<std::wstring> paths;
            for (const auto &entry : fs::directory_iterator(fs::current_path()))
            {
                if (entry.path().filename().wstring().rfind(L"workbench-", 0) == 0)
                {
                    paths.insert(entry.path().wstring());
                }
            }
            return paths;
        };
        for (int format : {2, 3, 4, 5, 6, 7})
        {
            auto beforeFiles = temporarySet();
            app.dialogs.clear();
            app.dialogAnswers.clear();
            startExport(format);
            check(app.outputBusy, "cancellation worker started");
            app.dialogAnswers.push_back(IDNO);
            cancelExport();
            check(app.core.WB_ExportCancelled() == 0, "active job No resumes");
            app.dialogAnswers.push_back(IDYES);
            cancelExport();
            pumpUntil([&] { return !app.outputBusy; }, 30000);
            check(app.core.WB_ExportCancelled() != 0, "active job Yes cancels");
            bool completed = false;
            for (const auto &item : app.dialogs)
            {
                if (item.first == L"出力完了")
                {
                    completed = true;
                }
            }
            check(!completed, "cancelled job does not publish output");
            check(temporarySet() == beforeFiles, "cancelled job removes temporary files");
        }
        app.dialogAnswers.clear();
        app.core.WB_ExportStart();
        app.outputBusy = true;
        app.dialogAnswers.push_back(IDNO);
        cancelExport();
        check(app.core.WB_ExportCheckpoint() != 0, "cancel No resumes");
        app.dialogAnswers.push_back(IDYES);
        cancelExport();
        check(app.core.WB_ExportCancelled() != 0, "cancel Yes cancels");
        app.outputBusy = false;
        app.core.WB_ExportStart();
        setText(app.editor, original);
        send(app.editor, EM_SETSEL, 0, 0);
        check(compileFull(), "restore valid document");
        autosave();
        check(gui::exists(autosavePath()), "editor autosave");
        check(app.dialogAnswers.empty(), "all confirmation answers consumed");
    }
    catch (const std::exception &e)
    {
        failures.push_back(e.what());
    }
    report["version"] = utf8(version);
    report["implementation"] = "C++";
    report["abi_bytes"] = 16936;
    report["ok"] = failures.empty();
    report["checks"] = checks;
    report["language"] = [&]()
    {
        if (app.english)
        {
            return "en";
        }
        return "ja";
    }();
    report["wav_bytes"] = uint64_t(wavBytes);
    report["failures"] = Json::array();
    for (const auto &f : failures)
    {
        report["failures"].items.emplace_back(f);
    }
    write(executableDirectory()/([&]() { if (frontend) { return L"GUI_SMOKE_CPP_RESULT.json"; } return L"GUI_SMOKE_RESULT.json"; }()),report.dump()+"\n");
    if (failures.empty())
    {
        return 0;
    }
    return 1;
}
} // namespace gui

