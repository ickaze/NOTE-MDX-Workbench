// SPDX-License-Identifier: 0BSD
// Copyright (C) 2026 by I.C.KaZe
#include "app.h"
#include "source_watch.h"
#include <objbase.h>
namespace gui
{
App app;
void browsePdx();
static void flushLog()
{
    std::vector<std::wstring> lines;
    {
        std::lock_guard<std::mutex> lock(app.logMutex);
        lines.swap(app.pendingLog);
    }
    if (lines.empty())
    {
        return;
    }
    SCROLLINFO si{};
    si.cbSize = sizeof(si);
    si.fMask = SIF_ALL;
    bool bottom = !GetScrollInfo(app.log, SB_VERT, &si) || si.nPos + int(si.nPage) >= si.nMax;
    int first = int(send(app.log, EM_GETFIRSTVISIBLELINE));
    auto old = text(app.log);
    for (const auto &line : lines)
    {
        if (!old.empty())
        {
            old += L"\r\n";
        }
        old += translate(line);
    }
    if (old.size() > 50000)
    {
        old.erase(0, old.size() - 40000);
        first = 0;
    }
    setText(app.log, old);
    if (bottom)
    {
        int n = GetWindowTextLengthW(app.log);
        send(app.log, EM_SETSEL, n, n);
        send(app.log, EM_SCROLLCARET);
    }
    else
    {
        send(app.log, EM_LINESCROLL, 0, first - send(app.log, EM_GETFIRSTVISIBLELINE));
    }
}
static void chooseFormat()
{
    if (app.outputBusy)
    {
        cancelExport();
        return;
    }
    HMENU menu = CreatePopupMenu();
    const wchar_t *labels[] = {L"MDX", L"VGM", L"VGZ", L"MRL", L"MRZ"};
    for (int i = 0; i < 5; ++i)
    {
        AppendMenuW(menu, MF_STRING, UINT_PTR(i + 1), labels[i]);
    }
    RECT r{};
    GetWindowRect(app.convertBtn, &r);
    int selected = TrackPopupMenu(menu, TPM_RETURNCMD, r.left, r.bottom, 0, app.hwnd, nullptr);
    DestroyMenu(menu);
    if (selected == 1)
    {
        convertMdx();
    }
    else if (selected >= 2 && selected <= 5)
    {
        startExport(selected);
    }
}
static void handleCommand(int id, int code, HWND origin)
{
    if (id >= idMeasured && id <= idAftertouch)
    {
        commandControl(id, code);
        return;
    }
    if (id == idLanguage && code == CBN_SELCHANGE)
    {
        changeLanguage(send(app.languageControl, CB_GETCURSEL) == 1);
        return;
    }
    if (id == idLiveOutput && code == CBN_SELCHANGE)
    {
        app.core.command(5, int(send(app.outputControl, CB_GETCURSEL)));
        updateProgress();
    }
    if (id == idLiveMIDI && code == CBN_DROPDOWN)
    {
        refreshMidi();
    }
    if (id == idLiveMIDI && code == CBN_SELCHANGE)
    {
        app.core.command(12, int(send(app.midiControl, CB_GETCURSEL)));
    }
    if (id == ID_EDITOR && code == EN_CHANGE)
    {
        if (!app.restoring)
        {
            invalidate();
            app.autosaveAt = GetTickCount64();
        }
        syncGutter();
    }
    if ((id == ID_SRC_EDIT || id == ID_PDX_EDIT || id == ID_EDITOR_NAME) && code == EN_CHANGE)
    {
        if (!app.restoring)
        {
            invalidate();
        }
        saveSettings();
    }
    if (code == EN_CHANGE && (id == ID_WAV_LOOPS_EDIT || id == ID_FADE_MS_EDIT))
    {
        try
        {
            auto s = text(
                [&]()
                {
                    if (id == ID_WAV_LOOPS_EDIT)
                    {
                        return app.wavLoopsEdit;
                    }
                    return app.fadeMsEdit;
                }());
            std::size_t at;
            int n = std::stoi(s, &at);
            if (at==s.size() && n>=(int(id==ID_WAV_LOOPS_EDIT)) && n<=([&]() { if (id==ID_WAV_LOOPS_EDIT) { return 100; } return 600000; }()))
            {
                if (id == ID_WAV_LOOPS_EDIT)
                {
                    app.loops = n;
                }
                else
                {
                    app.fade = n;
                }
                saveSettings();
            }
        }
        catch (...)
        {
        }
    }
    if (code != BN_CLICKED)
    {
        return;
    }
    switch (id)
    {
    case ID_MODE_FILE:
    case ID_MODE_EDIT:
        updateMode();
        invalidate();
        saveSettings();
        break;
    case ID_SRC_BROWSE:
        browseSource();
        break;
    case ID_PDX_BROWSE:
        browsePdx();
        break;
    case ID_CONVERT:
        chooseFormat();
        break;
    case ID_MML_SAVE:
        saveSource(true);
        break;
    case idPDXEdit:
        if (origin == app.pdxButton)
        {
            launchPdx();
        }
        break;
    case idLiveVoices:
        app.core.command(7);
        break;
    case ID_PLAY:
        play();
        break;
    case ID_PAUSE:
        app.core.command(2);
        break;
    case ID_STOP:
        stop();
        break;
    case ID_EXPORT_MIX:
        if (!app.outputBusy || app.activeButton == app.exportMixBtn)
        {
            startExport(6);
        }
        break;
    case ID_EXPORT_TRACKS:
        if (!app.outputBusy || app.activeButton == app.exportTracksBtn)
        {
            startExport(7);
        }
        break;
    case ID_WATCH:
        saveSettings();
        break;
    default:
        if (id >= ID_TRACK_BASE && id < ID_TRACK_BASE + 16)
        {
            int mask = 0;
            for (int i = 0; i < 16; ++i)
            {
                if (!checked(app.trackChecks[i]))
                {
                    mask |= 1 << i;
                }
            }
            app.core.command(8, mask);
            saveSettings();
        }
    }
}
LRESULT CALLBACK windowProc(HWND w, UINT m, WPARAM a, LPARAM b)
{
    try
    {
        switch (m)
        {
        case WM_CREATE:
            app.hwnd = w;
            app.bg = CreateSolidBrush(RGB(4, 5, 15));
            app.panel = CreateSolidBrush(RGB(7, 8, 24));
            app.edit = CreateSolidBrush(RGB(20, 22, 48));
            app.accent = CreateSolidBrush(RGB(92, 210, 255));
            createControls();
            {
                RECT r{};
                GetClientRect(w, &r);
                layout(r.right, r.bottom);
            }
            return 0;
        case WM_ACTIVATE:
            if (LOWORD(a) == WA_INACTIVE)
            {
                auto focus = GetFocus();
                if (IsChild(w, focus))
                {
                    app.lastFocus = focus;
                }
            }
            else if (LOWORD(a) == WA_ACTIVE)
            {
                PostMessageW(w, WM_APP + 74, 0, 0);
            }
            break;
        case WM_APP + 74:
            if (GetForegroundWindow() == w && IsChild(w, app.lastFocus) && IsWindowEnabled(app.lastFocus))
            {
                SetFocus(app.lastFocus);
            }
            return 0;
        case WM_SYSCHAR:
            return 0;
        case WM_GETMINMAXINFO:
        {
            auto limits = reinterpret_cast<MINMAXINFO *>(b);
            limits->ptMinTrackSize = {1050, 860};
            return 0;
        }
        case WM_COMMAND:
            handleCommand(LOWORD(a), HIWORD(a), HWND(b));
            return 0;
        case WM_HSCROLL:
            if (app.ready)
            {
                slider(HWND(b), LOWORD(a));
            }
            return 0;
        case analysisMessage:
            receiveAnalysis(0);
            return 0;
        case selectionMessage:
            receiveAnalysis(1);
            return 0;
        case outputMessage:
            finishExport();
            return 0;
        case WM_APP + 73:
            if (a == VK_F5)
            {
                play();
            }
            else if (a == VK_F6 && app.snapshot.playing)
            {
                app.core.command(2);
            }
            else if (a == VK_ESCAPE)
            {
                stop();
            }
            return 0;
        case WM_TIMER:
            if (a == 2)
            {
                if (app.ready)
                {
                    auto previous = app.snapshot;
                    app.core.require(app.core.WB_State(&app.snapshot, sizeof(app.snapshot)));
                    paintDecoration();
                    captureHistory();
                    if ((app.snapshot.playing && !app.snapshot.paused && app.snapshot.speed) ||
                        app.snapshot.reserved || app.snapshot.position != previous.position ||
                        app.snapshot.cursor != previous.cursor ||
                        std::memcmp(app.snapshot.notes, previous.notes, sizeof(previous.notes)) != 0 ||
                        std::memcmp(app.snapshot.registers, previous.registers, sizeof(previous.registers)) !=
                            0 ||
                        app.snapshot.muteMask != previous.muteMask ||
                        app.snapshot.playing != previous.playing)
                    {
                        InvalidateRect(app.mmdspPanel, nullptr, FALSE);
                    }
                }
                return 0;
            }
            if (a == 1)
            {
                flushLog();
                if (app.ready)
                {
                    tick();
                }
                static int savedTop = -1;
                int top = int(send(app.editor, EM_GETFIRSTVISIBLELINE));
                if (top != savedTop)
                {
                    savedTop = top;
                    saveSettings();
                }
                if (!app.restoring && app.autosaveAt && GetTickCount64() - app.autosaveAt >= 1000)
                {
                    autosave();
                }
                static workbench::SourceWatchState watch{};
                static unsigned watchTick = 0;
                ++watchTick;
                if ((watchTick % 5 == 0) && app.ready && checked(app.modeFile) && checked(app.watchCheck) &&
                    !app.outputBusy)
                {
                    auto src = trim(text(app.srcEdit)), pdx = trim(text(app.pdxEdit));
                    if (!src.empty() && workbench::pollSourceWatch(watch, src.c_str(), pdx.c_str()))
                    {
                        logLine(L"更新を検出しました。自動変換して再生します。");
                        convertMdx(true);
                    }
                }
                return 0;
            }
            break;
        case WM_SIZE:
            layout(LOWORD(b), HIWORD(b));
            return 0;
        case WM_LBUTTONDOWN:
        {
            int x = GET_X_LPARAM(b), y = GET_Y_LPARAM(b);
            if (x >= 10 && y >= app.monitorTop - 8 && y < app.monitorTop)
            {
                app.monitorDrag = true;
                SetCapture(w);
                return 0;
            }
            if (y >= 10 + app.splitterActual && y <= 24 + app.splitterActual)
            {
                app.splitDrag = true;
                SetCapture(w);
                return 0;
            }
            break;
        }
        case WM_MOUSEMOVE:
            if (app.monitorDrag || app.splitDrag)
            {
                RECT r{};
                GetClientRect(w, &r);
                if (app.monitorDrag)
                {
                    app.monitorHeight = std::clamp(int(r.bottom) - GET_Y_LPARAM(b) - 10, 220,
                                                   std::max(220, int(r.bottom) - 574));
                }
                else
                {
                    app.splitterPos = std::clamp(GET_Y_LPARAM(b) - 14, 200, 1200);
                }
                layout(r.right, r.bottom);
                return 0;
            }
            break;
        case WM_LBUTTONUP:
            if (app.monitorDrag || app.splitDrag)
            {
                app.monitorDrag = app.splitDrag = false;
                ReleaseCapture();
                saveSettings();
                return 0;
            }
            break;
        case WM_CAPTURECHANGED:
            app.monitorDrag = app.splitDrag = false;
            break;
        case WM_SETCURSOR:
        {
            POINT p{};
            GetCursorPos(&p);
            ScreenToClient(w, &p);
            if ((p.y >= app.monitorTop - 8 && p.y < app.monitorTop) ||
                (p.x >= 520 && p.y >= app.splitterActual + 10 && p.y < app.splitterActual + 24))
            {
                SetCursor(LoadCursorW(nullptr, IDC_SIZENS));
                return TRUE;
            }
            break;
        }
        case WM_ERASEBKGND:
        {
            RECT r{};
            GetClientRect(w, &r);
            FillRect(HDC(a), &r, app.bg);
            return 1;
        }
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORBTN:
            SetTextColor(HDC(a), RGB(178, 178, 255));
            SetBkColor(HDC(a),
                       [&]()
                       {
                           if (m == WM_CTLCOLOREDIT)
                           {
                               return RGB(20, 22, 48);
                           }
                           return RGB(7, 8, 24);
                       }());
            SetBkMode(HDC(a), OPAQUE);
            return LRESULT(
                [&]()
                {
                    if (m == WM_CTLCOLOREDIT)
                    {
                        return app.edit;
                    }
                    return app.panel;
                }());
        case WM_CLOSE:
            if (!saveSource(false))
            {
                return 0;
            }
            if (app.ready && !app.core.WB_ClosePdx())
            {
                return 0;
            }
            app.closing = true;
            autosave();
            saveSettings();
            KillTimer(w, 1);
            KillTimer(w, 2);
            shutdownCore();
            DestroyWindow(w);
            return 0;
        case WM_DESTROY:
            if (app.font)
            {
                DeleteObject(app.font);
            }
            app.font = nullptr;
            for (auto brush : {app.bg, app.panel, app.edit, app.accent})
            {
                if (brush)
                {
                    DeleteObject(brush);
                }
            }
            PostQuitMessage(0);
            return 0;
        }
    }
    catch (const std::exception &e)
    {
        logError(e);
        if (m == WM_CREATE)
        {
            return -1;
        }
    }
    return DefWindowProcW(w, m, a, b);
}
} // namespace gui
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR command, int show)
{
    using namespace gui;
    int result = 0;
    HRESULT com = OleInitialize(nullptr);
    try
    {
        INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_WIN95_CLASSES | ICC_BAR_CLASSES};
        InitCommonControlsEx(&controls);
        auto saved = Json::object();
        try
        {
            saved = Json::parse(read(settingsPath()));
        }
        catch (...)
        {
        }
        auto language = saved["language"].text();
        app.english = language == "en" || (language != "ja" && (GetUserDefaultUILanguage() & 0x3ff) != 0x11);
        WNDCLASSEXW monitor{};
        monitor.cbSize = sizeof(monitor);
        monitor.hInstance = instance;
        monitor.lpfnWndProc = monitorProc;
        monitor.lpszClassName = L"MMDSPPanel";
        RegisterClassExW(&monitor);
        WNDCLASSEXW main{};
        main.cbSize = sizeof(main);
        main.hInstance = instance;
        main.lpfnWndProc = windowProc;
        main.lpszClassName = L"NoteMdxGuiClass";
        main.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        main.hIcon = HICON(LoadImageW(instance, MAKEINTRESOURCEW(101), IMAGE_ICON,
                                      GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), LR_SHARED));
        main.hIconSm =
            HICON(LoadImageW(instance, MAKEINTRESOURCEW(101), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON),
                             GetSystemMetrics(SM_CYSMICON), LR_SHARED));
        if (!RegisterClassExW(&main))
        {
            throw std::runtime_error("Cannot register application window");
        }
        std::wstring title = L"NOTE MDX Workbench v" + std::wstring(version);
        HWND hwnd =
            CreateWindowExW(0, main.lpszClassName, title.c_str(), WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, 100,
                            100, 1220, 870, nullptr, nullptr, instance, nullptr);
        if (!hwnd)
        {
            throw std::runtime_error("Cannot create application window");
        }
        initCore();
        ShowWindow(hwnd, show);
        UpdateWindow(hwnd);
        std::wstring option;
        if (command)
        {
            option = command;
        }
        if (option == L"--smoke" || option == L"--frontend-smoke")
        {
            result = runSmoke(option == L"--frontend-smoke");
            app.closing = true;
            shutdownCore();
            DestroyWindow(hwnd);
        }
        else
        {
            MSG msg{};
            int code;
            while ((code = GetMessageW(&msg, nullptr, 0, 0)) > 0)
            {
                if (msg.message == WM_KEYDOWN && msg.wParam == 'S' && GetKeyState(VK_CONTROL) < 0)
                {
                    if (!(msg.lParam & (1LL << 30)))
                    {
                        try
                        {
                            saveSource(true);
                        }
                        catch (const std::exception &e)
                        {
                            logError(e);
                        }
                    }
                    continue;
                }
                if (msg.message == WM_KEYDOWN &&
                    (msg.wParam == VK_F5 || msg.wParam == VK_F6 || msg.wParam == VK_ESCAPE))
                {
                    if (!(msg.lParam & (1LL << 30)))
                    {
                        try
                        {
                            if (msg.wParam == VK_F5)
                            {
                                play();
                            }
                            else if (msg.wParam == VK_F6 && app.snapshot.playing)
                            {
                                app.core.command(2);
                            }
                            else if (msg.wParam == VK_ESCAPE)
                            {
                                stop();
                            }
                        }
                        catch (const std::exception &e)
                        {
                            logError(e);
                        }
                    }
                    continue;
                }
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
            if (code < 0)
            {
                result = 1;
            }
        }
    }
    catch (const std::exception &e)
    {
        if (command && std::wstring(command).find(L"smoke") != std::wstring::npos)
        {
            try
            {
                write(executableDirectory() / L"GUI_SMOKE_ERROR.txt", e.what());
            }
            catch (...)
            {
            }
        }
        else
        {
            MessageBoxW(app.hwnd, wide(e.what()).c_str(), L"NOTE MDX Workbench", MB_OK | MB_ICONERROR);
        }
        shutdownCore();
        result = 1;
    }
    if (SUCCEEDED(com))
    {
        OleUninitialize();
    }
    return result;
}

