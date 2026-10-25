// SPDX-License-Identifier: 0BSD
// Copyright (C) 2026 by I.C.KaZe
// Playback service extracted from IndependentMDX. No GUI controls are created.
#pragma once
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "c86_output.h"
#include "live_session.h"
#include "resume_position.h"
#include "player.h"
#include "win_audio.h"
#include "notemdx/compiler.hpp"
#include <commctrl.h>
#include <commdlg.h>
#include <fstream>
#include <sstream>
#include <thread>
#include <condition_variable>
#include <windowsx.h>

namespace
{
HWND window = nullptr;
HMIDIIN midiInput = nullptr;
int activeMidiDevice = -1;
mdx::C86Output hardware;
std::atomic<int> selectedOutput{0};
std::atomic<bool> mmlCatalogLoaded{false};
auto retainedVoices = std::make_shared<mdx::VoiceState>();
auto midiQueue = std::make_shared<mdx::MidiQueue>();
bool monitorWaveRequested=true;
auto monitorView = std::make_shared<mdx::Monitor>();
auto liveMidiPolyphonic = std::make_shared<std::atomic<bool>>(true);
auto liveAftertouch = std::make_shared<std::atomic<bool>>(false);
auto liveGain = std::make_shared<std::atomic<double>>(1);
auto livePcmGain = std::make_shared<std::atomic<double>>(1);
auto liveIndependentGain = std::make_shared<std::atomic<double>>(2);
auto liveMeasuredOutputTiming = std::make_shared<std::atomic<bool>>(true);
auto liveDacRightDelay = std::make_shared<std::atomic<bool>>(false);
auto liveMuteMask = std::make_shared<std::atomic<uint32_t>>(0);
auto livePcmMuteMask = std::make_shared<std::atomic<uint32_t>>(0);
auto liveKeyMode = std::make_shared<std::atomic<int>>(1);
auto liveKeyShift = std::make_shared<std::atomic<int>>(0);
auto liveSpeed = std::make_shared<std::atomic<int>>(100);
auto liveLoops = std::make_shared<std::atomic<int>>(2);
std::atomic<double> seekRequest{-1};
std::atomic<double> songPosition{0};
std::atomic<double> retainedResumePosition{0};
std::atomic<bool> playbackPreparing{false};
std::atomic<double> songDuration{0};
std::atomic<double> playbackSeconds{0};
std::atomic<bool> stopFlag{false};
std::atomic<bool> pauseFlag{false};
std::atomic<bool> running{false};
std::atomic<bool> idleMidi{false};
std::atomic<bool> japanese{false};
std::thread worker;
std::mutex messageMutex;
std::wstring message;
std::filesystem::path input;
std::filesystem::path explicitPdx;
std::filesystem::path bankSearchOverride;
std::filesystem::path lastPlayedFile;
std::filesystem::path lastPlayedBank;
struct WatchedPdx {
 std::filesystem::path path;
 std::filesystem::file_time_type modified{};
 uintmax_t size=0;
};
std::mutex watchedPdxMutex;
WatchedPdx watchedPdx;
void watchPlaybackPdx(const std::shared_ptr<mdx::Pdx>& bank) {
 WatchedPdx next;
 if(bank && !bank->sourcePath.empty()) {
  std::error_code error;
  next.path=std::filesystem::absolute(bank->sourcePath,error);
  if(!error) next.modified=std::filesystem::last_write_time(next.path,error);
  if(!error) next.size=std::filesystem::file_size(next.path,error);
  if(error) next.path.clear();
 }
 std::lock_guard<std::mutex> lock(watchedPdxMutex);watchedPdx=std::move(next);
}
std::mutex scoreMutex;
mdx::Timeline scoreTimeline;
std::atomic<uint64_t> scoreRevision{0};
int fmDelayMs = 0;
int pcmDelayMs = 0;
int playbackLoops = 2;
bool infinitePlayback = false;
bool selectionPlaying = false;
struct OutputGains
{
    int fm = 200;
    int pcm = 100;
    int master = 100;
};
std::array<OutputGains, 3> outputGains{{{200, 100, 100}, {200, 100, 100}, {100, 100, 100}}};
std::filesystem::path executableFolder()
{
    wchar_t path[32768]{};
    GetModuleFileNameW(nullptr, path, 32768);
    return std::filesystem::path(path).parent_path();
}
std::filesystem::path settingsPath = executableFolder() / L"NOTE-MDX-Workbench.ini";
void settingsChanged();
void startMidiIdle();
void closeMidi();
const wchar_t *tr(const wchar_t *ja, const wchar_t *en) { return japanese ? ja : en; }
std::wstring wide(const std::string &text) {
    int count = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring result(count, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), count);
    return result;
}
const std::pair<const wchar_t *, const wchar_t *> translations[] = {
    {L"value=", L"参照値="},
    {L"expected=", L"対応範囲="},
    {L"defined voice IDs", L"定義済み音色番号"},
    {L"PCM is silent while FM continues", L"PCMを消音してFMの再生を続行します"},
    {L"Numeric field is outside its allowed range", L"数値が入力可能な範囲外です"},
    {L"WAV output must not overwrite the input file", L"入力ファイルをWAVで上書きすることはできません"},
    {L"Cannot create audio event", L"音声イベントを作成できません"},
    {L"Windows audio device could not be opened", L"Windowsの音声デバイスを開けません"},
    {L"Audio buffer preparation failed", L"音声バッファーの準備に失敗しました"},
    {L"Audio buffer submission failed", L"音声バッファーの送信に失敗しました"},
    {L"Truncated file", L"ファイルが途中で切れています"},
    {L"Cannot open: ", L"ファイルを開けません: "},
    {L"File size must be 1..128 MiB", L"ファイルサイズは1バイト以上128 MiB以下にしてください"},
    {L"File read failed", L"ファイルの読み込みに失敗しました"},
    {L"Not an MDX: title terminator missing", L"MDXのタイトル終端がありません"},
    {L"PDX filename terminator missing", L"PDXファイル名の終端がありません"},
    {L"Unsupported MDX track table (expected 9 or 16 compact tracks)",
     L"未対応のMDXトラック表です（9または16トラックが必要です）"},
    {L"Invalid MDX track offset", L"MDXトラックの位置が不正です"},
    {L"Invalid voice offset", L"音色データの位置が不正です"},
    {L"PDX header is shorter than 96 entries", L"PDXヘッダーが96項目に満たない長さです"},
    {L"Invalid PDX sample offset/length (compressed PDX is unsupported)",
     L"PDXのサンプル位置／長さが不正です（圧縮PDXは未対応）"},
    {L"PDX sample/bank does not exist", L"PDXのサンプル／バンクが存在しません"},
    {L"PCM format must be F0..F6", L"PCM形式はF0〜F6を指定してください"},
    {L"P16 sample has odd byte length", L"P16サンプルのバイト数が奇数です"},
    {L"Ambiguous PDX filename; specify --pdx / Choose PDX",
     L"PDX候補が複数あります。「PDXを指定」で選択してください"},
    {L"Multiple numbered PDX copies found; specify --pdx / Choose PDX",
     L"番号付きPDXが複数あります。「PDXを指定」で選択してください"},
    {L"Required PDX not found: ", L"必要なPDXが見つかりません: "},
    {L". Place it beside the MDX or specify --pdx.", L"。MDXの隣に置くか「PDXを指定」で選択してください。"},
    {L"Invalid sample playback options", L"サンプル再生設定が不正です"},
    {L"Cannot create WAV output", L"WAV出力ファイルを作成できません"},
    {L"WAV exceeds RIFF size limit", L"WAVがRIFF形式のサイズ上限を超えています"},
    {L"WAV write failed", L"WAVの書き込みに失敗しました"},
    {L"WAV finalization failed", L"WAVの保存終了処理に失敗しました"},
    {L"Invalid playback options", L"再生設定が不正です"},
    {L"Software LFO uses an independent approximation; not verified against MXDRV integer behavior.",
     L"ソフトLFOは独自近似です。MXDRVの整数演算との一致は未検証です。"},
    {L"F5/F6 linear PCM extension: interpreted as big-endian P16 / signed P8 at 15625 Hz.",
     L"F5/F6拡張PCMは15625 HzのビッグエンディアンP16／符号付きP8として再生します。"},
    {L"Embedded fade speed is approximated; use the player's fade option for a defined duration.",
     L"埋め込みフェード速度は近似です。正確な時間はプレーヤーのフェード設定を使用してください。"},
    {L"All active MDX tracks are waiting for sync; playback cannot continue",
     L"全MDXトラックが同期待ちになったため、再生を続行できません"},
    {L"Unexpected end of MDX command stream", L"MDX命令が途中で終了しています"},
    {L"MDX jump outside sequence data", L"MDXのジャンプ先がシーケンス範囲外です"},
    {L"Referenced FM voice is missing", L"指定されたFM音色がありません"},
    {L"PCM note without a PDX sample bank", L"PCM音符に必要なPDXがありません"},
    {L"Decoded PCM cache exceeds 128 MiB", L"復号PCMキャッシュが128 MiBを超えています"},
    {L"Unsupported software LFO waveform", L"未対応のソフトLFO波形です"},
    {L"Unsupported extension inside repeat escape", L"リピート脱出内に未対応の拡張命令があります"},
    {L"Unterminated or malformed repeat escape", L"リピート脱出の終端がないか不正です"},
    {L"Repeat escape scan limit exceeded", L"リピート脱出の探索上限を超えました"},
    {L"Unsupported coarse volume outside v0..v15", L"粗音量はv0〜v15のみ対応します"},
    {L"Invalid q value", L"q値が不正です"},
    {L"Invalid repeat count or excessive repeat nesting", L"リピート回数が不正か、入れ子が深すぎます"},
    {L"Repeat end without repeat start", L"リピート開始なしに終端があります"},
    {L"Repeat end requires a backward offset", L"リピート終端には後方オフセットが必要です"},
    {L"Repeat escape without repeat start", L"リピート開始なしに脱出があります"},
    {L"Repeat escape requires a forward offset", L"リピート脱出には前方オフセットが必要です"},
    {L"End-loop pointer must be negative", L"終端ループのポインターは負数である必要があります"},
    {L"Sync channel is outside the track table", L"同期先チャンネルがトラック表の範囲外です"},
    {L"Unsupported PCM frequency/format (F0..F6 supported)", L"未対応のPCM周波数／形式です（F0〜F6に対応）"},
    {L"Invalid hardware LFO switch", L"ハードLFO切り替え値が不正です"},
    {L"PCM8 declaration requires a 16-track header", L"PCM8宣言には16トラックのヘッダーが必要です"},
    {L"Unsupported E7 extension ", L"未対応のE7拡張命令: "},
    {L"Unsupported E6 extension ", L"未対応のE6拡張命令: "},
    {L"Unsupported MDX opcode ", L"未対応のMDX命令: "},
    {L"Too many zero-time commands (possible cyclic/corrupt sequence)",
     L"時間を進めない命令が多すぎます（循環または破損の可能性）"},
    {L" (channel ", L"（チャンネル "},
    {L", file offset 0x", L"、ファイル位置 0x"},
    {L"ERROR: ", L"エラー: "},
    {L"Exporting WAV...", L"WAVを書き出しています…"},
    {L"Exporting WAV", L"WAV保存中"},
    {L"WAV saved", L"WAV保存完了"},
    {L"Playing", L"再生中"},
    {L"Stopped", L"停止しました"},
    {L"Finished", L"再生終了"},
    {L"Ready", L"準備完了"},
};
std::wstring translated(std::wstring text, bool toJapanese) {
    for (const auto &pair : translations) {
        std::wstring from = toJapanese ? pair.first : pair.second;
        const wchar_t *to = toJapanese ? pair.second : pair.first;
        size_t pos = 0;
        while ((pos = text.find(from, pos)) != std::wstring::npos) {
            text.replace(pos, from.size(), to);
            pos += wcslen(to);
        }
    }
    return text;
}
std::wstring diagnostic(const std::string &text) {
    return japanese ? translated(wide(text), true) : wide(text);
}
void status(std::wstring text) {
    std::lock_guard<std::mutex> lock(messageMutex);
    message = translated(std::move(text), false);
}
void stop() {
    stopFlag = true;
    if (worker.joinable())
        worker.join();
    running  = false;
    playbackPreparing = false;
    idleMidi = false;
    midiQueue->clear();
    try {
        hardware.reset();
    } catch (const std::exception &e) {
        status(wide(e.what()));
    }
    pauseFlag = false;
    std::lock_guard<std::mutex> lock(monitorView->mutex);
    monitorView->frame = mdx::MonitorFrame{};
}
bool isPdx(const std::filesystem::path& file)
{
    return (_wcsicmp(file.extension().c_str(), L".pdx") == 0);
}
bool load(const std::filesystem::path& file)
{
    if (file.empty())
    {
        throw std::runtime_error("No playback file supplied.");
    }
    stop();
    bankSearchOverride.clear();
    if (!isPdx(file))
    {
        mdx::Song song(mdx::readFile(file));
        std::lock_guard<std::mutex> lock(retainedVoices->mutex);
        if(!mmlCatalogLoaded.load()) {
            retainedVoices->loadDefinitions(song.voices, song.hasVoice);
        }
        retainedVoices->used.fill(false);
        ++retainedVoices->catalogRevision;
        for (auto& request : retainedVoices->requestedVoice)
        {
            request = -1;
        }
    }
    input = file;
    explicitPdx.clear();
    playbackSeconds = 0;
    return true;
}
// UTF-8 CSV: one physical line per error, appended across sessions.
void logPlaybackError(const std::filesystem::path& file, const std::string& title, const std::string& error) noexcept
{
    if
    (
           (error.find("Required PDX not found:") != std::string::npos)
        || (error.find("PCM note without a PDX sample bank") != std::string::npos)
    )
    {
        return;
    }
    try
    {
        static std::mutex logMutex;
        std::lock_guard<std::mutex> lock(logMutex);
        const auto field = [](const std::string& text)
        {
            std::string escaped = "\"";
            for (const char character : text)
            {
                if ((character == '\r') || (character == '\n'))
                {
                    escaped += ' ';
                }
                else
                {
                    if (character == '"')
                    {
                        escaped += '"';
                    }
                    escaped += character;
                }
            }
            return escaped + '"';
        };
        std::ofstream log(executableFolder() / L"NOTE-MDX-Workbench-errors.csv", std::ios::binary | std::ios::app);
        log << field(std::filesystem::absolute(file).u8string()) << ',' << field(title) << ',' << field(error) << "\r\n";
    }
    catch (...)
    {
    }
}
mdx::Options playbackOptions()
{
    mdx::Options options;
    options.voiceState = retainedVoices;
    options.fastSeek   = true;
    if (selectedOutput.load() == 1)
    {
        options.fmBackend = 1;
    }
    options.externalFm = hardware.active();
    if (options.externalFm)
    {
        options.registerOutput = [](uint64_t time, int address, int value)
        {
            int channel = -1;
            if (address == 8)
            {
                channel = value & 7;
            }
            else
            if (address >= 0x20)
            {
                channel = address & 7;
            }
            if ((channel >= 0) && (retainedVoices->midiFmOverride.load() & (1u << channel)))
            {
                return;
            }
            hardware.scheduled(time, address, value, stopFlag);
        };
    }
    options.monitor                  = monitorView;
    options.keyShift                 = liveKeyShift->load();
    options.keyMode                  = liveKeyMode->load();
    options.measuredOutputTiming     = liveMeasuredOutputTiming->load();
    options.dacRightDelay            = liveDacRightDelay->load();
    options.fmMuteMask               = liveMuteMask->load();
    options.pcmMuteMask              = livePcmMuteMask->load();
    options.independentGain          = liveIndependentGain->load();
    options.gain                     = liveGain->load();
    options.pcmGain                  = livePcmGain->load();
    options.fmDelayMs                = fmDelayMs;
    options.pcmDelayMs               = pcmDelayMs;
    options.liveKeyShift             = liveKeyShift;
    options.liveKeyMode              = liveKeyMode;
    options.liveSpeed                = liveSpeed;
    options.liveMeasuredOutputTiming = liveMeasuredOutputTiming;
    options.liveDacRightDelay        = liveDacRightDelay;
    options.liveMuteMask             = liveMuteMask;
    options.livePcmMuteMask          = livePcmMuteMask;
    options.liveIndependentGain      = liveIndependentGain;
    options.liveGain                 = liveGain;
    options.livePcmGain              = livePcmGain;
    options.liveMidiPolyphonic       = liveMidiPolyphonic;
    options.liveAftertouch           = liveAftertouch;
    options.maxSeconds               = 0;
    hardware.setDelay(options.fmDelayMs);
    return options;
}
void start(double resumePosition = 0, bool resumePaused = false, std::shared_ptr<mdx::Pdx> updatedPdx = {})
{
    if (input.empty())
    {
        throw std::runtime_error("No playback file supplied.");
    }
    stop();
    watchPlaybackPdx({});
    auto options = playbackOptions();
    // Interactive playback is infinite; the WAV loop count is export-only.
    options.loops = 0;
    if (selectionPlaying)
    {
        options.loops = 1;
        options.repeatSelection = true;
    }
    liveLoops->store(options.loops);
    options.liveLoops = liveLoops;
    const auto file = input;
    const auto bankPath = explicitPdx;
    auto bankSearchFile = bankSearchOverride;
    if (bankSearchFile.empty())
    {
        bankSearchFile = file;
    }
    options.diagnostic = [file](const std::string& note)
    {
        if (note.rfind("ERROR:", 0) == 0)
        {
            logPlaybackError(file, "", note);
            status(wide(note));
        }
    };
    lastPlayedFile = file;
    lastPlayedBank = bankPath;
    retainedResumePosition = resumePosition;
    playbackPreparing = true;
    songPosition   = resumePosition;
    songDuration   = 0;
    seekRequest    = -1;
    stopFlag       = false;
    pauseFlag      = resumePaused;
    running        = true;
    status(tr(L"再生中", L"Playing"));
    try
    {
        worker = std::thread([file, bankPath, bankSearchFile, options, resumePosition, resumePaused, updatedPdx]() mutable
        {
            try
            {
                std::unique_ptr<mdx::Source> source;
                mdx::Player* music = nullptr;
                if (isPdx(file))
                {
                    mdx::Pdx bank(mdx::readFile(file));
                    source = std::make_unique<mdx::SamplePlayer>(bank, 0, 0, options);
                }
                else
                {
                    mdx::Song song(mdx::readFile(file));
                    std::shared_ptr<mdx::Pdx> bank;
                    try
                    {
                        bank = updatedPdx ? updatedPdx : mdx::locatePdx(song, bankSearchFile, bankPath);
                        watchPlaybackPdx(bank);
                    }
                    catch (const std::exception& error)
                    {
                        options.diagnostic(std::string("ERROR: ") + error.what());
                    }
                    auto player = std::make_unique<mdx::Player>(std::move(song), bank, options);
                    music  = player.get();
                    source = std::move(player);
                }
                mdx::Timeline timeline;
                if (music != nullptr)
                {
                    timeline = music->analyze([] { return stopFlag.load(); }, true);
                }
                {
                    std::lock_guard<std::mutex> lock(scoreMutex);
                    scoreTimeline = timeline;
                    scoreRevision.fetch_add(1);
                }
                if (timeline.truncated)
                {
                    options.diagnostic("ERROR: Timeline analysis reached 3600 seconds; displayed duration is a lower bound");
                }
                if (!stopFlag.load())
                {
                    mdx::LiveSession session(
                        source.get(), music, timeline, retainedVoices, midiQueue, options, pauseFlag,
                        seekRequest, songPosition, songDuration, [] { return stopFlag.load(); },
                        [] { hardware.reset(); }, [](int address, int value) { hardware.write(address, value); });
                    if (resumePosition > 0)
                    {
                        if (seekRequest.load() < 0) seekRequest = resumePosition;
                        session.service();
                    }
                    if (pauseFlag.load() || seekRequest.load() >= 0) session.service();
                    if (!stopFlag.load()) playbackPreparing = false;
                    mdx::playWindows(session, options.sampleRate, [] { return stopFlag.load(); }, [] { return false; });
                }
                std::wstring result = tr(L"再生終了", L"Finished");
                if (stopFlag.load())
                {
                    result = tr(L"停止しました", L"Stopped");
                }
                if (music != nullptr)
                {
                    for (const auto& note : music->warnings())
                    {
                        if (note.rfind("ERROR:", 0) != 0 &&
                            note != "Software LFO uses an independent approximation; not verified against MXDRV integer behavior.")
                        {
                            result += L"\r\n" + wide(note);
                        }
                    }
                }
                status(result);
            }
            catch (const std::exception& error)
            {
                logPlaybackError(file, "", error.what());
                status(L"ERROR: " + wide(error.what()));
            }
            try
            {
                hardware.reset();
            }
            catch (const std::exception& error)
            {
                status(L"ERROR: " + wide(error.what()));
            }
            playbackPreparing = false;
            running = false;
        });
    }
    catch (...)
    {
        playbackPreparing = false;
        running = false;
        throw;
    }
}
void closeMidi()
{
    if (midiInput != nullptr)
    {
        midiInStop(midiInput);
        midiInReset(midiInput);
        midiInClose(midiInput);
        midiInput        = nullptr;
        activeMidiDevice = -1;
    }
    for (int channel = 0; channel < 16; ++channel)
    {
        midiQueue->push(0xb0 | channel | (120 << 8));
    }
}
// Poll the resolved bank actually loaded by the player, including case-insensitive lookup.
void pollPlaybackPdx() {
 static ULONGLONG lastPoll=0;
 const auto now=GetTickCount64();
 if(now-lastPoll<500) return;
 lastPoll=now;
 if(!running || idleMidi || playbackPreparing) return;
 WatchedPdx current;
 {std::lock_guard<std::mutex> lock(watchedPdxMutex);current=watchedPdx;}
 if(current.path.empty()) return;
 std::error_code error;
 const auto changed=std::filesystem::last_write_time(current.path,error);
 if(error) return;
 const auto size=std::filesystem::file_size(current.path,error);
 if(error || (changed==current.modified && size==current.size)) return;
 try {
  // A partially written/replaced bank must not interrupt the current player.
  auto validated=std::make_shared<mdx::Pdx>(mdx::readFile(current.path));
  validated->sourcePath=current.path;
  if(std::filesystem::last_write_time(current.path)!=changed || std::filesystem::file_size(current.path)!=size) return;
  const bool paused=pauseFlag.load();
  const double position=mdx::resumePosition(playbackPreparing.load(),retainedResumePosition.load(),songPosition.load(),seekRequest.load());
  start(position,paused,std::move(validated));
 } catch(const std::exception&) {
  // Retry after the editor finishes writing; retain the old decoded bank until then.
 }
}

void startMidiIdle()
{
    if (running.load())
    {
        return;
    }
    if (worker.joinable())
    {
        worker.join();
    }
    auto options = playbackOptions();
    stopFlag  = false;
    pauseFlag = false;
    running   = true;
    idleMidi  = true;
    try
    {
        worker = std::thread([options]
        {
            try
            {
                mdx::LiveSession session(
                    nullptr, nullptr, {}, retainedVoices, midiQueue, options, pauseFlag, seekRequest,
                    songPosition, songDuration, [] { return stopFlag.load(); }, [] { hardware.reset(); },
                    [](int address, int value) { hardware.write(address, value); });
                mdx::playWindows(session, options.sampleRate, [] { return stopFlag.load(); }, [] { return false; });
            }
            catch (const std::exception& error)
            {
                status(L"ERROR: " + wide(error.what()));
            }
            try
            {
                hardware.reset();
            }
            catch (const std::exception& error)
            {
                status(L"ERROR: " + wide(error.what()));
            }
            running  = false;
            idleMidi = false;
        });
    }
    catch (...)
    {
        running  = false;
        idleMidi = false;
        throw;
    }
}
void captureOutputGains()
{
    auto& values = outputGains[std::clamp(selectedOutput.load(), 0, 2)];
    values.fm     = int(std::lround(liveIndependentGain->load() * 100));
    values.pcm    = int(std::lround(livePcmGain->load() * 100));
    values.master = int(std::lround(liveGain->load() * 100));
}
void applyOutputGains()
{
    const auto& values = outputGains[std::clamp(selectedOutput.load(), 0, 2)];
    liveIndependentGain->store(values.fm / 100.0);
    livePcmGain->store(values.pcm / 100.0);
    liveGain->store(values.master / 100.0);
    hardware.setGain(liveGain->load(), liveIndependentGain->load());
}
std::wstring settingText(const wchar_t* name)
{
    wchar_t text[32768]{};
    GetPrivateProfileStringW(L"Settings", name, L"", text, 32768, settingsPath.c_str());
    return text;
}
int setting(const wchar_t* name, int fallback, int low, int high)
{
    const auto text = settingText(name);
    wchar_t* end = nullptr;
    const auto value = wcstoll(text.c_str(), &end, 10);
    if ((end == text.c_str()) || (*end != 0) || (value < low) || (value > high))
    {
        return fallback;
    }
    return int(value);
}
void settingsChanged()
{
    if (window != nullptr)
    {
        SetTimer(window, 2, 500, nullptr);
    }
}
void saveRuntimeSettings()
{
    captureOutputGains();
    std::error_code fileError;
    if (!std::filesystem::exists(settingsPath, fileError))
    {
        std::ofstream output(settingsPath, std::ios::binary);
        const char bom[] = {char(255), char(254)};
        output.write(bom, 2);
    }
    const auto number = [](const wchar_t* key, int value)
    {
        if (!WritePrivateProfileStringW(L"Settings", key, std::to_wstring(value).c_str(), settingsPath.c_str()))
        {
            status(tr(L"設定ファイルを保存できません。", L"Cannot save settings."));
        }
    };
    for (int index = 0; index < 3; ++index)
    {
        const auto key = L"Output" + std::to_wstring(index);
        number((key + L"FmGain").c_str(), outputGains[index].fm);
        number((key + L"PcmGain").c_str(), outputGains[index].pcm);
        number((key + L"MasterGain").c_str(), outputGains[index].master);
    }
    number(L"OutputBackend", selectedOutput.load());
    number(L"Measured", liveMeasuredOutputTiming->load());
    number(L"Dac", liveDacRightDelay->load());
    number(L"FmDelayMs", fmDelayMs);
    number(L"PcmDelayMs", pcmDelayMs);
    number(L"KeyShift", liveKeyShift->load());
    number(L"KeyMode", liveKeyMode->load());
    number(L"PlaybackSpeed", liveSpeed->load());
    number(L"MidiPolyphonic", liveMidiPolyphonic->load());
    number(L"Aftertouch", liveAftertouch->load());
    number(L"FmMask", liveMuteMask->load());
    number(L"PcmMask", livePcmMuteMask->load());
    number(L"Loops", playbackLoops);
    number(L"Infinite", infinitePlayback);
    MIDIINCAPSW capabilities{};
    std::wstring midiName;
    if ((activeMidiDevice >= 0) && (midiInGetDevCapsW(activeMidiDevice, &capabilities, sizeof(capabilities)) == MMSYSERR_NOERROR))
    {
        midiName = capabilities.szPname;
    }
    WritePrivateProfileStringW(L"Settings", L"MidiInputName", midiName.c_str(), settingsPath.c_str());
    WritePrivateProfileStringW(nullptr, nullptr, nullptr, settingsPath.c_str());
}
void selectMidi(int device)
{
    if ((device == activeMidiDevice) && (midiInput != nullptr))
    {
        return;
    }
    if (device < 0)
    {
        closeMidi();
        return;
    }
    HMIDIIN next = nullptr;
    auto error = midiInOpen(&next, UINT(device), DWORD_PTR(window), 0, CALLBACK_WINDOW);
    if (error == MMSYSERR_NOERROR)
    {
        error = midiInStart(next);
    }
    if (error != MMSYSERR_NOERROR)
    {
        if (next != nullptr)
        {
            midiInReset(next);
            midiInClose(next);
        }
        throw std::runtime_error("Cannot open MIDI input (WinMM error " + std::to_string(error) + ").");
    }
    closeMidi();
    midiInput        = next;
    activeMidiDevice = device;
}
void selectOutput(int output)
{
    if ((output < 0) || (output > 2))
    {
        throw std::runtime_error("Invalid output backend.");
    }
    if (output == selectedOutput.load())
    {
        return;
    }
    const bool resume = running.load() && !idleMidi.load();
    const bool resumeIdle = running.load() && idleMidi.load();
    const bool paused = pauseFlag.load();
    const double position = mdx::resumePosition(playbackPreparing.load(), retainedResumePosition.load(), songPosition.load(), seekRequest.load());
    captureOutputGains();
    stop();
    hardware.close();
    selectedOutput.store(output);
    try
    {
        if (output == 2)
        {
            hardware.open(executableFolder() / L"c86ctl.dll");
            hardware.setChannelOverride(&retainedVoices->midiFmOverride, liveMuteMask.get());
        }
    }
    catch (const std::exception& error)
    {
        selectedOutput.store(0);
        status(L"ERROR: " + wide(error.what()));
    }
    applyOutputGains();
    if (resume)
    {
        input       = lastPlayedFile;
        explicitPdx = lastPlayedBank;
        start(position, paused);
    }
    else
    if (resumeIdle)
    {
        startMidiIdle();
    }
}
void readRuntimeSettings()
{
    for (int index = 0; index < 3; ++index)
    {
        const auto key = L"Output" + std::to_wstring(index);
        auto& values = outputGains[index];
        values.fm     = setting((key + L"FmGain").c_str(), values.fm, 0, 400);
        values.pcm    = setting((key + L"PcmGain").c_str(), 100, 0, 400);
        values.master = setting((key + L"MasterGain").c_str(), 100, 0, 400);
    }
    liveMeasuredOutputTiming->store(setting(L"Measured", 1, 0, 1));
    liveDacRightDelay->store(setting(L"Dac", 0, 0, 1));
    fmDelayMs  = setting(L"FmDelayMs", 0, 0, 2000);
    pcmDelayMs = setting(L"PcmDelayMs", 0, 0, 2000);
    liveKeyShift->store(setting(L"KeyShift", 0, -24, 24));
    liveKeyMode->store(setting(L"KeyMode", 1, 0, 2));
    liveSpeed->store(setting(L"PlaybackSpeed", 100, 0, 200));
    liveMidiPolyphonic->store(setting(L"MidiPolyphonic", 1, 0, 1));
    liveAftertouch->store(setting(L"Aftertouch", 0, 0, 1));
    liveMuteMask->store(setting(L"FmMask", 0, 0, 255));
    livePcmMuteMask->store(setting(L"PcmMask", 0, 0, 255));
    playbackLoops    = setting(L"Loops", 2, 1, 1000);
    infinitePlayback = setting(L"Infinite", 0, 0, 1);
    const auto savedGains = outputGains;
    selectOutput(setting(L"OutputBackend", 0, 0, 2));
    outputGains = savedGains;
    applyOutputGains();
    const auto midiName = settingText(L"MidiInputName");
    for (UINT device = 0; !midiName.empty() && (device < midiInGetNumDevs()); ++device)
    {
        MIDIINCAPSW capabilities{};
        if ((midiInGetDevCapsW(device, &capabilities, sizeof(capabilities)) == MMSYSERR_NOERROR) && (midiName == capabilities.szPname))
        {
            try
            {
                selectMidi(device);
            }
            catch (const std::exception& error)
            {
                status(L"ERROR: " + wide(error.what()));
            }
            break;
        }
    }
}
}
