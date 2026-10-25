// SPDX-License-Identifier: 0BSD
// Copyright (C) 2026 by I.C.KaZe
#include "app.h"
namespace gui
{
Path settingsPath()
{
    return executableDirectory() / L"NOTE-MDX-Workbench.json";
}
Path autosavePath()
{
    Path extension = Path(text(app.srcEdit)).extension();
    if (extension.empty())
    {
        extension = L".mus";
    }
    return executableDirectory() / (safeName(text(app.editorName)) + L"_Autosave" + extension.wstring());
}
void saveSettings()
{
    if (app.restoring || !app.settingsLoaded || !app.hwnd)
    {
        return;
    }
    Json st = app.settings; // Unknown future settings survive a round trip.
    st["language"] = [&]()
    {
        if (app.english)
        {
            return "en";
        }
        return "ja";
    }();
    st["mode"] = [&]()
    {
        if (checked(app.modeEdit))
        {
            return "editor";
        }
        return "file";
    }();
    st["source_path"] = utf8(text(app.srcEdit));
    st["pdx_path"] = utf8(text(app.pdxEdit));
    st["editor_name"] = utf8(text(app.editorName));
    st["watch"] = checked(app.watchCheck);
    st["master_volume"] = app.master;
    st["pcm_volume"] = app.pcm;
    st["track_enabled"] = Json::array();
    for (auto h : app.trackChecks)
    {
        st["track_enabled"].items.emplace_back(checked(h));
    }
    st["editor_top_line"] = int(send(app.editor, EM_GETFIRSTVISIBLELINE));
    st["font_name"] = utf8(app.fontName);
    st["font_size"] = app.fontSize;
    st["splitter_pos"] = app.splitterPos;
    st["wav_loop_count"] = app.loops;
    st["wav_fade_ms"] = app.fade;
    st["monitor_height"] = app.monitorHeight;
    st["piano_roll"] = app.roll;
    st["carrier_pitch"] = app.carrier;
    st["pitch_scroll"] = app.pitchScroll;
    st["pitch_height"] = app.pitchHeight;
    st["editor_follow_mode"] = app.follow;
    st["waveforms"] = app.waves;
    st["gutter_note_lengths"] = app.gutterLengths;
    try
    {
        atomicWrite(settingsPath(), st.dump() + "\n");
        app.settings = std::move(st);
    }
    catch (const std::exception &e)
    {
        logError(e);
    }
}
void autosave()
{
    if (app.restoring || !app.editor)
    {
        return;
    }
    try
    {
        atomicWrite(autosavePath(), utf8(text(app.editor)));
        app.autosaveAt = 0;
        saveSettings();
    }
    catch (const std::exception &e)
    {
        logError(e);
    }
}
void applyFont()
{
    HFONT previous = app.font;
    app.font = CreateFontW(-((app.fontSize * 96 + 36) / 72), 0, 0, 0, 400, FALSE, FALSE, FALSE,
                           DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, app.fontName.c_str());
    EnumChildWindows(
        app.hwnd,
        [](HWND w, LPARAM p) -> BOOL
        {
            send(w, WM_SETFONT, WPARAM(p), TRUE);
            return TRUE;
        },
        LPARAM(app.font));
    CHARFORMAT2W f{};
    f.cbSize = sizeof(f);
    f.dwMask = CFM_COLOR | CFM_BACKCOLOR;
    f.crTextColor = RGB(178, 178, 255);
    f.crBackColor = RGB(20, 22, 48);
    send(app.editor, EM_SETCHARFORMAT, SCF_ALL, LPARAM(&f));
    send(app.editor, EM_SETCHARFORMAT, SCF_DEFAULT, LPARAM(&f));
    if (previous)
    {
        DeleteObject(previous);
    }
    app.decorationDirty = true;
}
void loadSettings()
{
    app.restoring = true;
    setChecked(app.modeEdit, true);
    setChecked(app.modeFile, false);
    setChecked(app.watchCheck, false);
    setText(app.editorName, L"editor");
    for (auto w : app.trackChecks)
    {
        setChecked(w, true);
    }
    int top = 0;
    try
    {
        app.settings = Json::parse(read(settingsPath()));
        if (app.settings.kind != Json::Object)
        {
            throw std::runtime_error("Settings must be a JSON object");
        }
        const auto &st = app.settings;
        auto bounded = [&](const char *name, int low, int high, int fallback)
        {
            int n = st[name].integer();
            if (n >= low && n <= high)
            {
                return n;
            }
            return fallback;
        };
        app.master = bounded("master_volume", 0, 400, 100);
        app.pcm = bounded("pcm_volume", 0, 400, 100);
        app.fontName = wide(st["font_name"].text("MS Gothic"));
        if (app.fontName.empty())
        {
            app.fontName = L"MS Gothic";
        }
        app.fontSize = bounded("font_size", 6, 24, 9);
        app.splitterPos = bounded("splitter_pos", 200, 1200, 470);
        if (st["monitor_height"].integer() >= 220)
        {
            app.monitorHeight = st["monitor_height"].integer();
        }
        app.loops = bounded("wav_loop_count", 0, 100, 2);
        app.fade = bounded("wav_fade_ms", 0, 600000, 2000);
        app.roll = st["piano_roll"].boolean();
        app.carrier = st["carrier_pitch"].boolean();
        app.waves = st["waveforms"].boolean(true);
        app.gutterLengths = st["gutter_note_lengths"].boolean();
        app.pitchHeight = bounded("pitch_height", 3, 36, 18);
        app.pitchScroll = st["pitch_scroll"].integer();
        app.centered = app.roll;
        app.follow = workbench::restoredEditorFollow(
            [&]() -> std::optional<int>
            {
                if (st["editor_follow_mode"].kind == Json::Number)
                {
                    return st["editor_follow_mode"].integer();
                }
                return std::nullopt;
            }(),
            st["editor_follow"].boolean());
        setText(app.srcEdit, wide(st["source_path"].text()));
        setText(app.pdxEdit, wide(st["pdx_path"].text()));
        auto name = wide(st["editor_name"].text());
        setText(app.editorName,
                [&]() -> std::wstring
                {
                    if (name.empty())
                    {
                        return L"editor";
                    }
                    return name;
                }());
        setChecked(app.watchCheck, st["watch"].boolean());
        setChecked(app.modeEdit, st["mode"].text() == "editor");
        setChecked(app.modeFile, !checked(app.modeEdit));
        for (int i = 0; i < 16; ++i)
        {
            setChecked(app.trackChecks[i], st["track_enabled"][std::size_t(i)].boolean());
        }
        top = st["editor_top_line"].integer();
    }
    catch (const std::exception &e)
    {
        if (gui::exists(settingsPath()))
        {
            logError(e);
        }
    }
    if (checked(app.modeFile) && !text(app.srcEdit).empty())
    {
        setText(app.editorName, safeName(Path(text(app.srcEdit)).filename().wstring()));
    }
    send(app.masterSlider, TBM_SETPOS, TRUE, app.master);
    send(app.pcmSlider, TBM_SETPOS, TRUE, app.pcm);
    setText(app.masterLabel, std::to_wstring(app.master) + L"%");
    setText(app.pcmLabel, std::to_wstring(app.pcm) + L"%");
    setText(app.wavLoopsEdit, std::to_wstring(app.loops));
    setText(app.fadeMsEdit, std::to_wstring(app.fade));
    setChecked(app.rollButton, app.roll);
    setChecked(app.carrierButton, app.carrier);
    setChecked(app.waveButton, app.waves);
    send(app.followControl, CB_SETCURSEL, app.follow);
    for (const auto &path :
         {autosavePath(), executableDirectory() / (safeName(text(app.editorName)) + L".mus"),
          executableDirectory() / L"NOTE-MDX-Editor-Autosave.txt"})
    {
        if (gui::exists(path))
        {
            try
            {
                setText(app.editor, decode(read(path)));
            }
            catch (const std::exception &e)
            {
                logError(e);
            }
            break;
        }
    }
    applyFont();
    if (top > 0)
    {
        send(app.editor, EM_LINESCROLL, 0, top);
    }
    app.restoring = false;
    app.settingsLoaded = true;
    updateMode();
    saveSettings();
}
} // namespace gui
