// SPDX-License-Identifier: 0BSD
// Copyright (C) 2026 by I.C.KaZe
#pragma once
#include "platform.h"
namespace gui
{
constexpr const wchar_t *version = L"2.10.0";
constexpr UINT analysisMessage = WM_APP + 76, selectionMessage = WM_APP + 72, outputMessage = WM_APP + 71;
constexpr wchar_t names[] = L"ABCDEFGHPQRSTUVW";
enum
{
    ID_MODE_FILE = 1001,
    ID_MODE_EDIT = 1002,
    ID_SRC_EDIT = 1010,
    ID_SRC_BROWSE = 1011,
    ID_PDX_EDIT = 1012,
    ID_PDX_BROWSE = 1013,
    ID_WATCH = 1014,
    ID_CONVERT = 1015,
    ID_MML_SAVE = 1016,
    ID_EDITOR = 1020,
    ID_LINES = 1021,
    ID_LOG = 1030,
    ID_PLAY = 1040,
    ID_PAUSE = 1041,
    ID_STOP = 1042,
    ID_EXPORT_MIX = 1043,
    ID_EXPORT_TRACKS = 1044,
    ID_SEEK = 1045,
    ID_TIME = 1046,
    ID_EDITOR_NAME = 1047,
    ID_MASTER_VOL = 1048,
    ID_MASTER_LABEL = 1049,
    ID_PCM_VOL = 1050,
    ID_PCM_LABEL = 1051,
    ID_WAV_LOOPS_EDIT = 1053,
    ID_FADE_MS_EDIT = 1054,
    ID_STEP_STATUS_BASE = 1060,
    ID_SELECTION_STATUS = 1076,
    ID_TRACK_BASE = 1100,
    idLiveOutput = 4100,
    idLiveVoices = 4102,
    idPDXEdit = 4104,
    idLiveMIDI = 4103,
    idMeasured = 4110,
    idDAC,
    idFMDelay,
    idPCMDelay,
    idKey,
    idKeyMode,
    idSpeed,
    idFMGain,
    idPoly,
    idAftertouch,
    idLanguage = 4200,
    idMonitorRoll = 4200,
    idMonitorCarrier = 4201,
    idWaves = 4202,
    idEditorFollow = 4203
};
struct App
{
    HWND hwnd{}, modeFile{}, modeEdit{}, srcEdit{}, pdxEdit{}, watchCheck{}, convertBtn{}, editor{}, lines{},
        log{}, playBtn{}, pauseBtn{}, stopBtn{};
    HWND exportMixBtn{}, exportTracksBtn{}, seek{}, timeLabel{}, editorName{}, masterSlider{}, masterLabel{},
        pcmSlider{}, pcmLabel{}, wavLoopsEdit{}, fadeMsEdit{};
    HWND mmdspPanel{}, splitterHwnd{}, selectionLabel{}, languageControl{}, outputControl{}, midiControl{},
        pdxButton{};
    HWND measured{}, dac{}, fmDelay{}, pcmDelay{}, key{}, keyValue{}, keyMode{}, speed{}, speedValue{},
        fmGain{}, fmValue{}, poly{}, aftertouch{};
    HWND rollButton{}, carrierButton{}, waveButton{}, followControl{}, scroll{}, monitorSplitter{};
    std::array<HWND, 16> trackChecks{}, stepLabels{};
    HBRUSH bg{}, panel{}, edit{}, accent{};
    HFONT font{}, gutterFont{};
    int fontSize = 9, splitterPos = 470, splitterActual = 470, monitorHeight = 260, monitorTop = 0, pitchHeight = 18,
        pitchScroll = 0, follow = 0;
    std::wstring fontName = L"MS Gothic";
    bool smoke = false;
    std::deque<int> dialogAnswers;
    std::deque<Path> fileChoices;
    std::vector<std::pair<std::wstring, std::wstring>> dialogs;
    bool english = false, restoring = true, settingsLoaded = false, roll = false, carrier = false,
         waves = true, gutterLengths = false, centered = false;
    bool splitDrag = false, monitorDrag = false, ready = false, closing = false, seeking = false,
         cursorsValid = false, decorationDirty = true;
    int master = 100, pcm = 100, loops = 2, fade = 2000;
    std::uint64_t revision = 0, checkedRevision = UINT64_MAX, compiledRevision = UINT64_MAX,
                  selectionRevision = UINT64_MAX, selectionGeneration = 0;
    std::uint64_t savedAt = 0, autosaveAt = 0;
    DWORD selectionBegin = 0, selectionEnd = 0;
    Path directory, fullPath, selectionPath, sourcePath, checkedPath, referencedPath;
    std::wstring compiledText, sourceText, checkedText, playingText, referencedText;
    Core core;
    NativeState snapshot{};
    Json full, selection, checkedDocument, score;
    std::map<int, workbench::TrackSteps> lineSteps;
    std::vector<std::wstring> gutterRows;
    std::vector<workbench::EditorErrorSpan> errors;
    std::map<uint64_t, Json> cursors;
    std::array<uint64_t, 16> paintedOffsets{};
    bool paintedActive = false;
    workbench::EditorCheckGate gate;
    std::array<std::thread, 2> receivers;
    std::mutex resultsMutex, logMutex;
    std::array<std::optional<Json>, 2> results;
    std::vector<std::wstring> pendingLog;
    std::thread outputThread;
    bool outputBusy = false, logOutput = false;
    HWND activeButton{};
    int progress = 0;
    std::vector<Path> completed;
    std::wstring outputError;
    std::map<HWND, std::wstring> captions;
    Json settings = Json::object();
    HWND lastFocus{};
    struct Moment
    {
        double time;
        std::array<std::array<int, 4>, 16> notes;
    };
    std::deque<Moment> history;
    double clock = 0;
    uint64_t wall = 0, scoreRevision = UINT64_MAX;
};
extern App app;
inline LRESULT send(HWND w, UINT m, WPARAM a = 0, LPARAM b = 0)
{
    return SendMessageW(w, m, a, b);
}
inline bool checked(HWND w)
{
    return send(w, BM_GETCHECK) == BST_CHECKED;
}
inline void setChecked(HWND w, bool b)
{
    if (checked(w) != b)
    {
        send(w, BM_SETCHECK,
             [&]() -> WPARAM
             {
                 if (b)
                 {
                     return BST_CHECKED;
                 }
                 return BST_UNCHECKED;
             }());
    }
}
std::wstring text(HWND);
void setText(HWND, const std::wstring &);
std::wstring translate(const std::wstring &);
HWND create(const wchar_t *, const std::wstring &, DWORD, int, int, int, int, HWND, int);
void logLine(const std::wstring &);
inline void logError(const std::exception &e)
{
    logLine(wide(e.what()));
}
int dialog(const std::wstring &, const std::wstring &, UINT);
void saveSettings();
void loadSettings();
void applyFont();
void autosave();
Path settingsPath();
Path autosavePath();
void updateMode();
void layout(int, int);
void createControls();
void initCore();
void shutdownCore();
void tick();
void invalidate();
void queueCheck();
void receiveAnalysis(unsigned);
void refreshSelection(bool = false);
void applyDocument(const Path &, const std::wstring &, uint64_t, const Json &, const Json &);
std::pair<Path, std::wstring> source();
bool compileFull(bool = false);
void play();
void stop();
void updateStatistics();
void paintDecoration();
void followPlayback(bool = false);
void startExport(int);
void cancelExport();
void finishExport();
void updateProgress();
bool confirmOverwrite(const std::vector<Path> &);
void convertMdx(bool = false);
void browseSource();
bool saveSource(bool);
void rememberSource();
void launchPdx();
void pollVoice();
void refreshMidi();
void changeLanguage(bool);
void syncControls();
void commandControl(int, int);
void slider(HWND, int);
LRESULT CALLBACK windowProc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK monitorProc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK editorProc(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);
LRESULT CALLBACK gutterProc(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);
LRESULT CALLBACK sliderProc(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);
LRESULT CALLBACK buttonProc(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);
LRESULT CALLBACK silentProc(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);
void monitorLayout();
void paintMonitor(HDC, int, int);
void captureHistory();
void paintBuffered(HWND, bool);
void syncGutter();
HFONT gutterFont(HDC);
void drawText(HDC, int, int, const std::wstring &, COLORREF);
void fill(HDC, int, int, int, int, COLORREF);
int runSmoke(bool);
} // namespace gui

