#pragma once
// Viewer audition uses the same queue and Mono/Poly allocator as external MIDI.
HWND voiceKeyboard = nullptr;
int auditionOctave = 4;
struct AuditionKey {
    int note = -1, channel = -1;
    uint32_t flags = 0;
};
std::array<AuditionKey, 13> auditionKeys;
AuditionKey mouseAudition;
constexpr wchar_t auditionMap[] = L"AWSEDFTGYHUJK";
void releaseAudition(AuditionKey &key) {
    if (key.note >= 0)
        midiQueue->push(key.flags | 0x80u | uint32_t(key.channel) | (uint32_t(key.note) << 8));
    key = {};
}
void releaseVoiceKeys() {
    for (auto &key : auditionKeys)
        releaseAudition(key);
    releaseAudition(mouseAudition);
    if (GetCapture() == voiceKeyboard)
        ReleaseCapture();
    if (voiceKeyboard)
        InvalidateRect(voiceKeyboard, nullptr, FALSE);
}
void beginAudition(AuditionKey &key, int note) {
    if(!previewEditedVoice(true)) return;
    if (note < 15 || note > 110)
        return;
    int channel = int(SendMessageW(voiceChannel, CB_GETCURSEL, 0, 0));
    if (channel < 0 || channel > 7)
        return;
    releaseAudition(key);
    if (!running)
        startMidiIdle();
    int id=selectedVoice;
    if(voiceDirty) {id=editedVoiceId;}
    if(id>=0) {midiQueue->push(0xf0u | uint32_t(channel) | (uint32_t(id)<<8));}
    key = {note, channel, 0x80000000u | (liveMidiPolyphonic->load() ? 0x40000000u : 0)};
    midiQueue->push(key.flags | 0x90u | uint32_t(channel) | (uint32_t(note) << 8) | (100u << 16));
    InvalidateRect(voiceKeyboard, nullptr, FALSE);
}
bool voiceKeyMessage(const MSG &msg) {
    // Release keys owned by audition even if focus moved to a text control.
    if(msg.message==WM_KEYUP) {
        for(int i=0;i<13;++i) if(msg.wParam==WPARAM(auditionMap[i]) && auditionKeys[i].note>=0) {
            releaseAudition(auditionKeys[i]);
            if(voiceKeyboard) InvalidateRect(voiceKeyboard,nullptr,FALSE);
            return true;
        }
    }
    if (!voiceWindow || !IsWindowVisible(voiceWindow) ||
        (msg.hwnd != voiceWindow && !IsChild(voiceWindow, msg.hwnd)))
        return false;
    if (msg.message != WM_KEYDOWN && msg.message != WM_KEYUP)
        return false;
    if(msg.hwnd==voiceEdit || msg.hwnd==registerEdit) return false;
    bool down = msg.message == WM_KEYDOWN;
    if (msg.wParam == VK_PRIOR || msg.wParam == VK_NEXT) {
        if (down && !(msg.lParam & (LPARAM(1) << 30))) {
            releaseVoiceKeys();
            auditionOctave = std::clamp(auditionOctave + (msg.wParam == VK_PRIOR ? 1 : -1), 0, 8);
            InvalidateRect(voiceKeyboard, nullptr, FALSE);
        }
        return true;
    }
    for (int i = 0; i < 13; ++i) {
        if (msg.wParam != WPARAM(auditionMap[i]))
            continue;
        // Preserve Ctrl+A / Ctrl+C and other edit shortcuts.
        if (down && (GetKeyState(VK_CONTROL) < 0 || GetKeyState(VK_MENU) < 0))
            return false;
        if (down) {
            if (!(msg.lParam & (LPARAM(1) << 30)))
                beginAudition(auditionKeys[i], (auditionOctave + 1) * 12 + i);
        } else
            releaseAudition(auditionKeys[i]);
        InvalidateRect(voiceKeyboard, nullptr, FALSE);
        return true;
    }
    return false;
}
std::array<RECT, 96> auditionRects(RECT client) {
    std::array<RECT, 96> boxes;
    int width = std::max(56L, client.right - scale(8)), left = scale(4), top = scale(24);
    int white = 0;
    for (int n = 0; n < 96; ++n) {
        if (blackKey(n + 3)) {
            int center = left + white * width / 56, half = std::max(1, width / 180);
            boxes[n] = {std::max(left, center - half), top, center + half,
                        top + (client.bottom - top) * 3 / 5};
        } else {
            boxes[n] = {left + white * width / 56, top, left + (white + 1) * width / 56, client.bottom};
            ++white;
        }
    }
    return boxes;
}
LRESULT CALLBACK auditionProcedure(HWND h, UINT m, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR) {
    if (m == WM_ERASEBKGND)
        return 1;
    if (m == WM_LBUTTONDOWN || (m == WM_MOUSEMOVE && GetCapture() == h)) {
        if (m == WM_LBUTTONDOWN) {
            SetFocus(h);
            SetCapture(h);
        }
        RECT client;
        GetClientRect(h, &client);
        auto boxes = auditionRects(client);
        POINT point{GET_X_LPARAM(l), GET_Y_LPARAM(l)};
        int note = -1;
        for (int pass = 1; pass >= 0 && note < 0; --pass)
            for (int n = 0; n < 96; ++n)
                if (blackKey(n + 3) == bool(pass) && PtInRect(&boxes[n], point)) {
                    note = n + 15;
                    break;
                }
        if (note != mouseAudition.note) {
            releaseAudition(mouseAudition);
            beginAudition(mouseAudition, note);
            InvalidateRect(h, nullptr, FALSE);
        }
        return 0;
    }
    if (m == WM_LBUTTONUP || m == WM_CANCELMODE || m == WM_CAPTURECHANGED) {
        releaseAudition(mouseAudition);
        if (m != WM_CAPTURECHANGED && GetCapture() == h)
            ReleaseCapture();
        InvalidateRect(h, nullptr, FALSE);
        return 0;
    }
    if (m == WM_PAINT) {
        PAINTSTRUCT ps;
        HDC target = BeginPaint(h, &ps), dc = CreateCompatibleDC(target);
        RECT client;
        GetClientRect(h, &client);
        auto bitmap = CreateCompatibleBitmap(target, std::max(1L, client.right), std::max(1L, client.bottom));
        auto old = SelectObject(dc, bitmap);
        FillRect(dc, &client, panelBrush);
        SelectObject(dc, voiceFont);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, textColor);
        auto title = L"o" + std::to_wstring(auditionOctave) +
                     tr(L"  PageUp / PageDown: オクターブ  |  クリック / AWSEDFTGYHUJK: 試奏",
                        L"  PageUp / PageDown: octave  |  Click / AWSEDFTGYHUJK: audition");
        RECT label{scale(4), 0, client.right, scale(24)};
        DrawTextW(dc, title.c_str(), -1, &label, DT_SINGLELINE | DT_VCENTER);
        auto boxes = auditionRects(client);
        for (int pass = 0; pass < 2; ++pass)
            for (int n = 0; n < 96; ++n) {
                bool black = blackKey(n + 3);
                if (black != bool(pass))
                    continue;
                bool held = mouseAudition.note == n + 15;
                for(const auto& active:retainedVoices->midiNotes) held |= active.load()==n+15;
                for (auto &key : auditionKeys)
                    held |= key.note == n + 15;
                auto brush = CreateSolidBrush(held    ? RGB(145, 230, 65)
                                              : black ? RGB(15, 18, 28)
                                                      : RGB(211, 215, 224));
                FillRect(dc, &boxes[n], brush);
                DeleteObject(brush);
                FrameRect(dc, &boxes[n], panelBrush);
                int offset = n + 15 - (auditionOctave + 1) * 12;
                if (offset >= 0 && offset < 13) {
                    SetTextColor(dc, black ? RGB(245, 245, 250) : RGB(10, 15, 25));
                    RECT text = boxes[n];
                    text.top = std::max(text.top, text.bottom - scale(22));
                    DrawTextW(dc, &auditionMap[offset], 1, &text, DT_CENTER | DT_BOTTOM | DT_SINGLELINE);
                }
            }
        BitBlt(target, 0, 0, client.right, client.bottom, dc, 0, 0, SRCCOPY);
        SelectObject(dc, old);
        DeleteObject(bitmap);
        DeleteDC(dc);
        EndPaint(h, &ps);
        return 0;
    }
    return DefSubclassProc(h, m, w, l);
}

std::array<int,16> shownMidiNotes{};
void refreshVoiceKeyboard() {
    if(!voiceKeyboard) return;
    bool changed=false;
    // Also recover from a dropped key-up during a capture/focus transition.
    for(int i=0;i<13;++i) if(auditionKeys[i].note>=0 && !(GetAsyncKeyState(auditionMap[i]) & 0x8000)) {
        releaseAudition(auditionKeys[i]); changed=true;
    }
    for(int i=0;i<16;++i) {
        int note=retainedVoices->midiNotes[i].load();
        if(note!=shownMidiNotes[i]) {shownMidiNotes[i]=note;changed=true;}
    }
    if(changed) InvalidateRect(voiceKeyboard,nullptr,FALSE);
}
