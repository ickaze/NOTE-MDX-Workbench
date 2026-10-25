// SPDX-License-Identifier: 0BSD
// Copyright (C) 2026 by I.C.KaZe
#include "app.h"
namespace gui
{
constexpr int header = 58;
static COLORREF palette[16] = {RGB(255, 95, 95),  RGB(255, 174, 72), RGB(242, 225, 70),  RGB(116, 226, 99),
                               RGB(72, 214, 198), RGB(94, 167, 255), RGB(178, 137, 255), RGB(245, 122, 204),
                               RGB(205, 72, 72),  RGB(196, 130, 40), RGB(174, 180, 45),  RGB(54, 163, 99),
                               RGB(47, 155, 177), RGB(86, 104, 210), RGB(137, 82, 205),  RGB(205, 76, 143)};
static std::array<std::array<int, 4>, 16> monitorNotes()
{
    std::array<std::array<int, 4>, 16> result;
    static const int codes[] = {0, 1, 2, -1, 3, 4, 5, -1, 6, 7, 8, -1, 9, 10, 11, -1};
    static const int masks[] = {8, 8, 8, 8, 12, 14, 14, 15};
    static const double detune[] = {0, 6, 7.81, 9.50};
    auto &state = app.snapshot;
    for (int ch = 0; ch < 16; ++ch)
    {
        result[ch].fill(-1);
        int base = state.notes[ch];
        if (ch < 8 && base >= 0)
        {
            auto kc = state.registers[0x28 + ch];
            base = -1;
            if (codes[kc & 15] >= 0)
            {
                base = ((kc >> 4) & 7) * 12 + codes[kc & 15];
            }
        }
        if (state.muteMask & (1u << ch))
        {
            base = -1;
        }
        if (base < 0)
        {
            continue;
        }
        if (!app.carrier || ch >= 8)
        {
            result[ch][0] = base;
            continue;
        }
        int at = 0;
        for (int slot = 0; slot < 4; ++slot)
        {
            if (!(masks[state.registers[0x20 + ch] & 7] & (1 << slot)))
            {
                continue;
            }
            double ml = state.registers[0x40 + slot * 8 + ch] & 15;
            if (!ml)
            {
                ml = .5;
            }
            double shift = 12 * std::log2(ml) + detune[state.registers[0xc0 + slot * 8 + ch] >> 6];
            int note = int(std::round(base + double(state.registers[0x30 + ch] >> 2) / 64 + shift));
            if (note >= 0 && note < 96)
            {
                result[ch][at] = note;
            }
            ++at;
        }
    }
    return result;
}
void captureHistory()
{
    uint64_t now = GetTickCount64();
    auto &s = app.snapshot;
    bool active = (s.playing && !s.paused && s.speed > 0) || s.reserved;
    if (app.wall && active)
    {
        double speed = 1;
        if (s.playing && !s.paused)
        {
            speed = double(s.speed) / 100;
        }
        app.clock += (now - app.wall) / 1000.0 * speed;
    }
    app.wall = now;
    auto notes = monitorNotes();
    if (app.history.empty() || app.history.back().notes != notes)
    {
        app.history.push_back({app.clock, notes});
    }
    while (app.history.size() > 1 && app.history[1].time < app.clock - 8)
    {
        app.history.pop_front();
    }
}
void monitorLayout()
{
    if (!app.scroll)
    {
        return;
    }
    RECT r{};
    GetClientRect(app.mmdspPanel, &r);
    int page = std::max(1, int(r.bottom) - header - 2);
    MoveWindow(app.scroll, r.right - 19, header, 18, page, TRUE);
    ShowWindow(app.scroll,
               [&]()
               {
                   if (app.roll)
                   {
                       return SW_SHOW;
                   }
                   return SW_HIDE;
               }());
    int maximum = std::max(0, 96 * app.pitchHeight - page);
    if (!app.centered)
    {
        app.pitchScroll = maximum / 2;
        app.centered = true;
    }
    app.pitchScroll = std::clamp(app.pitchScroll, 0, maximum);
    SCROLLINFO info{};
    info.cbSize = sizeof(info);
    info.fMask = SIF_ALL;
    info.nMax = 96 * app.pitchHeight - 1;
    info.nPage = UINT(page);
    info.nPos = app.pitchScroll;
    SetScrollInfo(app.scroll, SB_CTL, &info, TRUE);
    if (app.ready)
    {
        app.core.command(25, app.roll || app.waves);
    }
}
static void keyboard(HDC dc, int x, int y, int width, int h, int ch, bool masked,
                     const std::array<int, 4> &notes)
{
    for (int pass = 0; pass < 2; ++pass)
    {
        for (int note = 0; note < 96; ++note)
        {
            auto key = workbench::pianoKeyBounds(note, width);
            if (key.black != (pass == 1))
            {
                continue;
            }
            COLORREF c = RGB(205, 208, 215);
            int height = h;
            if (key.black)
            {
                c = RGB(12, 14, 22);
                height = h * 3 / 5;
            }
            if (!masked && std::find(notes.begin(), notes.end(), note) != notes.end())
            {
                c = palette[ch];
            }
            fill(dc, x + key.left, y, key.right - key.left, height, c);
            if (!key.black)
            {
                fill(dc, x + key.right - 1, y, 1, height, RGB(7, 8, 24));
            }
        }
    }
}
static void paintRoll(HDC dc, int w, int h)
{
    int top = header, bottom = h - 2, left = 62, right = w - 22, center = (left + right) / 2;
    if (bottom <= top || right <= left)
    {
        return;
    }
    double scale = double(right - left) / 8, now = double(app.snapshot.position) / 48000;
    for (int ch = 0; ch < 16; ++ch)
    {
        drawText(dc, 9 + ch * 29, 32, std::wstring(1, names[ch]),
                 [&]()
                 {
                     if (app.snapshot.muteMask & (1u << ch))
                     {
                         return RGB(50, 52, 64);
                     }
                     return palette[ch];
                 }());
    }
    drawText(dc, 510, 32, L"中央: 現在 / 左: 履歴 / 右: 予定", RGB(140, 165, 255));
    for (int note = 0; note < 96; ++note)
    {
        int y = top + (95 - note) * app.pitchHeight - app.pitchScroll;
        if (y + app.pitchHeight <= top || y >= bottom)
        {
            continue;
        }
        int y1 = std::max(top, y), y2 = std::min(bottom, y + app.pitchHeight);
        int semi = (note + 3) % 12;
        bool black = semi == 1 || semi == 3 || semi == 6 || semi == 8 || semi == 10;
        fill(dc, left, y1, right - left, y2 - y1,
             [&]()
             {
                 if (black)
                 {
                     return RGB(12, 15, 26);
                 }
                 return RGB(24, 28, 45);
             }());
        fill(dc, left, y1, right - left, 1, RGB(34, 38, 59));
        if (semi == 0)
        {
            drawText(dc, 6, y1, L"C" + std::to_wstring((note + 3) / 12), RGB(145, 150, 175));
        }
    }
    for (int sec = -4; sec <= 4; ++sec)
    {
        fill(dc, center + int(sec * scale), top, 1, bottom - top, RGB(34, 38, 59));
    }
    auto pen = CreatePen(PS_SOLID, 1, RGB(55, 74, 98));
    auto old = SelectObject(dc, pen);
    for (int x = left; x < right; ++x)
    {
        int sample = (x - left) * 256 / (right - left);
        double mixed = 0;
        for (int ch = 0; ch < 16; ++ch)
        {
            mixed += app.snapshot.waveform[ch][sample];
        }
        int y = (top + bottom) / 2 - int(std::clamp(mixed, -1.0, 1.0) * double(bottom - top) / 2);
        if (x == left)
        {
            MoveToEx(dc, x, y, nullptr);
        }
        else
        {
            LineTo(dc, x, y);
        }
    }
    SelectObject(dc, old);
    DeleteObject(pen);
    struct Bar
    {
        int start, end, channel, note;
    };
    std::vector<Bar> bars;
    auto add = [&](double start, double end, int ch, int note, bool future)
    {
        if (ch < 0 || ch >= 16 || note < 0 || note >= 96 || (app.snapshot.muteMask & (1u << ch)))
        {
            return;
        }
        int x1 = center + int(start * scale), x2 = center + int(end * scale);
        if (future)
        {
            x1 = std::max(x1, center);
        }
        else
        {
            x2 = std::min(x2, center);
        }
        x1 = std::max(x1, left);
        x2 = std::min(x2, right);
        int y = top + (95 - note) * app.pitchHeight - app.pitchScroll;
        if (x2 > x1 && std::min(bottom, y + app.pitchHeight - 1) > std::max(top, y + 1))
        {
            bars.push_back({x1, x2, ch, note});
        }
    };
    if (app.snapshot.playing)
    {
        for (const auto &event : app.score["notes"].items)
        {
            int ch = event["track"].integer(-1);
            if (ch < 0 || ch >= 16)
            {
                continue;
            }
            double a = event["start"].real(), b = event["end"].real(),
                   loopStart = app.score["trackLoopStart"][std::size_t(ch)].real(),
                   loopEnd = app.score["trackLoopEnd"][std::size_t(ch)].real(), period = loopEnd - loopStart;
            int first = 0, last = 0;
            bool repeats = period > 0 && a >= loopStart && a < loopEnd;
            if (repeats)
            {
                first = std::max(0, int(std::floor((now - b) / period)) + 1);
                last = int(std::floor((now + 4 - a) / period));
            }
            for (int cycle = first; cycle <= last; ++cycle)
            {
                double shift = 0;
                if (repeats)
                {
                    shift = cycle * period;
                }
                if (b + shift < now || a + shift > now + 4)
                {
                    continue;
                }
                for (std::size_t slot=0;slot<([&]() { if (app.carrier) { return 4u; } return 1u; }());++slot)
                {
                    int note = event["note"].integer(-1);
                    if (app.carrier)
                    {
                        note = event["carrier"][slot].integer(-1);
                    }
                    if (note >= 0)
                    {
                        add(a+shift-now,b+shift-now,ch,note+([&]() { if (ch<8) { return app.snapshot.key-app.score["key"].integer(); } return 0; }()),true);
                    }
                }
            }
        }
    }
    for (std::size_t i = 0; i < app.history.size(); ++i)
    {
        double end = app.clock;
        if (i + 1 < app.history.size())
        {
            end = app.history[i + 1].time;
        }
        for (int ch = 0; ch < 16; ++ch)
        {
            for (int note : app.history[i].notes[ch])
            {
                add(app.history[i].time - app.clock, end - app.clock, ch, note, false);
            }
        }
    }
    for (const auto &bar : bars)
    {
        int middle = (bar.start + bar.end) / 2;
        std::array<bool, 16> present{};
        for (const auto &other : bars)
        {
            if (other.note == bar.note && other.start <= middle && other.end > middle)
            {
                present[other.channel] = true;
            }
        }
        present[bar.channel] = true;
        int count = 0, index = 0;
        for (int ch = 0; ch < 16; ++ch)
        {
            if (present[ch])
            {
                ++count;
                if (ch < bar.channel)
                {
                    ++index;
                }
            }
        }
        int y = top + (95 - bar.note) * app.pitchHeight - app.pitchScroll + 1, span = app.pitchHeight - 2;
        int y1 = std::max(top, y + index * span / count),
            y2 = std::min(bottom, y + (index + 1) * span / count);
        if (y2 > y1)
        {
            fill(dc, bar.start, y1, bar.end - bar.start, y2 - y1, palette[bar.channel]);
        }
    }
    fill(dc, center, top, 2, bottom - top, RGB(240, 245, 255));
}
void paintMonitor(HDC dc, int w, int h)
{
    fill(dc, 0, 0, w, h, RGB(7, 8, 24));
    SelectObject(dc, app.font);
    drawText(dc, 6, 2, L"MDX REALTIME MONITOR", RGB(140, 165, 255));
    if (app.roll)
    {
        paintRoll(dc, w, h);
        return;
    }
    auto notes = monitorNotes();
    auto &state = app.snapshot;
    int colW = w / 2;
    static int masks[] = {8, 8, 8, 8, 10, 14, 14, 15};
    for (int ch = 0; ch < 16; ++ch)
    {
        int base = 6 + (ch / 8) * colW, y = header + (ch % 8) * std::max(20, (h - header - 4) / 8),
            keyX = base + 18;
        bool masked = state.muteMask & (1u << ch), keyOn = state.notes[ch] >= 0;
        drawText(dc, base, y + 3, std::wstring(1, names[ch]),
                 [&]()
                 {
                     if (masked)
                     {
                         return RGB(55, 65, 105);
                     }
                     return RGB(140, 165, 255);
                 }());
        keyboard(dc, keyX, y, 288, 18, ch, masked, notes[ch]);
        double peak = 0;
        for (float v : state.waveform[ch])
        {
            peak = std::max(peak, std::abs(double(v)));
        }
        double lvL = peak, lvR = peak;
        int pan = 0;
        if (ch < 8)
        {
            int reg = (state.registers[0x20 + ch] >> 6) & 3;
            pan = 0;
            if (reg == 2)
            {
                pan = -1;
            }
            else if (reg == 1)
            {
                pan = 1;
            }
            double tl = 0;
            int carriers = 0, mask = masks[state.registers[0x20 + ch] & 7];
            for (int slot = 0; slot < 4; ++slot)
            {
                if (mask & (1 << slot))
                {
                    tl = std::max(tl, double(127 - (state.registers[0x60 + slot * 8 + ch] & 127)) / 127);
                    ++carriers;
                }
            }
            if (!keyOn || !carriers)
            {
                tl = 0;
            }
            lvL = 0;
            lvR = 0;
            if (reg & 2)
            {
                lvL = tl;
            }
            if (reg & 1)
            {
                lvR = tl;
            }
        }
        else if (lvL + lvR > .0001)
        {
            double ratio = (lvR - lvL) / (lvL + lvR);
            pan = 0;
            if (ratio < -.25)
            {
                pan = -1;
            }
            else if (ratio > .25)
            {
                pan = 1;
            }
        }
        if (masked)
        {
            lvL = lvR = 0;
        }
        int panX = keyX + 308, meterX = panX + 18;
        if (!masked)
        {
            auto pen = CreatePen(PS_SOLID, 1,
                                 [&]()
                                 {
                                     if (keyOn)
                                     {
                                         return RGB(160, 240, 55);
                                     }
                                     return RGB(70, 88, 38);
                                 }());
            auto old = SelectObject(dc, pen);
            auto oldBrush = SelectObject(dc, GetStockObject(HOLLOW_BRUSH));
            double angle = -3.141592653589793 / 2;
            if (pan < 0)
            {
                angle = 3 * 3.141592653589793 / 4;
            }
            else if (pan > 0)
            {
                angle = 3.141592653589793 / 4;
            }
            Ellipse(dc, panX - 8, y - 3, panX + 9, y + 14);
            MoveToEx(dc, panX, y + 5, nullptr);
            LineTo(dc, panX + int(std::cos(angle) * 6), y + 5 + int(std::sin(angle) * 6));
            SelectObject(dc, oldBrush);
            SelectObject(dc, old);
            DeleteObject(pen);
        }
        fill(dc, meterX, y + 1, 48, 3, RGB(18, 24, 48));
        fill(dc, meterX, y + 6, 48, 3, RGB(18, 24, 48));
        fill(dc, meterX, y + 1, 48 * std::clamp(int(lvL * 128 + .5), 0, 128) / 128, 3, RGB(105, 230, 70));
        fill(dc, meterX, y + 6, 48 * std::clamp(int(lvR * 128 + .5), 0, 128) / 128, 3, RGB(105, 230, 70));
        wchar_t number[16]{};
        swprintf(number, 16, L"%03d", std::clamp(int(std::max(lvL, lvR) * 128 + .5), 0, 128));
        drawText(dc, meterX, y + 10, number, RGB(100, 125, 205));
        int waveX = meterX + 100, waveW = colW - (waveX - base) - 8;
        if (waveW > 24 && app.waves)
        {
            fill(dc, waveX, y, waveW, 18, RGB(20, 22, 48));
            if (!masked)
            {
                auto pen = CreatePen(PS_SOLID, 1, RGB(75, 205, 255));
                auto old = SelectObject(dc, pen);
                for (int px = 0; px < waveW; ++px)
                {
                    int i = (int(state.cursor) + px * 256 / waveW) % 256;
                    int yy = y + 9 - int(state.waveform[ch][i] / std::max(peak, .01) * 8);
                    if (!px)
                    {
                        MoveToEx(dc, waveX, yy, nullptr);
                    }
                    else
                    {
                        LineTo(dc, waveX + px, yy);
                    }
                }
                SelectObject(dc, old);
                DeleteObject(pen);
            }
        }
    }
}
static void toggle(int ch)
{
    if (ch < 0 || ch >= 16)
    {
        return;
    }
    setChecked(app.trackChecks[ch], !checked(app.trackChecks[ch]));
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
    InvalidateRect(app.mmdspPanel, nullptr, FALSE);
}
LRESULT CALLBACK monitorProc(HWND w, UINT m, WPARAM a, LPARAM b)
{
    try
    {
        if (m == WM_COMMAND)
        {
            int id = LOWORD(a);
            if (id == idMonitorRoll)
            {
                app.roll = checked(app.rollButton);
                monitorLayout();
                saveSettings();
            }
            if (id == idMonitorCarrier)
            {
                app.carrier = checked(app.carrierButton);
                app.history.clear();
                saveSettings();
            }
            if (id == idWaves)
            {
                app.waves = checked(app.waveButton);
                monitorLayout();
                saveSettings();
            }
            if (id == idEditorFollow && HIWORD(a) == CBN_SELCHANGE)
            {
                app.follow =
                    workbench::restoredEditorFollow(int(send(app.followControl, CB_GETCURSEL)), false);
                followPlayback(true);
                saveSettings();
            }
            InvalidateRect(w, nullptr, FALSE);
            return 0;
        }
        if (m == WM_SIZE)
        {
            monitorLayout();
            return 0;
        }
        if (m == WM_VSCROLL && HWND(b) == app.scroll)
        {
            RECT r{};
            GetClientRect(w, &r);
            int page = r.bottom - header - 2;
            switch (LOWORD(a))
            {
            case SB_LINEUP:
                app.pitchScroll -= app.pitchHeight;
                break;
            case SB_LINEDOWN:
                app.pitchScroll += app.pitchHeight;
                break;
            case SB_PAGEUP:
                app.pitchScroll -= page;
                break;
            case SB_PAGEDOWN:
                app.pitchScroll += page;
                break;
            case SB_THUMBPOSITION:
            case SB_THUMBTRACK:
            {
                SCROLLINFO si{};
                si.cbSize = sizeof(si);
                si.fMask = SIF_TRACKPOS;
                GetScrollInfo(app.scroll, SB_CTL, &si);
                app.pitchScroll = si.nTrackPos;
                break;
            }
            case SB_TOP:
                app.pitchScroll = 0;
                break;
            case SB_BOTTOM:
                app.pitchScroll = 96 * app.pitchHeight;
                break;
            }
            monitorLayout();
            InvalidateRect(w, nullptr, FALSE);
            return 0;
        }
        if (m == WM_MOUSEWHEEL && app.roll)
        {
            int wheel = GET_WHEEL_DELTA_WPARAM(a);
            if (GET_KEYSTATE_WPARAM(a) & MK_CONTROL)
            {
                RECT r{};
                GetClientRect(w, &r);
                int page = r.bottom - header - 2, old = app.pitchHeight;
                app.pitchHeight = std::clamp(old + wheel / 60, 3, 36);
                app.pitchScroll = (app.pitchScroll + page / 2) * app.pitchHeight / old - page / 2;
                saveSettings();
            }
            else
            {
                app.pitchScroll -= wheel * 3 * app.pitchHeight / 120;
            }
            monitorLayout();
            InvalidateRect(w, nullptr, FALSE);
            return 0;
        }
        if (m == WM_LBUTTONDOWN && app.ready)
        {
            int x = GET_X_LPARAM(b), y = GET_Y_LPARAM(b);
            if (app.roll)
            {
                if (y >= 30 && y < 54 && x >= 6)
                {
                    toggle((x - 6) / 29);
                }
            }
            else
            {
                RECT r{};
                GetClientRect(w, &r);
                if (y >= header)
                {
                    int ch = (y - header) / std::max(20, (int(r.bottom) - header - 4) / 8);
                    int local = x % (int(r.right) / 2);
                    if (ch < 8 && local >= 24 && local < 312)
                    {
                        toggle(ch+([&]() { if (x>=r.right/2) { return 8; } return 0; }()));
                    }
                }
            }
            return 0;
        }
        if (m == WM_ERASEBKGND)
        {
            return 1;
        }
        if (m == WM_PAINT)
        {
            PAINTSTRUCT ps{};
            auto dc = BeginPaint(w, &ps);
            RECT r{};
            GetClientRect(w, &r);
            if (r.right > 0 && r.bottom > 0)
            {
                auto memory = CreateCompatibleDC(dc);
                auto bitmap = CreateCompatibleBitmap(dc, r.right, r.bottom);
                auto old = SelectObject(memory, bitmap);
                paintMonitor(memory, r.right, r.bottom);
                BitBlt(dc, 0, 0, r.right, r.bottom, memory, 0, 0, SRCCOPY);
                SelectObject(memory, old);
                DeleteObject(bitmap);
                DeleteDC(memory);
            }
            EndPaint(w, &ps);
            return 0;
        }
        if (m == WM_CTLCOLORSTATIC || m == WM_CTLCOLORBTN)
        {
            SetTextColor(HDC(a), RGB(178, 178, 255));
            SetBkColor(HDC(a), RGB(7, 8, 24));
            return LRESULT(app.panel);
        }
    }
    catch (const std::exception &e)
    {
        logError(e);
    }
    return DefWindowProcW(w, m, a, b);
}
} // namespace gui
