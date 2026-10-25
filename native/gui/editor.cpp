// SPDX-License-Identifier: 0BSD
// Copyright (C) 2026 by I.C.KaZe
#include "app.h"
namespace gui
{
static COLORREF colors[16] = {RGB(255, 95, 95),  RGB(255, 174, 72), RGB(242, 225, 70),  RGB(116, 226, 99),
                              RGB(72, 214, 198), RGB(94, 167, 255), RGB(178, 137, 255), RGB(245, 122, 204),
                              RGB(205, 72, 72),  RGB(196, 130, 40), RGB(174, 180, 45),  RGB(54, 163, 99),
                              RGB(47, 155, 177), RGB(86, 104, 210), RGB(137, 82, 205),  RGB(205, 76, 143)};
void fill(HDC dc, int x, int y, int w, int h, COLORREF color)
{
    RECT r{x, y, x + w, y + h};
    auto b = CreateSolidBrush(color);
    FillRect(dc, &r, b);
    DeleteObject(b);
}
void drawText(HDC dc, int x, int y, const std::wstring &value, COLORREF color)
{
    auto s = translate(value);
    SetTextColor(dc, color);
    SetBkMode(dc, TRANSPARENT);
    TextOutW(dc, x, y, s.data(), int(s.size()));
}
struct Surface
{
    HDC dc{};
    HBITMAP bitmap{};
    HGDIOBJ previous{};
    int width = 0, height = 0;
    void clear()
    {
        if (dc)
        {
            SelectObject(dc, previous);
        }
        if (bitmap)
        {
            DeleteObject(bitmap);
        }
        if (dc)
        {
            DeleteDC(dc);
        }
        dc = nullptr;
        bitmap = nullptr;
        width = height = 0;
    }
    ~Surface()
    {
        clear();
    }
    HDC acquire(HDC original, int w, int h)
    {
        if (width != w || height != h)
        {
            clear();
            dc = CreateCompatibleDC(original);
            bitmap = CreateCompatibleBitmap(original, w, h);
            if (!dc || !bitmap)
            {
                clear();
                return original;
            }
            previous = SelectObject(dc, bitmap);
            width = w;
            height = h;
        }
        return dc;
    }
};
static Surface editorSurface, gutterSurface;
HFONT gutterFont(HDC dc)
{
    CHARFORMAT2W f{};
    f.cbSize = sizeof(f);
    send(app.editor, EM_GETCHARFORMAT, SCF_DEFAULT, LPARAM(&f));
    static LONG height = 0;
    static std::wstring face;
    if (f.yHeight <= 0)
    {
        return HFONT(send(app.editor, WM_GETFONT));
    }
    if (!app.gutterFont || height != f.yHeight || face != f.szFaceName)
    {
        if (app.gutterFont)
        {
            DeleteObject(app.gutterFont);
        }
        int logical = -int((int64_t(f.yHeight) * GetDeviceCaps(dc, LOGPIXELSY) + 720) / 1440);
        app.gutterFont = CreateFontW(logical, 0, 0, 0, 400, FALSE, FALSE, FALSE, f.bCharSet, 0, 0,
                                     CLEARTYPE_QUALITY, f.bPitchAndFamily, f.szFaceName);
        height = f.yHeight;
        face = f.szFaceName;
    }
    return app.gutterFont;
}
void syncGutter()
{
    static POINT previous{INT32_MIN, INT32_MIN};
    static RECT old{};
    POINT scroll{};
    RECT rect{};
    send(app.editor, EM_GETSCROLLPOS, 0, LPARAM(&scroll));
    GetClientRect(app.editor, &rect);
    if (scroll.x != previous.x || scroll.y != previous.y || !EqualRect(&rect, &old))
    {
        previous = scroll;
        old = rect;
        InvalidateRect(app.lines, nullptr, FALSE);
    }
}
void paintBuffered(HWND hwnd, bool gutter)
{
    PAINTSTRUCT ps{};
    auto paint = BeginPaint(hwnd, &ps);
    RECT rect{};
    GetClientRect(hwnd, &rect);
    if (rect.right > 0 && rect.bottom > 0)
    {
        auto &surface = [&]() -> Surface &
        {
            if (gutter)
            {
                return gutterSurface;
            }
            return editorSurface;
        }();
        auto dc = surface.acquire(paint, rect.right, rect.bottom);
        int saved = SaveDC(dc);
        FillRect(dc, &rect, app.edit);
        if (!gutter)
        {
            DefSubclassProc(hwnd, WM_PRINTCLIENT, WPARAM(dc), PRF_CLIENT);
        }
        else
        {
            SelectObject(dc, gutterFont(dc));
            POINT origin{};
            MapWindowPoints(app.editor, hwnd, &origin, 1);
            RECT format{};
            send(app.editor, EM_GETRECT, 0, LPARAM(&format));
            int bottom = format.bottom + origin.y;
            IntersectClipRect(dc, 0, format.top + origin.y, rect.right, bottom);
            int first = int(send(app.editor, EM_GETFIRSTVISIBLELINE)),
                count = int(send(app.editor, EM_GETLINECOUNT));
            for (int line = first; line < count; ++line)
            {
                auto index = send(app.editor, EM_LINEINDEX, line);
                if (index < 0)
                {
                    break;
                }
                POINT point{};
                send(app.editor, EM_POSFROMCHAR, WPARAM(&point), index);
                int y = point.y + origin.y;
                if (y >= bottom)
                {
                    break;
                }
                std::wstring label = std::to_wstring(line + 1);
                if (app.checkedRevision == app.revision && line < int(app.gutterRows.size()))
                {
                    label = app.gutterRows[line];
                    if (app.gutterLengths)
                    {
                        label = std::to_wstring(line + 1) + L"  " +
                                wide(workbench::formatLineNoteLengths(app.lineSteps[line + 1]));
                    }
                }
                drawText(dc, 3, y, label, RGB(178, 178, 255));
            }
        }
        RestoreDC(dc, saved);
        if (dc != paint)
        {
            BitBlt(paint, 0, 0, rect.right, rect.bottom, dc, 0, 0, SRCCOPY);
        }
    }
    EndPaint(hwnd, &ps);
}
LRESULT CALLBACK gutterProc(HWND w, UINT m, WPARAM a, LPARAM b, UINT_PTR, DWORD_PTR)
{
    if (m == WM_NCHITTEST)
    {
        return HTCLIENT;
    }
    if (m == WM_RBUTTONDOWN || m == WM_CONTEXTMENU)
    {
        return 0;
    }
    if (m == WM_RBUTTONUP)
    {
        app.gutterLengths = !app.gutterLengths;
        saveSettings();
        InvalidateRect(w, nullptr, FALSE);
        return 0;
    }
    if (m == WM_MOUSEWHEEL)
    {
        return send(app.editor, m, a, b);
    }
    if (m == WM_ERASEBKGND)
    {
        return 1;
    }
    if (m == WM_PAINT)
    {
        paintBuffered(w, true);
        return 0;
    }
    if (m == WM_NCDESTROY)
    {
        gutterSurface.clear();
        if (app.gutterFont)
        {
            DeleteObject(app.gutterFont);
        }
        app.gutterFont = nullptr;
    }
    return DefSubclassProc(w, m, a, b);
}
LRESULT CALLBACK editorProc(HWND w, UINT m, WPARAM a, LPARAM b, UINT_PTR, DWORD_PTR)
{
    static DWORD savedSelection[2]{};
    static POINT savedScroll{};
    static uint64_t savedRevision = 0;
    static bool savedFocus = false;
    if (m == WM_KILLFOCUS)
    {
        send(w, EM_GETSEL, WPARAM(&savedSelection[0]), LPARAM(&savedSelection[1]));
        send(w, EM_GETSCROLLPOS, 0, LPARAM(&savedScroll));
        savedRevision = app.revision;
        savedFocus = true;
    }
    if (m == WM_NCDESTROY)
    {
        editorSurface.clear();
    }
    if (m == WM_ERASEBKGND)
    {
        return 1;
    }
    if (m == WM_PAINT)
    {
        InvalidateRect(w, nullptr, FALSE);
        paintBuffered(w, false);
        syncGutter();
        return 0;
    }
    if (m == WM_CHAR && (a == 26 || a == 25))
    {
        return 0;
    }
    if (m == WM_PASTE)
    {
        return send(w, EM_PASTESPECIAL, CF_UNICODETEXT);
    }
    if (m == WM_KEYDOWN && GetKeyState(VK_CONTROL) < 0)
    {
        if (a == 'Y' || (a == 'Z' && GetKeyState(VK_SHIFT) < 0))
        {
            send(w, EM_REDO);
            return 0;
        }
        if (a == 'Z')
        {
            send(w, WM_UNDO);
            return 0;
        }
    }
    auto r = DefSubclassProc(w, m, a, b);
    if (m == WM_SETFOCUS && savedFocus && savedRevision == app.revision)
    {
        DWORD current[2]{};
        send(w, EM_GETSEL, WPARAM(&current[0]), LPARAM(&current[1]));
        if (current[0] != savedSelection[0] || current[1] != savedSelection[1])
        {
            send(w, EM_SETSEL, savedSelection[0], savedSelection[1]);
        }
        send(w, EM_SETSCROLLPOS, 0, LPARAM(&savedScroll));
        savedFocus = false;
        syncGutter();
    }
    switch (m)
    {
    case WM_MOUSEWHEEL:
    case WM_VSCROLL:
    case WM_HSCROLL:
    case WM_KEYDOWN:
    case WM_SIZE:
    case WM_CHAR:
    case WM_PASTE:
    case EM_SETSEL:
    case EM_REPLACESEL:
    case EM_EXSETSEL:
    case EM_LINESCROLL:
    case EM_SCROLLCARET:
    case EM_SETSCROLLPOS:
        InvalidateRect(w, nullptr, FALSE);
        syncGutter();
        break;
    }
    if (m == WM_KEYDOWN || m == WM_CHAR || m == WM_LBUTTONUP || (m == WM_MOUSEMOVE && (a & MK_LBUTTON)))
    {
        refreshSelection();
    }
    return r;
}
void paintDecoration()
{
    bool active = app.cursorsValid && app.snapshot.playing;
    bool same = std::equal(app.paintedOffsets.begin(), app.paintedOffsets.end(), app.snapshot.offsets);
    if (!app.decorationDirty && active == app.paintedActive && (!active || same))
    {
        followPlayback();
        return;
    }
    app.decorationDirty = false;
    app.paintedActive = active;
    std::copy_n(app.snapshot.offsets, 16, app.paintedOffsets.begin());
    if (!app.ready)
    {
        return;
    }
    std::vector<int32_t> spans;
    if (active)
    {
        auto units = workbench::toUtf16(utf8(app.playingText));
        std::set<int> seen;
        for (int i = 0; i < 16; ++i)
        {
            auto it = app.cursors.find(app.snapshot.offsets[i]);
            if (it == app.cursors.end())
            {
                continue;
            }
            int at = it->second["char"].integer(-1);
            if (at < 0 || at >= int(units.size()) || !seen.insert(at).second)
            {
                continue;
            }
            spans.insert(spans.end(), {at, workbench::mmlTokenEnd(units, at), int32_t(RGB(178, 178, 255)),
                                       int32_t(colors[i])});
        }
    }
    for (const auto &e : app.errors)
    {
        spans.insert(spans.end(), {e.first, e.second, int32_t(RGB(20, 22, 48)), int32_t(RGB(178, 178, 255))});
    }
    app.core.require(app.core.WB_EditorDecoration(app.editor, spans.data(), spans.size() / 4));
    followPlayback();
    if (!app.cursorsValid)
    {
        app.playingText.clear();
    }
}
void followPlayback(bool immediate)
{
    if (!app.follow || !app.ready || !app.cursorsValid || !app.snapshot.playing ||
        (app.snapshot.paused && !immediate))
    {
        return;
    }
    int ch = workbench::editorFollowTrack(app.follow, app.snapshot.muteMask);
    if (ch < 0)
    {
        return;
    }
    auto found = app.cursors.find(app.snapshot.offsets[ch]);
    if (found == app.cursors.end() || found->second["track"].integer() != ch)
    {
        return;
    }
    int at = found->second["char"].integer(-1);
    if (at < 0)
    {
        return;
    }
    POINT p{-1, -1};
    RECT format{};
    send(app.editor, EM_POSFROMCHAR, WPARAM(&p), at);
    send(app.editor, EM_GETRECT, 0, LPARAM(&format));
    if (p.x == -1 && p.y == -1)
    {
        return;
    }
    auto dc = GetDC(app.editor);
    if (!dc)
    {
        return;
    }
    auto previous = SelectObject(dc, gutterFont(dc));
    TEXTMETRICW metrics{};
    GetTextMetricsW(dc, &metrics);
    SelectObject(dc, previous);
    ReleaseDC(app.editor, dc);
    POINT scroll{};
    send(app.editor, EM_GETSCROLLPOS, 0, LPARAM(&scroll));
    scroll.y = std::max<LONG>(0, scroll.y + p.y + metrics.tmHeight / 2 - (format.top + format.bottom) / 2);
    send(app.editor, EM_SETSCROLLPOS, 0, LPARAM(&scroll));
}
LRESULT CALLBACK sliderProc(HWND w, UINT m, WPARAM a, LPARAM b, UINT_PTR, DWORD_PTR)
{
    static HWND dragging = nullptr;
    auto update = [&]
    {
        RECT channel{};
        send(w, TBM_GETCHANNELRECT, 0, LPARAM(&channel));
        int value = workbench::sliderClickValue(GET_X_LPARAM(b), channel.left, channel.right - channel.left,
                                                int(send(w, TBM_GETRANGEMIN)), int(send(w, TBM_GETRANGEMAX)));
        send(w, TBM_SETPOS, TRUE, value);
        send(GetParent(w), WM_HSCROLL, TB_THUMBPOSITION, LPARAM(w));
    };
    if (m == WM_LBUTTONDOWN)
    {
        SetFocus(w);
        SetCapture(w);
        dragging = w;
        if (w == app.seek)
        {
            app.seeking = true;
        }
        update();
        return 0;
    }
    if (m == WM_MOUSEMOVE && dragging == w)
    {
        update();
        return 0;
    }
    if (m == WM_LBUTTONUP && dragging == w)
    {
        update();
        dragging = nullptr;
        ReleaseCapture();
        send(GetParent(w), WM_HSCROLL, TB_ENDTRACK, LPARAM(w));
        return 0;
    }
    if (m == WM_CAPTURECHANGED)
    {
        dragging = nullptr;
        if (w == app.seek)
        {
            app.seeking = false;
        }
    }
    return DefSubclassProc(w, m, a, b);
}
LRESULT CALLBACK buttonProc(HWND w, UINT m, WPARAM a, LPARAM b, UINT_PTR, DWORD_PTR)
{
    if (m == WM_ERASEBKGND)
    {
        return 1;
    }
    if (m == WM_ENABLE || m == WM_SETTEXT)
    {
        InvalidateRect(w, nullptr, FALSE);
    }
    if (m == WM_PAINT)
    {
        PAINTSTRUCT ps{};
        auto dc = BeginPaint(w, &ps);
        RECT r{};
        GetClientRect(w, &r);
        FillRect(dc, &r, app.edit);
        bool active = app.outputBusy && app.activeButton == w;
        if (active)
        {
            fill(dc, 0, 0, r.right * app.progress / 100, r.bottom, RGB(22, 105, 130));
        }
        FrameRect(dc, &r, app.accent);
        SetTextColor(dc,
                     [&]()
                     {
                         if (IsWindowEnabled(w) || active)
                         {
                             return RGB(178, 178, 255);
                         }
                         return RGB(80, 83, 110);
                     }());
        SetBkMode(dc, TRANSPARENT);
        auto previous = SelectObject(dc, HFONT(send(w, WM_GETFONT)));
        auto label = text(w);
        if (active)
        {
            auto at = label.find(L" WAV");
            if (at != std::wstring::npos)
            {
                label.erase(at);
            }
            label += L" " + std::to_wstring(app.progress) + L"%";
        }
        DrawTextW(dc, label.c_str(), -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SelectObject(dc, previous);
        EndPaint(w, &ps);
        return 0;
    }
    return DefSubclassProc(w, m, a, b);
}
LRESULT CALLBACK silentProc(HWND w, UINT m, WPARAM a, LPARAM b, UINT_PTR, DWORD_PTR)
{
    if (m == WM_SETFOCUS)
    {
        app.lastFocus = w;
    }
    if (m == WM_SYSCHAR)
    {
        return 0;
    }
    wchar_t cls[64]{};
    GetClassNameW(w, cls, 64);
    bool edit = !_wcsicmp(cls, L"EDIT"), rich = !_wcsnicmp(cls, L"RichEdit", 8),
         button = !_wcsicmp(cls, L"BUTTON"), combo = !_wcsicmp(cls, L"COMBOBOX");
    if (m == WM_CHAR && button && a != 32)
    {
        return 0;
    }
    if (m == WM_CHAR && combo && a >= 32)
    {
        wchar_t query[2]{wchar_t(a), 0};
        if (send(w, CB_FINDSTRING, WPARAM(-1), LPARAM(query)) == CB_ERR)
        {
            return 0;
        }
    }
    if ((edit || rich) && (m == WM_CHAR || m == WM_KEYDOWN))
    {
        DWORD begin = 0, end = 0;
        send(w, EM_GETSEL, WPARAM(&begin), LPARAM(&end));
        auto style = GetWindowLongW(w, GWL_STYLE);
        if (rich && m == WM_KEYDOWN && (a == VK_PRIOR || a == VK_NEXT))
        {
            SCROLLINFO si{};
            si.cbSize = sizeof(si);
            si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
            if (GetScrollInfo(w, SB_VERT, &si))
            {
                int last = std::max(si.nMin, si.nMax - int(si.nPage) + 1);
                if ((a == VK_PRIOR && si.nPos <= si.nMin) || (a == VK_NEXT && si.nPos >= last))
                {
                    int target = 0;
                    if (a == VK_NEXT)
                    {
                        target = GetWindowTextLengthW(w);
                    }
                    DWORD anchor = DWORD(target);
                    if (GetKeyState(VK_SHIFT) < 0)
                    {
                        anchor = begin;
                        POINT caret{};
                        if (GetCaretPos(&caret) && send(w, EM_CHARFROMPOS, 0, LPARAM(&caret)) <= begin)
                        {
                            anchor = end;
                        }
                    }
                    send(w, EM_SETSEL, anchor, target);
                    send(w, EM_SCROLLCARET);
                    return 0;
                }
            }
        }
        if (m == WM_CHAR)
        {
            if (a == 1)
            {
                send(w, EM_SETSEL, 0, -1);
                return 0;
            }
            if (a == 26 && !send(w, EM_CANUNDO))
            {
                return 0;
            }
            if (a < 32 && a != 8 && a != 9 && a != 13 && a != 3 && a != 22 && a != 24 && a != 26)
            {
                return 0;
            }
            if ((a == 8 && begin == 0 && end == 0) || ((style & ES_READONLY) && a != 3))
            {
                return 0;
            }
            if ((a == 13 || a == 9) && !(style & ES_MULTILINE))
            {
                return 0;
            }
            if (edit && (style & ES_NUMBER) && a >= 32 && (a < '0' || a > '9'))
            {
                return 0;
            }
            auto limit = send(w, EM_GETLIMITTEXT);
            if (a >= 32 && begin == end && limit > 0 && GetWindowTextLengthW(w) >= limit)
            {
                return 0;
            }
        }
        else if (begin == end)
        {
            if (((a == VK_PRIOR || a == VK_LEFT || a == VK_UP) && begin == 0) ||
                ((a == VK_NEXT || a == VK_RIGHT || a == VK_DOWN || a == VK_DELETE) &&
                 end == DWORD(GetWindowTextLengthW(w))))
            {
                return 0;
            }
        }
    }
    return DefSubclassProc(w, m, a, b);
}
} // namespace gui
