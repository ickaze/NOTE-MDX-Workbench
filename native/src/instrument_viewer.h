// SPDX-License-Identifier: 0BSD
// Copyright (C) 2026 by I.C.KaZe
// Instrument viewer extracted from IndependentMDX; connected to the playback service.
#pragma once
#include "algorithm_layout.h"
#include "voice_text.h"
#include "instrument_waveform.h"
#include <deque>
namespace
{
HWND voiceWindow = nullptr;
HWND voiceEdit = nullptr;
HWND voiceSave = nullptr;
HWND voiceReset = nullptr;
mdx::Voice baselineVoice{};
std::array<int, 47> baselineParameters{};
bool baselineVoiceValid = false;
bool voiceDirty = false, updatingVoiceEdit = false;
std::wstring lastPreviewText;

std::mutex voiceEditsMutex;
struct VoiceEditRequest
{
  int id;
  std::string mml;
};
std::deque<VoiceEditRequest> voiceEdits;
bool previewEditedVoice(bool reportError);
bool finishVoiceEdit();
void refreshVoiceKeyboard();
HWND registerEdit = nullptr;
HWND voiceChannel = nullptr;
HWND voiceList = nullptr;
HWND voiceListLabel = nullptr;
const COLORREF textColor = RGB(178, 178, 255);
const COLORREF panelColor = RGB(7, 8, 24);
const COLORREF editColor = RGB(20, 22, 48);
HBRUSH panelBrush = nullptr;
HBRUSH editBrush = nullptr;
HBRUSH accentBrush = nullptr;
int scale(int value)
{
  static const int dpi = []
  {
    HDC dc = GetDC(nullptr);
    int value = GetDeviceCaps(dc, LOGPIXELSX);
    ReleaseDC(nullptr, dc);
    return value;
  }();
  return MulDiv(value, dpi, 96);
}
bool blackKey(int note)
{
  const int pitch = note % 12;
  return ((pitch == 1) || (pitch == 3) || (pitch == 6) || (pitch == 8) || (pitch == 10));
}
void updateAdvancedLanguage()
{
  if (voiceWindow != nullptr)
  {
    if (voiceSave)
      SetWindowTextW(voiceSave, tr(L"保存", L"Save"));
    if (voiceReset)
      SetWindowTextW(voiceReset, tr(L"リセット", L"Reset"));
    if (voiceListLabel)
      SetWindowTextW(voiceListLabel, tr(L"音色一覧（* 使用済み）", L"Instruments (* used)"));
    SetWindowTextW(voiceWindow, tr(L"FM音色 — MML / レジスタ / 接続図",
                                   L"FM instruments — MML / registers / algorithm"));
  }
}
bool rebuildingVoiceList = false;
int selectedVoice = -1;
int shownVoiceId = -1;
int editedVoiceId = -1;
uint64_t displayedCatalog = UINT64_MAX;
std::array<bool, 256> displayedUsed{}, displayedDefined{};
std::array<bool, 256> changedVoiceList{};
bool voiceTextCacheDirty = true;
HFONT parameterFont = nullptr, limitFont = nullptr;
uint64_t observedCatalog = UINT64_MAX;
int observedChannelVoice = -1;
int shownAlgorithm = 0, shownFeedback = 0;
std::wstring shownMml, shownRegisters;
HFONT voiceFont = nullptr;
std::array<HWND, 47> parameterKnobs{};
std::array<int, 47> parameterValues{};
void setParameterValue(int n, int value);
std::array<HWND, 11> parameterHeaders{};
std::array<HWND, 4> parameterRows{};
std::array<HWND, 3> globalParameterHeaders{};
bool syncingParameters = false, voiceWaveWasActive = false;
mdx::Voice parameterVoice{};
constexpr int parameterMaximum[11] = {31, 31, 31, 15, 15, 127, 3, 15, 7, 3, 1};
int parameterLimit(int n) { return n < 44 ? parameterMaximum[n % 11] : n == 46 ? 15 : 7; }
void setParameterValue(int n, int value)
{
  int next = std::clamp(value, 0, parameterLimit(n));
  bool changed = next != parameterValues[n];
  parameterValues[n] = next;
  if (changed)
    InvalidateRect(parameterKnobs[n], nullptr, FALSE);
}
void syncVoiceParameters(const mdx::Voice &voice)
{
  parameterVoice = voice;
  syncingParameters = true;
  auto values = mdx::voiceParameterValues(voice);
  for (int n = 0; n < 47; ++n)
    if (parameterKnobs[n])
      setParameterValue(n, values[n]);
  syncingParameters = false;
}
void changedVoiceParameter()
{
  if (syncingParameters)
    return;
  // Use the same validated MML path for both editing surfaces.
  std::ostringstream source;
  source << "@" << int(parameterVoice[0]) << " = {\r\n";
  for (int row = 0; row < 4; ++row)
  {
    for (int col = 0; col < 11; ++col)
      source << parameterValues[row * 11 + col] << ',';
    source << "\r\n";
  }
  for (int col = 0; col < 3; ++col)
  {
    if (col)
      source << ',';
    source << parameterValues[44 + col];
  }
  source << "\r\n}\r\n";
  std::string error;
  auto parsed = mdx::parseVoiceMml(source.str(), error);
  if (!parsed)
    return;
  updatingVoiceEdit = true;
  SetWindowTextW(voiceEdit, wide(mdx::voiceMml(*parsed, (*parsed)[0])).c_str());
  updatingVoiceEdit = false;
  voiceDirty = true;
  previewEditedVoice(false);
}
LRESULT CALLBACK parameterProcedure(HWND hwnd, UINT message, WPARAM wp, LPARAM lp, UINT_PTR,
                                    DWORD_PTR index)
{
  int n = int(index);
  static HWND dragging = nullptr;
  static int originX = 0, originValue = 0, remainder = 0;
  static HWND wheelWindow = nullptr;
  auto change = [&](int value)
  {
    int next = std::clamp(value, 0, parameterLimit(n));
    if (next != parameterValues[n])
    {
      setParameterValue(n, next);
      changedVoiceParameter();
    }
  };
  if (message == WM_MOUSEWHEEL)
  {
    if (wheelWindow != hwnd)
    {
      remainder = 0;
      wheelWindow = hwnd;
    }
    remainder += GET_WHEEL_DELTA_WPARAM(wp);
    change(parameterValues[n] + remainder / WHEEL_DELTA);
    remainder %= WHEEL_DELTA;
    return 0;
  }
  if (hwnd == parameterKnobs[n])
  {
    if (message == WM_LBUTTONDOWN)
    {
      SetFocus(hwnd);
      dragging = hwnd;
      originX = GET_X_LPARAM(lp);
      originValue = parameterValues[n];
      SetCapture(hwnd);
      return 0;
    }
    if (message == WM_MOUSEMOVE && dragging == hwnd)
    {
      RECT r;
      GetClientRect(hwnd, &r);
      int diameter = std::max(1, scale(20));
      change(originValue + int(std::lround(double(GET_X_LPARAM(lp) - originX) * parameterLimit(n) /
                                           (2 * diameter))));
      return 0;
    }
    if (message == WM_LBUTTONUP)
    {
      if (GetCapture() == hwnd)
        ReleaseCapture();
      dragging = nullptr;
      return 0;
    }
    if (message == WM_CAPTURECHANGED)
      dragging = nullptr;
    if (message == WM_ERASEBKGND)
      return 1;
    if (message == WM_PAINT)
    {
      PAINTSTRUCT ps;
      HDC dc = BeginPaint(hwnd, &ps);
      RECT r;
      GetClientRect(hwnd, &r);
      FillRect(dc, &r, editBrush);
      bool changed = baselineVoiceValid && parameterValues[n] != baselineParameters[n];
      COLORREF color = changed ? RGB(255, 80, 80) : textColor;
      int radius = std::max(3, std::min(int(r.right / 2 - 2), scale(10)));
      int cx = r.right / 2, cy = radius + 2;
      HPEN pen = CreatePen(PS_SOLID, std::max(1, scale(1)), color);
      auto oldPen = SelectObject(dc, pen);
      auto oldBrush = SelectObject(dc, editBrush);
      Ellipse(dc, cx - radius, cy - radius, cx + radius, cy + radius);
      if (baselineVoiceValid)
      {
        double oldAngle = (-150.0 + 300.0 * baselineParameters[n] / parameterLimit(n)) *
                          3.141592653589793 / 180.0;
        // A translucent reference pointer, composited over the flat knob background.
        COLORREF reference = RGB((GetRValue(textColor) + GetRValue(editColor)) * 0.5,
                                 (GetGValue(textColor) + GetGValue(editColor)) * 0.5,
                                 (GetBValue(textColor) + GetBValue(editColor)) * 0.5);
        HPEN ghost = CreatePen(PS_SOLID, 1, reference);
        auto currentPen = SelectObject(dc, ghost);
        MoveToEx(dc, cx, cy, nullptr);
        LineTo(dc, cx + int(std::sin(oldAngle) * (radius - 2)),
               cy - int(std::cos(oldAngle) * (radius - 2)));
        SelectObject(dc, currentPen);
        DeleteObject(ghost);
      }
      double angle =
          (-150.0 + 300.0 * parameterValues[n] / parameterLimit(n)) * 3.141592653589793 / 180.0;
      MoveToEx(dc, cx, cy, nullptr);
      LineTo(dc, cx + int(std::sin(angle) * (radius - 3)),
             cy - int(std::cos(angle) * (radius - 3)));
      SelectObject(dc, oldPen);
      SelectObject(dc, oldBrush);
      DeleteObject(pen);
      auto oldFont = SelectObject(dc, limitFont);
      SetBkMode(dc, TRANSPARENT);
      SetTextColor(dc, textColor);
      RECT minimum{r.right / 2 - scale(16), scale(16), r.right / 2 - scale(9), scale(28)};
      RECT maximum{r.right / 2 + scale(9), scale(16), std::min(r.right, r.right / 2 + scale(19)), scale(28)};
      DrawTextW(dc, L"0", 1, &minimum, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
      auto maxText = std::to_wstring(parameterLimit(n));
      DrawTextW(dc, maxText.c_str(), -1, &maximum, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
      RECT current{r.right / 2 - scale(9), scale(16), r.right / 2 + scale(9), scale(28)};
      FillRect(dc, &current, editBrush);
      SelectObject(dc, parameterFont);
      SetTextColor(dc, color);
      auto value = std::to_wstring(parameterValues[n]);
      DrawTextW(dc, value.c_str(), -1, &current, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
      SelectObject(dc, oldFont);
      EndPaint(hwnd, &ps);
      return 0;
    }
  }
  if (message == WM_CHAR || message == WM_SYSCHAR)
    return 0;
  return DefSubclassProc(hwnd, message, wp, lp);
}
struct VoiceTextSurface
{
  HDC composite = nullptr, text = nullptr;
  HBITMAP bitmap = nullptr, textBitmap = nullptr;
  HGDIOBJ old = nullptr, oldText = nullptr;
  int width = 0, height = 0;
  void clear()
  {
    if (composite)
    {
      SelectObject(composite, old);
      DeleteObject(bitmap);
      DeleteDC(composite);
    }
    if (text)
    {
      SelectObject(text, oldText);
      DeleteObject(textBitmap);
      DeleteDC(text);
    }
    composite = text = nullptr;
    bitmap = textBitmap = nullptr;
    width = height = 0;
  }
  void size(HDC dc, int w, int h)
  {
    if (width == w && height == h)
      return;
    clear();
    width = w;
    height = h;
    composite = CreateCompatibleDC(dc);
    text = CreateCompatibleDC(dc);
    bitmap = CreateCompatibleBitmap(dc, w, h);
    textBitmap = CreateCompatibleBitmap(dc, w, h);
    old = SelectObject(composite, bitmap);
    oldText = SelectObject(text, textBitmap);
    voiceTextCacheDirty = true;
  }
} voiceTextSurface;
void paintChangedParameters(HDC dc, HWND hwnd)
{
  if (!baselineVoiceValid)
    return;
  int length = GetWindowTextLengthW(hwnd);
  std::wstring text(length + 1, L'\0');
  GetWindowTextW(hwnd, text.data(), length + 1);
  text.resize(length);
  std::string error;
  int bytes = WideCharToMultiByte(CP_UTF8, 0, text.data(), length, nullptr, 0, nullptr, nullptr);
  std::string utf8(bytes, '\0');
  WideCharToMultiByte(CP_UTF8, 0, text.data(), length, utf8.data(), bytes, nullptr, nullptr);
  if (!mdx::parseVoiceMml(utf8, error))
    return;
  auto at = text.find(L'{');
  if (at == std::wstring::npos)
    return;
  ++at;
  auto oldFont = SelectObject(dc, voiceFont);
  SetTextColor(dc, textColor);
  SetBkColor(dc, RGB(104, 35, 53));
  SetBkMode(dc, OPAQUE);
  int n = 0;
  while (at < text.size() && n < 47)
  {
    if (text[at] == L';' || text[at] == L'*' ||
        (text[at] == L'/' && at + 1 < text.size() &&
         (text[at + 1] == L'/' || text[at + 1] == L'*')))
    {
      at = text.find(L'\n', at);
      if (at == std::wstring::npos)
        break;
      continue;
    }
    wchar_t c = text[at];
    if ((c >= L'0' && c <= L'9') || c == L'+' || c == L'-' || c == L'$')
    {
      size_t begin = at;
      if (c == L'+' || c == L'-')
        ++at;
      bool hex = at < text.size() && text[at] == L'$';
      if (hex)
        ++at;
      while (at < text.size() && ((text[at] >= L'0' && text[at] <= L'9') ||
                                  (hex && ((text[at] >= L'a' && text[at] <= L'f') ||
                                           (text[at] >= L'A' && text[at] <= L'F')))))
        ++at;
      if (parameterValues[n] != baselineParameters[n])
      {
        auto pos = SendMessageW(hwnd, EM_POSFROMCHAR, begin, 0);
        if (pos != -1)
          TextOutW(dc, int(short(LOWORD(pos))), int(short(HIWORD(pos))), text.data() + begin,
                   int(at - begin));
      }
      ++n;
    }
    else
      ++at;
  }
  SelectObject(dc, oldFont);
}
LRESULT CALLBACK voiceTextProcedure(HWND hwnd, UINT message, WPARAM wp, LPARAM lp, UINT_PTR,
                                    DWORD_PTR)
{
  if (message == WM_SYSCHAR)
    return 0;
  if (message == WM_CHAR && wp == 1) { SendMessageW(hwnd, EM_SETSEL, 0, -1); return 0; }
  if (message == WM_CHAR && wp == 26 && !SendMessageW(hwnd, EM_CANUNDO, 0, 0)) return 0;
  if (message == WM_CHAR && wp < 32 && wp != VK_BACK && wp != VK_RETURN && wp != 3 && wp != 22 && wp != 24 && wp != 26) return 0;
  if (message == WM_CHAR && (wp == VK_BACK || wp == VK_ESCAPE || wp == VK_TAB))
  {
    DWORD start = 0, end = 0;
    SendMessageW(hwnd, EM_GETSEL, WPARAM(&start), LPARAM(&end));
    if (wp == VK_ESCAPE || wp == VK_TAB || (wp == VK_BACK && start == 0 && end == 0))
      return 0;
  }
  if (message == WM_KEYDOWN && (wp == VK_NEXT || wp == VK_PRIOR || wp == VK_DELETE))
  {
    DWORD start = 0, end = 0;
    SendMessageW(hwnd, EM_GETSEL, WPARAM(&start), LPARAM(&end));
    DWORD length = DWORD(GetWindowTextLengthW(hwnd));
    if (start == end && ((wp == VK_PRIOR && start == 0) ||
        ((wp == VK_NEXT || wp == VK_DELETE) && end == length)))
      return 0;
  }
  if (message == WM_ERASEBKGND)
    return 1;
  if (message != WM_PAINT)
  {
    if (message == WM_SETTEXT || message == WM_CHAR || message == WM_KEYDOWN ||
        message == WM_LBUTTONDOWN || message == WM_LBUTTONUP || message == WM_MOUSEMOVE ||
        message == WM_MOUSEWHEEL || message == WM_HSCROLL || message == WM_VSCROLL ||
        message == EM_SETSEL || message == EM_REPLACESEL || message == WM_SETFONT ||
        message == WM_SETFOCUS || message == WM_KILLFOCUS || message == WM_SIZE ||
        message == WM_PASTE || message == WM_CUT || message == WM_CLEAR || message == EM_UNDO)
      voiceTextCacheDirty = true;
    if (message == WM_NCDESTROY)
      voiceTextSurface.clear();
    return DefSubclassProc(hwnd, message, wp, lp);
  }
  PAINTSTRUCT ps;
  HDC dc = BeginPaint(hwnd, &ps);
  RECT rect;
  GetClientRect(hwnd, &rect);
  int w = rect.right, h = rect.bottom;
  if (w <= 0 || h <= 0)
  {
    EndPaint(hwnd, &ps);
    return 0;
  }
  voiceTextSurface.size(dc, w, h);
  HDC buffer = voiceTextSurface.composite, text = voiceTextSurface.text;
  FillRect(buffer, &rect, editBrush);
  bool active = running;
  for (const auto &note : retainedVoices->midiNotes)
    active |= note.load() >= 0;
  if (active)
  {
    mdx::MonitorFrame frame;
    {
      std::lock_guard<std::mutex> lock(monitorView->mutex);
      frame = monitorView->frame;
    }
    const int selected = int(SendMessageW(voiceChannel, CB_GETCURSEL, 0, 0));
    uint32_t auditionMask = 0;
    for (size_t ch = 0; ch < retainedVoices->midiNotes.size(); ++ch)
      if (retainedVoices->midiNotes[ch].load() >= 0)
        auditionMask |= 1u << ch;
    const auto mixed = mdx::instrumentWaveform(frame, selected, auditionMask);
    HPEN pen = CreatePen(PS_SOLID, 1, RGB(62, 92, 128));
    auto previous = SelectObject(buffer, pen);
    for (int x = 0; x < w; ++x)
    {
      size_t sample = (frame.cursor + size_t(x) * mixed.size() / w) % mixed.size();
      int y = h / 2 - int(std::clamp(mixed[sample], -1.0, 1.0) * (h / 2 - 2));
      if (!x)
        MoveToEx(buffer, x, y, nullptr);
      else
        LineTo(buffer, x, y);
    }
    SelectObject(buffer, previous);
    DeleteObject(pen);
  }
  if (voiceTextCacheDirty)
  {
    FillRect(text, &rect, editBrush);
    DefSubclassProc(hwnd, WM_PRINTCLIENT, WPARAM(text), PRF_CLIENT);
    paintChangedParameters(text, hwnd);
    voiceTextCacheDirty = false;
  }
  TransparentBlt(buffer, 0, 0, w, h, text, 0, 0, w, h, editColor);
  BitBlt(dc, 0, 0, w, h, buffer, 0, 0, SRCCOPY);
  EndPaint(hwnd, &ps);
  return 0;
}
void refreshVoiceWindow()
{
  if (!voiceWindow || !IsWindowVisible(voiceWindow))
    return;
  refreshVoiceKeyboard();
  bool active = running;
  for (const auto &note : retainedVoices->midiNotes)
    active |= note.load() >= 0;
  if (active || voiceWaveWasActive)
    InvalidateRect(voiceEdit, nullptr, FALSE);
  voiceWaveWasActive = active;
  int ch = std::max(0, int(SendMessageW(voiceChannel, CB_GETCURSEL, 0, 0)));
  mdx::Voice voice{}, initial{};
  std::array<uint8_t, 256> regs{};
  std::array<bool, 256> used{}, defined{}, changed{};
  uint64_t revision;
  int channelId, id;
  bool initialValid = false;
  {
    std::lock_guard<std::mutex> lock(retainedVoices->mutex);
    revision = retainedVoices->catalogRevision;
    used = retainedVoices->used;
    defined = retainedVoices->defined;
    for (int n = 0; n < 256; ++n)
      changed[n] = retainedVoices->modified(n);
    channelId = retainedVoices->ids[ch];
    if (running && channelId >= 0 && channelId != observedChannelVoice)
      selectedVoice = -1;
    observedChannelVoice = channelId;
    if (selectedVoice >= 0 && !defined[selectedVoice])
      selectedVoice = -1;
    id = selectedVoice >= 0 ? selectedVoice : channelId;
    if (id < 0)
      id = ch;
    regs = retainedVoices->registers;
    voice = defined[id] ? retainedVoices->definitions[id]
                        : mdx::registerVoice(regs, ch, retainedVoices->voices[ch][2]);
    voice[0] = uint8_t(id);
    initialValid = retainedVoices->initialDefined[id];
    initial = initialValid ? retainedVoices->initialDefinitions[id] : voice;
    initial[0] = uint8_t(id);
  }
  bool rebuild = displayedCatalog == UINT64_MAX || displayedDefined != defined;
  if (rebuild || displayedCatalog != revision || displayedUsed != used ||
      changedVoiceList != changed)
  {
    rebuildingVoiceList = true;
    if (rebuild)
    {
      SendMessageW(voiceList, WM_SETREDRAW, FALSE, 0);
      ListView_DeleteAllItems(voiceList);
    }
    int index = 0;
    for (int n = 0; n < 256; ++n)
      if (defined[n])
      {
        if (rebuild || displayedUsed[n] != used[n])
        {
          wchar_t title[32];
          swprintf(title, 32, L"%s@%03d", used[n] ? L"* " : L"", n);
          if (rebuild)
          {
            LVITEMW item{};
            item.mask = LVIF_TEXT | LVIF_PARAM;
            item.iItem = index;
            item.pszText = title;
            item.lParam = n;
            ListView_InsertItem(voiceList, &item);
          }
          else
            ListView_SetItemText(voiceList, index, 0, title);
        }
        if (rebuild)
          ListView_SetItemState(voiceList, index, n == selectedVoice ? LVIS_SELECTED : 0,
                                LVIS_SELECTED);
        ++index;
      }
    displayedCatalog = revision;
    displayedDefined = defined;
    displayedUsed = used;
    changedVoiceList = changed;
    if (rebuild)
      SendMessageW(voiceList, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(voiceList, nullptr, FALSE);
    rebuildingVoiceList = false;
  }
  bool switching = shownVoiceId != id;
  bool catalogChanged = observedCatalog != revision;
  observedCatalog = revision;
  bool canSave = std::any_of(changed.begin(), changed.end(), [](bool b) { return b; });
  if (bool(IsWindowEnabled(voiceSave)) != canSave)
    EnableWindow(voiceSave, canSave);
  // Avoid replacing invalid in-progress input, but follow real playback voice changes.
  if (!switching && !catalogChanged)
    return;
  baselineVoice = initial;
  baselineVoiceValid = initialValid || defined[id];
  baselineParameters = mdx::voiceParameterValues(initial);
  syncVoiceParameters(voice);
  voiceTextCacheDirty = true;
  voiceDirty = voice != initial;
  editedVoiceId = shownVoiceId = id;
  EnableWindow(voiceSave, std::any_of(changed.begin(), changed.end(), [](bool b) { return b; }));
  EnableWindow(voiceReset, voiceDirty);
  auto mml = wide(mdx::voiceMml(voice, id));
  if (switching || mml != shownMml)
  {
    shownMml = mml;
    lastPreviewText = mml;
    updatingVoiceEdit = true;
    SetWindowTextW(voiceEdit, mml.c_str());
    updatingVoiceEdit = false;
    voiceTextCacheDirty = true;
  }
  regs.fill(0);
  regs[0x20 + ch] = voice[1];
  for (int g = 0; g < 6; ++g)
    for (int op = 0; op < 4; ++op)
      regs[0x40 + g * 32 + op * 8 + ch] = voice[3 + g * 4 + op];
  auto reg = wide(mdx::voiceRegisters(regs, ch));
  if (reg != shownRegisters)
  {
    shownRegisters = reg;
    SetWindowTextW(registerEdit, reg.c_str());
  }
  shownAlgorithm = voice[1] & 7;
  shownFeedback = (voice[1] >> 3) & 7;
  RECT client;
  GetClientRect(voiceWindow, &client);
  RECT area{client.right * 2 / 3, client.bottom - scale(435), client.right - scale(8),
            client.bottom - scale(255)};
  InvalidateRect(voiceWindow, &area, FALSE);
  for (auto knob : parameterKnobs)
    InvalidateRect(knob, nullptr, FALSE);
}
void applyVoiceNumber(int id)
{
  selectedVoice = id;
  editedVoiceId = id;
  voiceDirty = false;
  lastPreviewText.clear();
  shownVoiceId = -1;
  shownMml.clear();
  int channel = int(SendMessageW(voiceChannel, CB_GETCURSEL, 0, 0));
  if (channel >= 0 && channel < 8)
  {
    {
      std::lock_guard<std::mutex> lock(retainedVoices->mutex);
      auto &v = retainedVoices->definitions[selectedVoice];
      retainedVoices->voices[channel] = v;
      retainedVoices->ids[channel] = selectedVoice;
      retainedVoices->valid[channel] = true;
      retainedVoices->used[selectedVoice] = true;
      auto &regs = retainedVoices->registers;
      regs[0x20 + channel] = (regs[0x20 + channel] & 192) | (v[1] & 63);
      for (int g = 0; g < 6; ++g)
        for (int op = 0; op < 4; ++op)
          regs[0x40 + g * 32 + op * 8 + channel] = v[3 + g * 4 + op];
    }
    retainedVoices->requestedVoice[channel] = selectedVoice;
    // A private queue command retains all 8 bits of the MDX voice number.
    midiQueue->push(0xf0u | uint32_t(channel) | (uint32_t(selectedVoice) << 8));
  }
  SetFocus(voiceList);
  refreshVoiceWindow();
}
std::wstring currentVoiceText()
{
  int count = GetWindowTextLengthW(voiceEdit);
  std::wstring text(count + 1, L'\0');
  GetWindowTextW(voiceEdit, text.data(), count + 1);
  text.resize(count);
  return text;
}
std::string voiceUtf8(const std::wstring &text)
{
  int count =
      WideCharToMultiByte(CP_UTF8, 0, text.data(), int(text.size()), nullptr, 0, nullptr, nullptr);
  std::string result(count, '\0');
  WideCharToMultiByte(CP_UTF8, 0, text.data(), int(text.size()), result.data(), count, nullptr,
                      nullptr);
  return result;
}
bool previewEditedVoice(bool reportError)
{
  if (!voiceDirty)
  {
    return true;
  }
  auto text = currentVoiceText();
  if (!reportError && text == lastPreviewText)
    return true;
  std::string error;
  auto parsed = mdx::parseVoiceMml(voiceUtf8(text), error);
  if (!parsed)
  {
    EnableWindow(voiceSave, TRUE);
    EnableWindow(voiceReset, TRUE);
    if (reportError)
    {
      auto message =
          std::wstring(tr(L"音色定義を解析できません。\n", L"Invalid instrument definition.\n")) +
          wide(error);
      MessageBoxW(voiceWindow, message.c_str(), L"NOTE-MDX-Workbench", MB_OK);
    }
    return false;
  }
  struct
  {
    int id;
    mdx::Voice bytes;
  } definition{(*parsed)[0], *parsed};
  syncVoiceParameters(definition.bytes);
  voiceDirty = !baselineVoiceValid || definition.bytes != baselineVoice;
  EnableWindow(voiceSave, voiceDirty);
  EnableWindow(voiceReset, voiceDirty);
  voiceTextCacheDirty = true;
  InvalidateRect(voiceEdit, nullptr, FALSE);
  lastPreviewText = text;
  shownMml = text;

  editedVoiceId = definition.id;
  // Changing the ID is an add, with the selected definition as its insertion anchor.
  int channel = std::max(0, int(SendMessageW(voiceChannel, CB_GETCURSEL, 0, 0)));
  {
    std::lock_guard<std::mutex> lock(retainedVoices->mutex);
    retainedVoices->definitions[definition.id] = definition.bytes;
    retainedVoices->defined[definition.id] = true;
    for (int ch = 0; ch < 8; ++ch)
      if (retainedVoices->ids[ch] == definition.id)
        retainedVoices->requestedVoice[ch] = definition.id;
    retainedVoices->voices[channel] = definition.bytes;
    retainedVoices->ids[channel] = definition.id;
    retainedVoices->valid[channel] = true;
    bool anyChanged = false;
    for (int id = 0; id < 256; ++id)
      anyChanged |= retainedVoices->modified(id);
    EnableWindow(voiceSave, anyChanged);
    ++retainedVoices->catalogRevision;
    observedCatalog = retainedVoices->catalogRevision;
  }
  retainedVoices->requestedVoice[channel] = definition.id;
  midiQueue->push(0xf0u | uint32_t(channel) | (uint32_t(definition.id) << 8));
  std::array<uint8_t, 256> previewRegisters{};
  previewRegisters[0x20 + channel] = definition.bytes[1];
  for (int group = 0; group < 6; ++group)
    for (int op = 0; op < 4; ++op)
      previewRegisters[0x40 + group * 32 + op * 8 + channel] = definition.bytes[3 + group * 4 + op];
  shownRegisters = wide(mdx::voiceRegisters(previewRegisters, channel));
  SetWindowTextW(registerEdit, shownRegisters.c_str());
  shownAlgorithm = definition.bytes[1] & 7;
  shownFeedback = (definition.bytes[1] >> 3) & 7;
  RECT rect;
  GetClientRect(voiceWindow, &rect);
  RECT area{rect.right * 2 / 3, rect.bottom - scale(435), rect.right - scale(8),
            rect.bottom - scale(255)};
  InvalidateRect(voiceWindow, &area, FALSE);
  return true;
}
bool finishVoiceEdit()
{
  if (!previewEditedVoice(true))
    return false;
  std::vector<VoiceEditRequest> edits;
  {
    std::lock_guard<std::mutex> lock(retainedVoices->mutex);
    for (int id = 0; id < 256; ++id)
      if (retainedVoices->modified(id))
        edits.push_back({id, mdx::voiceMml(retainedVoices->definitions[id], id)});
  }
  {
    std::lock_guard<std::mutex> lock(voiceEditsMutex);
    for (auto &edit : edits)
    {
      auto pending = std::find_if(voiceEdits.begin(), voiceEdits.end(),
                                  [&](const auto &item) { return item.id == edit.id; });
      if (pending == voiceEdits.end())
        voiceEdits.push_back(std::move(edit));
      else
        *pending = std::move(edit);
    }
  }
  return true;
}
#include "voice_keyboard.h"
int voiceMinimumHeight()
{
  HDC dc = GetDC(nullptr);
  HFONT temporary = nullptr;
  if (!voiceFont)
    temporary = CreateFontW(-MulDiv(10, GetDeviceCaps(dc, LOGPIXELSY), 72), 0, 0, 0,
                           FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                           OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
                           FIXED_PITCH, L"Consolas");
  auto previous = SelectObject(dc, voiceFont ? voiceFont : temporary);
  TEXTMETRICW metrics{};
  GetTextMetricsW(dc, &metrics);
  if (previous) SelectObject(dc, previous);
  if (temporary) DeleteObject(temporary);
  ReleaseDC(nullptr, dc);
  // Nine full text rows, client padding, border and the optional horizontal bar.
  int textHeight = 9 * (metrics.tmHeight + metrics.tmExternalLeading) +
                   2 * GetSystemMetrics(SM_CYBORDER) + GetSystemMetrics(SM_CYHSCROLL) + scale(4);
  RECT rect{0, 0, scale(700), scale(495) + textHeight};
  AdjustWindowRectEx(&rect, WS_OVERLAPPEDWINDOW, FALSE, 0);
  return rect.bottom - rect.top;
}
void saveVoiceWindowSize(HWND hwnd)
{
  WINDOWPLACEMENT placement{};
  placement.length = sizeof(placement);
  if (!GetWindowPlacement(hwnd, &placement)) return;
  saveRuntimeSettings(); // Ensure the shared INI is created with its Unicode encoding.
  auto &r = placement.rcNormalPosition;
  int width = MulDiv(r.right - r.left, 96, scale(96));
  int height = MulDiv(r.bottom - r.top, 96, scale(96));
  WritePrivateProfileStringW(L"Settings", L"InstrumentWidth", std::to_wstring(width).c_str(), settingsPath.c_str());
  WritePrivateProfileStringW(L"Settings", L"InstrumentHeight", std::to_wstring(height).c_str(), settingsPath.c_str());
  // WINDOWPLACEMENT uses workspace coordinates; CreateWindowEx uses screen coordinates.
  MONITORINFO monitor{};
  monitor.cbSize = sizeof(monitor);
  POINT position{r.left, r.top};
  if (GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &monitor)) {
    position.x += monitor.rcWork.left - monitor.rcMonitor.left;
    position.y += monitor.rcWork.top - monitor.rcMonitor.top;
  }
  WritePrivateProfileStringW(L"Settings", L"InstrumentX", std::to_wstring(position.x).c_str(), settingsPath.c_str());
  WritePrivateProfileStringW(L"Settings", L"InstrumentY", std::to_wstring(position.y).c_str(), settingsPath.c_str());
  WritePrivateProfileStringW(nullptr, nullptr, nullptr, settingsPath.c_str());
}
LRESULT CALLBACK silentVoiceControl(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR kind)
{
  if (msg == WM_SYSCHAR) return 0;
  if (msg == WM_CHAR && kind == 1 && wp != VK_SPACE) return 0;
  if (msg == WM_CHAR && kind == 2 && wp >= 32) {
    wchar_t text[] = {wchar_t(wp), 0};
    if (SendMessageW(hwnd, CB_FINDSTRING, -1, LPARAM(text)) == CB_ERR) return 0;
  }
  return DefSubclassProc(hwnd, msg, wp, lp);
}
LRESULT CALLBACK voiceProcedure(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
  if (msg == WM_SYSCHAR)
    return 0;
  if (msg == WM_GETMINMAXINFO)
  {
    auto info = reinterpret_cast<MINMAXINFO *>(lp);
    info->ptMinTrackSize = {scale(700), voiceMinimumHeight()};
    return 0;
  }
  if (msg == WM_CREATE)
  {
    auto child = [&](const wchar_t *cls, const wchar_t *text, DWORD style, int id)
    {
      HWND control = CreateWindowW(cls, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 1, 1, hwnd,
                           HMENU(INT_PTR(id)), GetModuleHandleW(nullptr), nullptr);
      DWORD_PTR kind = lstrcmpiW(cls, L"BUTTON") == 0 ? 1 : lstrcmpiW(cls, L"COMBOBOX") == 0 ? 2 : 0;
      SetWindowSubclass(control, silentVoiceControl, 74, kind);
      return control;
    };
    voiceKeyboard = child(L"STATIC", L"", SS_NOTIFY | WS_TABSTOP, 6);
    SetWindowSubclass(voiceKeyboard, auditionProcedure, 1, 0);
    voiceChannel = child(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP, 1);
    for (int ch = 0; ch < 8; ++ch)
    {
      std::wstring text = L"FM ";
      text += wchar_t('A' + ch);
      SendMessageW(voiceChannel, CB_ADDSTRING, 0, LPARAM(text.c_str()));
    }
    SendMessageW(voiceChannel, CB_SETCURSEL, 0, 0);
    voiceEdit = child(
        L"EDIT", L"",
        ES_MULTILINE | ES_NOHIDESEL | ES_AUTOVSCROLL | WS_VSCROLL | WS_HSCROLL | WS_TABSTOP, 2);
    SetWindowSubclass(voiceEdit, voiceTextProcedure, 2, 0);
    const wchar_t *headers[] = {L"AR", L"DR", L"SR",  L"RR",  L"SL", L"OL",
                                L"KS", L"ML", L"DT1", L"DT2", L"AME"};
    for (int col = 0; col < 11; ++col)
      parameterHeaders[col] = child(L"STATIC", headers[col], 0, 200 + col);
    const wchar_t *globalHeaders[] = {L"CON", L"FL", L"OP"};
    for (int col = 0; col < 3; ++col)
      globalParameterHeaders[col] = child(L"STATIC", globalHeaders[col], 0, 230 + col);
    for (int row = 0; row < 4; ++row)
    {
      auto name = L"OP" + std::to_wstring(row + 1);
      parameterRows[row] = child(L"STATIC", name.c_str(), 0, 220 + row);
    }
    for (int n = 0; n < 47; ++n)
    {
      parameterKnobs[n] = child(L"STATIC", L"", SS_NOTIFY | WS_CLIPSIBLINGS | WS_TABSTOP, 300 + n);
      SetWindowSubclass(parameterKnobs[n], parameterProcedure, 3, n);
    }
    registerEdit =
        child(L"EDIT", L"",
              ES_MULTILINE | ES_READONLY | ES_NOHIDESEL | WS_VSCROLL | WS_HSCROLL | WS_TABSTOP, 3);
    voiceSave = child(L"BUTTON", tr(L"保存", L"Save"), WS_TABSTOP, 7);
    voiceReset = child(L"BUTTON", tr(L"リセット", L"Reset"), WS_TABSTOP, 8);
    EnableWindow(voiceSave, FALSE);
    EnableWindow(voiceReset, FALSE);
    voiceListLabel = child(L"STATIC", tr(L"音色一覧（* 使用済み）", L"Instruments (* used)"), 0, 5);
    voiceList =
        child(WC_LISTVIEWW, L"", LVS_LIST | LVS_SINGLESEL | LVS_SHOWSELALWAYS | WS_TABSTOP, 4);
    ListView_SetColumnWidth(voiceList, 0, scale(95));
    ListView_SetBkColor(voiceList, editColor);
    ListView_SetTextBkColor(voiceList, editColor);
    ListView_SetTextColor(voiceList, textColor);
    ListView_SetExtendedListViewStyle(voiceList, LVS_EX_DOUBLEBUFFER);
    displayedCatalog = UINT64_MAX;
    selectedVoice = -1;
    HDC fontDc = GetDC(hwnd);
    int fontDpi = GetDeviceCaps(fontDc, LOGPIXELSY);
    ReleaseDC(hwnd, fontDc);
    voiceFont = CreateFontW(-MulDiv(10, fontDpi, 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                            DEFAULT_QUALITY, FIXED_PITCH, L"Consolas");
    for (HWND h :
         {voiceChannel, voiceEdit, registerEdit, voiceList, voiceListLabel, voiceSave, voiceReset})
      SendMessageW(h, WM_SETFONT, WPARAM(voiceFont), TRUE);
    parameterFont = CreateFontW(-scale(9), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                0, 0, 0, FIXED_PITCH, L"Consolas");
    limitFont = CreateFontW(-scale(7), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0,
                            0, 0, 0, L"Segoe UI");
    for (auto h : parameterHeaders)
      SendMessageW(h, WM_SETFONT, WPARAM(voiceFont), TRUE);
    for (auto h : parameterRows)
      SendMessageW(h, WM_SETFONT, WPARAM(voiceFont), TRUE);
    for (auto h : globalParameterHeaders)
      SendMessageW(h, WM_SETFONT, WPARAM(voiceFont), TRUE);
    return 0;
  }
  if (msg == WM_SIZE)
  {
    int w = LOWORD(lp), h = HIWORD(lp), split = std::max(scale(260), w * 2 / 3);
    MoveWindow(voiceChannel, scale(12), scale(12), scale(160), scale(220), TRUE);
    MoveWindow(voiceSave, scale(190), scale(12), scale(100), scale(28), TRUE);
    MoveWindow(voiceReset, scale(300), scale(12), scale(100), scale(28), TRUE);
    MoveWindow(voiceEdit, scale(12), scale(52), std::max(scale(40), split - scale(20)),
               std::max(scale(40), h - scale(495)), TRUE);
    MoveWindow(registerEdit, split, scale(52), std::max(scale(40), w - split - scale(12)),
               std::max(scale(40), h - scale(495)), TRUE);
    int gridTop = h - scale(435), cell = (split - scale(60)) / 11;
    for (int col = 0; col < 11; ++col)
      MoveWindow(parameterHeaders[col], scale(52) + col * cell, gridTop, cell, scale(18), TRUE);
    for (int row = 0; row < 4; ++row)
      MoveWindow(parameterRows[row], scale(12), gridTop + scale(24 + row * 30),
                 scale(38), scale(16), TRUE);
    for (int col = 0; col < 3; ++col)
      MoveWindow(globalParameterHeaders[col], scale(52) + col * cell,
                 gridTop + scale(140), cell, scale(16), TRUE);
    for (int n = 0; n < 47; ++n)
    {
      int row = n < 44 ? n / 11 : 4, col = n < 44 ? n % 11 : n - 44;
      int x = scale(52) + col * cell;
      int width = std::min(cell, scale(40)),
          y = gridTop + scale(n < 44 ? 20 + row * 30 : 158);
      int left = x + (cell - width) / 2;
      MoveWindow(parameterKnobs[n], left, y, width, scale(28), TRUE);
    }
    MoveWindow(voiceKeyboard, scale(12), h - scale(248), w - scale(24), scale(96), TRUE);
    MoveWindow(voiceListLabel, scale(12), h - scale(148), w - scale(24), scale(24), TRUE);
    MoveWindow(voiceList, scale(12), h - scale(120), w - scale(24), scale(108), TRUE);
    InvalidateRect(hwnd, nullptr, TRUE);
    return 0;
  }
  if (msg == WM_COMMAND && LOWORD(wp) == 2 &&
      (HIWORD(wp) == EN_CHANGE || HIWORD(wp) == EN_UPDATE) && !updatingVoiceEdit)
  {
    voiceDirty = true;
    previewEditedVoice(false);
    return 0;
  }
  if (msg == WM_TIMER && wp == 3)
  {
    KillTimer(hwnd, 3);
    previewEditedVoice(false);
    return 0;
  }
  if (msg == WM_COMMAND && LOWORD(wp) == 8 && baselineVoiceValid)
  {
    updatingVoiceEdit = true;
    SetWindowTextW(voiceEdit, wide(mdx::voiceMml(baselineVoice, baselineVoice[0])).c_str());
    updatingVoiceEdit = false;
    lastPreviewText.clear();
    voiceDirty = true;
    previewEditedVoice(false);
    voiceDirty = false;
    EnableWindow(voiceReset, FALSE);
    refreshVoiceWindow();
    return 0;
  }
  if (msg == WM_COMMAND && LOWORD(wp) == 7)
  {
    finishVoiceEdit();
    return 0;
  }
  if (msg == WM_COMMAND && LOWORD(wp) == 1 && HIWORD(wp) == CBN_SELCHANGE)
  {
    selectedVoice = -1;
    shownVoiceId = -1;
    ListView_SetItemState(voiceList, -1, 0, LVIS_SELECTED);
    SetFocus(voiceChannel);
    refreshVoiceWindow();
    return 0;
  }
  if (msg == WM_NOTIFY && reinterpret_cast<NMHDR *>(lp)->hwndFrom == voiceList)
  {
    if (rebuildingVoiceList)
      return 0;
    auto n = reinterpret_cast<NMHDR *>(lp);
    if (n->code == NM_CUSTOMDRAW)
    {
      auto draw = reinterpret_cast<NMLVCUSTOMDRAW *>(lp);
      if (draw->nmcd.dwDrawStage == CDDS_PREPAINT)
        return CDRF_NOTIFYITEMDRAW;
      if (draw->nmcd.dwDrawStage == CDDS_ITEMPREPAINT)
      {
        int id = int(draw->nmcd.lItemlParam);
        draw->clrText = id >= 0 && id < 256 && changedVoiceList[id] ? RGB(255, 80, 80) : textColor;
        return CDRF_NEWFONT;
      }
    }
    if (n->code == LVN_ITEMCHANGED)
    {
      auto change = reinterpret_cast<NMLISTVIEW *>(lp);
      if ((change->uNewState & LVIS_SELECTED) && !(change->uOldState & LVIS_SELECTED))
      {
        LVITEMW item{};
        item.mask = LVIF_PARAM;
        item.iItem = change->iItem;
        if (ListView_GetItem(voiceList, &item))
        {
          applyVoiceNumber(int(item.lParam));
        }
      }
    }
    return 0;
  }
  if (msg == WM_LBUTTONDOWN)
  {
    SetFocus(voiceChannel);
    refreshVoiceWindow();
    return 0;
  }
  if (msg == WM_CTLCOLORSTATIC || msg == WM_CTLCOLOREDIT || msg == WM_CTLCOLORLISTBOX)
  {
    bool changed = false;
    SetTextColor(HDC(wp), changed ? RGB(255, 80, 80) : textColor);
    SetBkColor(HDC(wp), editColor);
    return LRESULT(editBrush);
  }
  if (msg == WM_PAINT)
  {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(hwnd, &ps);
    RECT client;
    GetClientRect(hwnd, &client);
    RECT area{client.right * 2 / 3, std::max(0L, client.bottom - scale(435)),
              client.right - scale(8), client.bottom - scale(255)};
    int saved = SaveDC(dc);
    IntersectClipRect(dc, area.left, area.top, area.right, area.bottom);
    FillRect(dc, &area, panelBrush);
    auto font = SelectObject(dc, voiceFont);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, textColor);
    std::wstring title =
        L"ALG " + std::to_wstring(shownAlgorithm) + L"   FB " + std::to_wstring(shownFeedback);
    TextOutW(dc, area.left + scale(8), area.top + scale(8), title.c_str(), int(title.size()));
    auto topology = mdx::algorithmLayout(shownAlgorithm);
    int bw = scale(28), bh = scale(23), margin = int(area.left) + scale(20),
        right = int(area.right) - scale(10);
    int columnWidth = std::max(bw + scale(6), (right - margin) / (topology.maxLevel + 2));
    POINT point[4];
    for (int op = 0; op < 4; ++op)
    {
      int usable = std::max(scale(80), int(area.bottom - area.top) - scale(40));
      point[op] = {margin + topology.level[op] * columnWidth,
                   int(area.top) + scale(32) +
                       (2 * topology.row[op] + 1) * usable / (2 * topology.rows[op]) - bh / 2};
    }
    auto pen = CreatePen(PS_SOLID, 2, RGB(92, 210, 255));
    auto old = SelectObject(dc, pen);
    auto arrow = [&](int x1, int y1, int x2, int y2)
    {
      int middle = (x1 + x2) / 2;
      MoveToEx(dc, x1, y1, nullptr);
      LineTo(dc, middle, y1);
      LineTo(dc, middle, y2);
      LineTo(dc, x2, y2);
      LineTo(dc, x2 - 5, y2 - 4);
      MoveToEx(dc, x2, y2, nullptr);
      LineTo(dc, x2 - 5, y2 + 4);
    };
    for (int i = 0; i < topology.edgeCount; ++i)
    {
      int a = topology.edges[i][0], b = topology.edges[i][1];
      arrow(point[a].x + bw, point[a].y + bh / 2, point[b].x, point[b].y + bh / 2);
    }
    int busX = margin + (topology.maxLevel + 1) * columnWidth;
    int minY = INT_MAX, maxY = 0;
    for (int op = 0; op < 4; ++op)
      if (topology.carriers & (1 << op))
      {
        int y = point[op].y + bh / 2;
        MoveToEx(dc, point[op].x + bw, y, nullptr);
        LineTo(dc, busX, y);
        minY = std::min(minY, y);
        maxY = std::max(maxY, y);
      }
    MoveToEx(dc, busX, minY, nullptr);
    LineTo(dc, busX, maxY);
    arrow(busX, (minY + maxY) / 2, right, (minY + maxY) / 2);
    if (shownFeedback)
    {
      int x = point[0].x, y = point[0].y;
      MoveToEx(dc, x + bw / 2, y, nullptr);
      LineTo(dc, x + bw / 2, y - 12);
      LineTo(dc, x - 14, y - 12);
      LineTo(dc, x - 14, y + bh / 2);
      LineTo(dc, x, y + bh / 2);
    }
    for (int op = 0; op < 4; ++op)
    {
      RECT box{point[op].x, point[op].y, point[op].x + bw, point[op].y + bh};
      FillRect(dc, &box, editBrush);
      FrameRect(dc, &box, accentBrush);
      auto name = L"OP" + std::to_wstring(op + 1);
      DrawTextW(dc, name.c_str(), -1, &box, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
    SelectObject(dc, old);
    DeleteObject(pen);
    SelectObject(dc, font);
    RestoreDC(dc, saved);
    EndPaint(hwnd, &ps);
    return 0;
  }
  if (msg == WM_EXITSIZEMOVE) { saveVoiceWindowSize(hwnd); return 0; }
  if (msg == WM_ACTIVATE && LOWORD(wp) == WA_INACTIVE)
    releaseVoiceKeys();
  if (msg == WM_CLOSE)
  {
    saveVoiceWindowSize(hwnd);
    WritePrivateProfileStringW(L"Settings", L"InstrumentOpen", L"0", settingsPath.c_str());
    releaseVoiceKeys();
    ShowWindow(hwnd, SW_HIDE);
    monitorView->waveforms.store(monitorWaveRequested);
    return 0;
  }
  if (msg == WM_DESTROY)
  {
    saveVoiceWindowSize(hwnd);
    releaseVoiceKeys();
    voiceKeyboard = nullptr;
    DeleteObject(parameterFont);
    DeleteObject(limitFont);
    parameterFont = limitFont = nullptr;
    DeleteObject(voiceFont);
    voiceFont = nullptr;
    voiceWindow = nullptr;
    return 0;
  }
  return DefWindowProcW(hwnd, msg, wp, lp);
}
void showVoices()
{
  if (voiceWindow && IsWindowVisible(voiceWindow)) {
    SendMessageW(voiceWindow, WM_CLOSE, 0, 0);
    return;
  }
  if (!voiceWindow)
  {
    WNDCLASSW cls{};
    cls.lpfnWndProc = voiceProcedure;
    cls.hInstance = GetModuleHandleW(nullptr);
    cls.lpszClassName = L"NOTE-MDX-Workbench.Instruments";
    cls.hbrBackground = panelBrush;
    cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    RegisterClassW(&cls);
    int width = scale(setting(L"InstrumentWidth", 1200, 700, 10000));
    int height = std::max(voiceMinimumHeight(), scale(setting(L"InstrumentHeight", 960, 400, 10000)));
    int x = setting(L"InstrumentX", CW_USEDEFAULT, -100000, 100000);
    int y = setting(L"InstrumentY", CW_USEDEFAULT, -100000, 100000);
    if (x != CW_USEDEFAULT && y != CW_USEDEFAULT) {
      RECT rect{x, y, x + width, y + height};
      MONITORINFO monitor{};
      monitor.cbSize = sizeof(monitor);
      if (GetMonitorInfoW(MonitorFromRect(&rect, MONITOR_DEFAULTTONEAREST), &monitor)) {
        // Keep the title bar accessible if a monitor was removed or rearranged.
        x = std::clamp(x, int(monitor.rcWork.left), std::max(int(monitor.rcWork.left), int(monitor.rcWork.right) - width));
        y = std::clamp(y, int(monitor.rcWork.top), std::max(int(monitor.rcWork.top), int(monitor.rcWork.bottom) - height));
      }
    } else { x = y = CW_USEDEFAULT; }
    voiceWindow = CreateWindowExW(WS_EX_TOPMOST, cls.lpszClassName, L"", WS_OVERLAPPEDWINDOW,
                                 x, y, width, height, nullptr, nullptr, cls.hInstance, nullptr);
  }
  updateAdvancedLanguage();
  monitorView->waveforms.store(true);
  SetWindowPos(voiceWindow, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
  ShowWindow(voiceWindow, SW_SHOW);
  saveRuntimeSettings();
  WritePrivateProfileStringW(L"Settings", L"InstrumentOpen", L"1", settingsPath.c_str());
  SetForegroundWindow(voiceWindow);
  refreshVoiceWindow();
}
void initializeViewer()
{
  panelBrush = CreateSolidBrush(panelColor);
  editBrush = CreateSolidBrush(editColor);
  accentBrush = CreateSolidBrush(RGB(92, 210, 255));
}
void shutdownViewer()
{
  if (voiceWindow != nullptr)
  {
    DestroyWindow(voiceWindow);
  }
  DeleteObject(panelBrush);
  DeleteObject(editBrush);
  DeleteObject(accentBrush);
  panelBrush = nullptr;
  editBrush = nullptr;
  accentBrush = nullptr;
}
} // namespace
