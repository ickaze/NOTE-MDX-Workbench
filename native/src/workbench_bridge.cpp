// SPDX-License-Identifier: 0BSD
// Copyright (C) 2026 by I.C.KaZe
#define WB_CORE_IMPLEMENTATION
#include "core_api.h"
#include "workbench_runtime.h"
#include "vgm_export.h"
#include "mrl_export.h"
#include "output_pipeline.h"
#include "text_logic.h"
#include "latest_worker.h"
#include "instrument_viewer.h"
#include "notemdx/report.hpp"
#include <condition_variable>
#include <richedit.h>
#include <richole.h>
#include <tom.h>
#include "silent_system_warnings.h"

namespace
{
constexpr UINT BRIDGE_MESSAGE = WM_APP + 100;
constexpr int COMMAND_PLAY = 1;
constexpr int COMMAND_PAUSE = 2;
constexpr int COMMAND_STOP = 3;
constexpr int COMMAND_SEEK = 4;
constexpr int COMMAND_OUTPUT = 5;

constexpr int COMMAND_VOICES = 7;
constexpr int COMMAND_MASK = 8;
constexpr int COMMAND_GAIN = 9;
constexpr int COMMAND_PCM = 10;
constexpr int COMMAND_MIDI = 12;
constexpr int COMMAND_STATE = 13;
constexpr int COMMAND_QUIT = 14;
constexpr int COMMAND_MEASURED = 15;
constexpr int COMMAND_DAC = 16;
constexpr int COMMAND_FM_DELAY = 17;
constexpr int COMMAND_PCM_DELAY = 18;
constexpr int COMMAND_KEY = 19;
constexpr int COMMAND_KEY_MODE = 20;
constexpr int COMMAND_SPEED = 21;
constexpr int COMMAND_FM_GAIN = 22;
constexpr int COMMAND_POLYPHONIC = 23;
constexpr int COMMAND_AFTERTOUCH = 24;
constexpr int COMMAND_WAVEFORMS = 25;
constexpr int COMMAND_LANGUAGE = 26;
constexpr int CHANNEL_COUNT = 16;
constexpr int FM_COUNT = 8;
constexpr int OUTPUT_RATE = 48000;

std::thread bridgeThread;
std::mutex bridgeMutex;
std::condition_variable bridgeReady;
bool bridgeStarted = false;
bool bridgeSucceeded = false;
std::string bridgeError;
std::mutex bridgeErrorMutex;
void setBridgeError(const std::string &text)
{
  std::lock_guard<std::mutex> lock(bridgeErrorMutex);
  bridgeError = text;
}

std::string jsonString(const std::string &text)
{
  static const char HEX_DIGITS[] = "0123456789abcdef";
  std::string result = "\"";
  for (unsigned char character : text)
  {
    if ((character == '"') || (character == '\\'))
    {
      result += '\\';
      result += character;
    }
    else if (character < 32)
    {
      result += "\\u00";
      result += HEX_DIGITS[character >> 4];
      result += HEX_DIGITS[character & 15];
    }
    else
    {
      result += character;
    }
  }
  result += '"';
  return result;
}

int copyResult(const std::string &text, char *destination, size_t capacity)
{
  if ((destination != nullptr) && (capacity > text.size()))
  {
    std::copy(text.begin(), text.end(), destination);
    destination[text.size()] = 0;
  }
  return static_cast<int>(text.size() + 1);
}

std::wstring decodeInclude(const std::vector<uint8_t> &bytes)
{
  if (bytes.empty())
  {
    return {};
  }
  const char *data = reinterpret_cast<const char *>(bytes.data());
  int count = static_cast<int>(bytes.size());
  if ((count >= 3) && (bytes[0] == 239) && (bytes[1] == 187) && (bytes[2] == 191))
  {
    data += 3;
    count -= 3;
  }
  UINT codepage = CP_UTF8;
  DWORD flags = MB_ERR_INVALID_CHARS;
  int length = MultiByteToWideChar(codepage, flags, data, count, nullptr, 0);
  if ((length == 0) && (count > 0))
  {
    codepage = 932;
    flags = 0;
    length = MultiByteToWideChar(codepage, flags, data, count, nullptr, 0);
  }
  std::wstring result(length, L'\0');
  MultiByteToWideChar(codepage, flags, data, count, result.data(), length);
  return result;
}

std::string encodeSource(const wchar_t *source)
{
  std::wstring text = source;
  BOOL substituted = FALSE;
  const DWORD flags = WC_NO_BEST_FIT_CHARS;
  int count = WideCharToMultiByte(932, flags, text.data(), static_cast<int>(text.size()), nullptr,
                                  0, nullptr, &substituted);
  std::string bytes(count, '\0');
  WideCharToMultiByte(932, flags, text.data(), static_cast<int>(text.size()), bytes.data(), count,
                      nullptr, &substituted);
  if (substituted)
  {
    throw std::runtime_error("MML contains characters outside CP932.");
  }
  std::string normalized;
  for (size_t index = 0; index < bytes.size(); ++index)
  {
    if (bytes[index] == '\r')
    {
      normalized += '\n';
      if (((index + 1) < bytes.size()) && (bytes[index + 1] == '\n'))
      {
        ++index;
      }
    }
    else
    {
      normalized += bytes[index];
    }
  }
  return normalized;
}

void publishState(BridgeState &state)
{
  state = {};
  state.size = sizeof(state);
  state.playing = running.load() && !idleMidi.load();
  state.paused = pauseFlag.load();
  state.output = selectedOutput.load();
  state.midi = activeMidiDevice;
  for (const auto &note : retainedVoices->midiNotes)
  {
    if (note.load() >= 0)
    {
      state.reserved |= 1;
    }
  }
  state.position = static_cast<uint64_t>(std::max(0.0, songPosition.load()) * OUTPUT_RATE);
  state.total = static_cast<uint64_t>(std::max(0.0, songDuration.load()) * OUTPUT_RATE);
  state.muteMask = liveMuteMask->load() | (livePcmMuteMask->load() << FM_COUNT);
  state.masterPercent = static_cast<uint32_t>(liveGain->load() * 100);
  state.pcmPercent = static_cast<uint32_t>(livePcmGain->load() * 100);
  state.fmPercent = static_cast<int32_t>(std::lround(liveIndependentGain->load() * 100));
  state.measured = liveMeasuredOutputTiming->load();
  state.dac = liveDacRightDelay->load();
  state.fmDelay = fmDelayMs;
  state.pcmDelay = pcmDelayMs;
  state.key = liveKeyShift->load();
  state.keyMode = liveKeyMode->load();
  state.speed = liveSpeed->load();
  state.polyphonic = liveMidiPolyphonic->load();
  state.aftertouch = liveAftertouch->load();
  state.scoreRevision = scoreRevision.load();
  {
    std::lock_guard<std::mutex> lock(retainedVoices->mutex);
    std::copy(retainedVoices->registers.begin(), retainedVoices->registers.end(), state.registers);
  }
  {
    std::lock_guard<std::mutex> lock(monitorView->mutex);
    const auto &frame = monitorView->frame;
    state.cursor = static_cast<uint32_t>(frame.cursor);
    for (int channel = 0; channel < CHANNEL_COUNT; ++channel)
    {
      state.offsets[channel] = frame.sourceOffset[channel];
      state.notes[channel] = frame.note[channel];
      if (monitorView->waveforms.load())
      {
        std::copy(frame.waveform[channel].begin(), frame.waveform[channel].end(),
                  state.waveform[channel]);
      }
    }
  }
}

void executeRequest(HWND hwnd, const BridgeRequest &request)
{
  switch (request.command)
  {
  case COMMAND_PLAY:
    if ((request.path == nullptr) || (request.path[0] == 0))
    {
      throw std::runtime_error("No playback file supplied.");
    }
    selectionPlaying = (request.value != 0);
    load(request.path);
    if (request.bank != nullptr)
    {
      explicitPdx = request.bank;
    }
    if (request.source != nullptr)
    {
      bankSearchOverride = request.source;
    }
    start();
    break;
  case COMMAND_PAUSE:
    if (running.load() && !idleMidi.load())
    {
      pauseFlag.store(!pauseFlag.load());
    }
    break;
  case COMMAND_STOP:
    stop();
    selectionPlaying = false;
    break;
  case COMMAND_SEEK:
    seekRequest.store(request.value / 1000.0);
    break;
  case COMMAND_OUTPUT:
    selectOutput(request.value);
    break;
  case COMMAND_VOICES:
    showVoices();
    break;
  case COMMAND_MASK:
    liveMuteMask->store(request.value & 255);
    livePcmMuteMask->store((request.value >> FM_COUNT) & 255);
    break;
  case COMMAND_GAIN:
    liveGain->store(std::clamp(request.value, 0, 400) / 100.0);
    hardware.setGain(liveGain->load(), liveIndependentGain->load());
    break;
  case COMMAND_PCM:
    livePcmGain->store(std::clamp(request.value, 0, 400) / 100.0);
    break;
  case COMMAND_MIDI:
    selectMidi(request.value - 1);
    break;
  case COMMAND_MEASURED:
    liveMeasuredOutputTiming->store(request.value != 0);
    break;
  case COMMAND_DAC:
    liveDacRightDelay->store(request.value != 0);
    break;
  case COMMAND_FM_DELAY:
    fmDelayMs = std::clamp(request.value, 0, 2000);
    break;
  case COMMAND_PCM_DELAY:
    pcmDelayMs = std::clamp(request.value, 0, 2000);
    break;
  case COMMAND_KEY:
    liveKeyShift->store(std::clamp(request.value, -24, 24));
    break;
  case COMMAND_KEY_MODE:
    liveKeyMode->store(std::clamp(request.value, 0, 2));
    break;
  case COMMAND_SPEED:
    liveSpeed->store(std::clamp(request.value, 0, 200));
    break;
  case COMMAND_FM_GAIN:
    liveIndependentGain->store(std::clamp(request.value, 0, 400) / 100.0);
    hardware.setGain(liveGain->load(), liveIndependentGain->load());
    break;
  case COMMAND_POLYPHONIC:
    releaseVoiceKeys();
    liveMidiPolyphonic->store(request.value != 0);
    break;
  case COMMAND_AFTERTOUCH:
    liveAftertouch->store(request.value != 0);
    break;
  case COMMAND_WAVEFORMS:
    monitorWaveRequested = request.value != 0;
    monitorView->waveforms.store(monitorWaveRequested ||
                                 (voiceWindow && IsWindowVisible(voiceWindow)));
    return;
  case COMMAND_LANGUAGE:
    japanese.store(request.value == 0);
    updateAdvancedLanguage();
    return;
  case COMMAND_STATE:
    if (request.state == nullptr)
    {
      throw std::runtime_error("Missing state destination.");
    }
    publishState(*request.state);
    return;
  case COMMAND_QUIT:
    saveRuntimeSettings();
    closeMidi();
    stop();
    hardware.close();
    shutdownViewer();
    DestroyWindow(hwnd);
    return;
  default:
    throw std::runtime_error("Unknown bridge command.");
    break;
  }
  settingsChanged();
}
LRESULT CALLBACK bridgeProcedure(HWND hwnd, UINT messageNumber, WPARAM wp, LPARAM lp)
{
  if (messageNumber == BRIDGE_MESSAGE)
  {
    try
    {
      executeRequest(hwnd, *reinterpret_cast<const BridgeRequest *>(lp));
      return 1;
    }
    catch (const std::exception &error)
    {
      setBridgeError(error.what());
      return 0;
    }
  }
  if (messageNumber == MM_MIM_DATA)
  {
    if (reinterpret_cast<HMIDIIN>(wp) == midiInput)
    {
      if (!running.load())
      {
        startMidiIdle();
      }
      midiQueue->push(uint32_t(lp));
    }
    return 0;
  }
  if (messageNumber == WM_TIMER)
  {
    if (wp == 1)
    {
      refreshVoiceWindow();
      pollPlaybackPdx();
    }
    if (wp == 2)
    {
      KillTimer(hwnd, 2);
      saveRuntimeSettings();
    }
    return 0;
  }
  if (messageNumber == WM_DESTROY)
  {
    KillTimer(hwnd, 1);
    KillTimer(hwnd, 2);
    window = nullptr;
    PostQuitMessage(0);
    return 0;
  }
  return DefWindowProcW(hwnd, messageNumber, wp, lp);
}
} // namespace

extern "C" __declspec(dllexport) int WB_Init(HWND owner)
{
  std::unique_lock<std::mutex> lock(bridgeMutex);
  silenceSystemWarnings();
  if (!bridgeThread.joinable())
  {
    bridgeStarted = false;
    bridgeSucceeded = false;
    bridgeThread = std::thread(
        [owner]
        {
          japanese = (PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_JAPANESE);
          INITCOMMONCONTROLSEX controls{
              sizeof(controls), ICC_LISTVIEW_CLASSES | ICC_STANDARD_CLASSES | ICC_BAR_CLASSES};
          InitCommonControlsEx(&controls);
          WNDCLASSW cls{};
          cls.lpfnWndProc = bridgeProcedure;
          cls.hInstance = GetModuleHandleW(nullptr);
          cls.lpszClassName = L"NOTE-MDX-Workbench.Core.Dispatch";
          cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
          RegisterClassW(&cls);
          initializeViewer();
          // The dispatcher has no child controls and no original player UI.
          // Its only role is serializing commands and MIDI callbacks.
          HWND dispatcher =
              CreateWindowW(cls.lpszClassName, L"NOTE-MDX-Workbench.Core.Dispatch", 0, 0, 0, 0, 0,
                            HWND_MESSAGE, nullptr, cls.hInstance, nullptr);
          window = dispatcher;
          bool initialized = (dispatcher != nullptr);
          if (initialized)
          {
            try
            {
              readRuntimeSettings();
              if (setting(L"InstrumentOpen", 0, 0, 1)) showVoices();
              SetTimer(dispatcher, 1, 33, nullptr);
            }
            catch (const std::exception &error)
            {
              setBridgeError(error.what());
              initialized = false;
            }
          }
          {
            std::lock_guard<std::mutex> readyLock(bridgeMutex);
            bridgeSucceeded = initialized;
            bridgeStarted = true;
          }
          bridgeReady.notify_all();
          if (!initialized)
          {
            closeMidi();
            stop();
            hardware.close();
            shutdownViewer();
            if (dispatcher != nullptr)
            {
              DestroyWindow(dispatcher);
            }
            return;
          }
          MSG messageValue{};
          while (GetMessageW(&messageValue, nullptr, 0, 0) > 0)
          {
            // Route viewer hotkeys through the same host playback buttons.
            if (messageValue.message == WM_KEYDOWN &&
                (messageValue.wParam == VK_F5 || messageValue.wParam == VK_F6 || messageValue.wParam == VK_ESCAPE)) {
              if (!(messageValue.lParam & (LPARAM(1) << 30)))
                PostMessageW(owner, WM_APP + 73, messageValue.wParam, 0);
              continue;
            }
            if (voiceKeyMessage(messageValue))
            {
              continue;
            }
            if ((voiceWindow == nullptr) || !IsDialogMessageW(voiceWindow, &messageValue))
            {
              TranslateMessage(&messageValue);
              DispatchMessageW(&messageValue);
            }
          }
        });
  }
  bridgeReady.wait(lock, [] { return bridgeStarted; });
  return bridgeSucceeded;
}

extern "C" __declspec(dllexport) int WB_Command(const BridgeRequest *request)
{
  if ((window == nullptr) || (request == nullptr))
  {
    return 0;
  }
  return static_cast<int>(SendMessageW(window, BRIDGE_MESSAGE, 0, LPARAM(request)));
}

extern "C" __declspec(dllexport) int WB_State(BridgeState *state, size_t size)
{
  if ((state == nullptr) || (size != sizeof(BridgeState)))
  {
    return 0;
  }
  BridgeRequest request{};
  request.command = COMMAND_STATE;
  request.state = state;
  return WB_Command(&request);
}

extern "C" __declspec(dllexport) int WB_Error(char *destination, size_t capacity)
{
  std::lock_guard<std::mutex> lock(bridgeErrorMutex);
  return copyResult(bridgeError, destination, capacity);
}

extern "C" __declspec(dllexport) int WB_Status(char *destination, size_t capacity)
{
  std::lock_guard<std::mutex> lock(messageMutex);
  auto localized = translated(message, japanese.load());
  int length = WideCharToMultiByte(CP_UTF8, 0, localized.data(), static_cast<int>(localized.size()),
                                   nullptr, 0, nullptr, nullptr);
  std::string text(length, '\0');
  WideCharToMultiByte(CP_UTF8, 0, localized.data(), static_cast<int>(localized.size()), text.data(),
                      length, nullptr, nullptr);
  return copyResult(text, destination, capacity);
}

void stopAnalysisWorkers();
extern "C" __declspec(dllexport) void WB_Shutdown()
{
  stopAnalysisWorkers();
  if (bridgeThread.joinable())
  {
    BridgeRequest request{};
    request.command = COMMAND_QUIT;
    WB_Command(&request);
    bridgeThread.join();
  }
  restoreSystemWarningImports();
}

std::string compilerReport(const notemdx::Statistics &stats)
{
  std::string title = stats.title;
  if (!title.empty())
  {
    int count = MultiByteToWideChar(932, 0, title.data(), int(title.size()), nullptr, 0);
    std::wstring wideTitle(count, L'\0');
    MultiByteToWideChar(932, 0, title.data(), int(title.size()), wideTitle.data(), count);
    title = voiceUtf8(wideTitle);
  }
  return notemdx::format_statistics(stats, title);
}
struct EditorComputed
{
  std::string text;
  std::map<int, workbench::TrackSteps> steps;
  std::vector<workbench::EditorErrorSpan> spans;
  std::string gutter;
};
void writeEditorFields(std::ostream &output, const EditorComputed &computed)
{
  output << "\"steps\":{";
  bool first = true;
  for (const auto &row : computed.steps)
  {
    if (!first)
    {
      output << ',';
    }
    first = false;
    output << '"' << row.first << "\":[";
    for (std::size_t index = 0; index < row.second.size(); ++index)
    {
      if (index != 0)
      {
        output << ',';
      }
      output << row.second[index];
    }
    output << ']';
  }
  output << "},\"spans\":[";
  first = true;
  for (const auto &span : computed.spans)
  {
    if (!first)
    {
      output << ',';
    }
    first = false;
    output << "{\"Begin\":" << span.first << ",\"End\":" << span.second << '}';
  }
  output << "],\"gutter\":" << jsonString(computed.gutter);
}

std::wstring analysisWide(const std::string &text)
{
  const auto units = workbench::toUtf16(text);
  return std::wstring(units.begin(), units.end());
}
int analysisByteWidth(const std::string &text)
{
  const auto wide = analysisWide(text);
  return WideCharToMultiByte(932, 0, wide.data(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
}
std::vector<workbench::EditorErrorSpan> unsupportedSpans(const std::string &text)
{
  const auto units = workbench::toUtf16(text);
  std::vector<workbench::EditorErrorSpan> spans;
  for (std::size_t begin = 0; begin < units.size();)
  {
    auto end = begin + 1;
    if (units[begin] >= 0xd800 && units[begin] <= 0xdbff && end < units.size() &&
        units[end] >= 0xdc00 && units[end] <= 0xdfff)
    {
      ++end;
    }
    const std::wstring character(units.begin() + begin, units.begin() + end);
    BOOL substituted = FALSE;
    char encoded[8]{};
    WideCharToMultiByte(932, WC_NO_BEST_FIT_CHARS, character.data(), static_cast<int>(character.size()),
      encoded, sizeof(encoded), nullptr, &substituted);
    if (substituted)
    {
      spans.emplace_back(static_cast<std::int32_t>(begin), static_cast<std::int32_t>(end));
    }
    begin = end;
  }
  return spans;
}
void computeEditorData(EditorComputed &computed, const std::filesystem::path &source,
    const notemdx::Result &result)
{
  std::vector<workbench::SourceEvent> events;
  if (result.ok())
  {
    for (const auto &event : result.source_events)
    {
      if (event.file == source.u8string())
      {
        events.push_back({event.track, event.line, event.steps});
      }
    }
  }
  computed.steps = workbench::accumulateLineSteps(events);
  std::vector<workbench::EditorDiagnostic> diagnostics;
  for (const auto &diagnostic : result.diagnostics)
  {
    int line = diagnostic.editor_line;
    int column = diagnostic.editor_column;
    if (diagnostic.file == source.u8string())
    {
      line = diagnostic.line;
      column = diagnostic.column;
    }
    diagnostics.push_back({diagnostic.severity == notemdx::Severity::error, line, column});
  }
  computed.spans = workbench::buildErrorSpans(computed.text, diagnostics, analysisByteWidth);
  if (computed.spans.empty())
  {
    for (const auto &diagnostic : result.diagnostics)
    {
      if (diagnostic.message.find("MML contains characters outside CP932.") != std::string::npos)
      {
        computed.spans = unsupportedSpans(computed.text);
        break;
      }
    }
  }
  computed.gutter = workbench::buildGutterText(computed.text, computed.steps);
}

std::string compileMMLReport(const wchar_t *sourcePath, const wchar_t *text,
                                                const wchar_t *outputPath, int beginLine,
                                                int beginColumn, int endLine, int endColumn,
                                                bool mapping, bool publishCatalog, EditorComputed *computed = nullptr)
{
  try
  {
    const std::filesystem::path source(sourcePath);
    notemdx::Options options;
    options.editor_mapping = mapping;
    options.include_loader = [](const std::string &name, const std::string &parent,
                                notemdx::Source &loaded, std::string &error)
    {
      try
      {
        auto path = std::filesystem::u8path(parent).parent_path() / mdx::sjisPath(name);
        auto data = mdx::readFile(path);
        const auto text = decodeInclude(data);
        loaded = {path.u8string(), encodeSource(text.c_str())};
        return true;
      }
      catch (const std::exception &exception)
      {
        error = exception.what();
        return false;
      }
    };
    options.binary_loader = [](const std::string &name, const std::string &parent,
                               std::vector<uint8_t> &data, std::string &error)
    {
      try
      {
        data = mdx::readFile(std::filesystem::u8path(parent).parent_path() / mdx::sjisPath(name));
        return true;
      }
      catch (const std::exception &exception)
      {
        error = exception.what();
        return false;
      }
    };
    if (beginLine > 0)
    {
      options.selection = notemdx::SelectionRange{beginLine, beginColumn, endLine, endColumn};
    }
    auto result = notemdx::compile({source.u8string(), encodeSource(text)}, options);
    if (computed)
    {
      computeEditorData(*computed, source, result);
    }
    auto catalogOptions = options;
    catalogOptions.selection.reset();
    catalogOptions.voice_catalog_only = true;
    notemdx::Result catalog;
    if (!options.selection)
      catalog = notemdx::compile({source.u8string(), encodeSource(text)}, catalogOptions);
    if (!options.selection && publishCatalog)
    {
      mmlCatalogLoaded = true;
      std::lock_guard<std::mutex> lock(retainedVoices->mutex);
      std::array<std::array<uint8_t, 27>, 256> incoming{};
      std::array<bool, 256> present{};
      for (const auto &definition : catalog.voice_definitions)
      {
        incoming[definition.id] = definition.bytes;
        present[definition.id] = true;
      }
      retainedVoices->loadDefinitions(incoming, present);
    }
    std::ostringstream json;
    json << "{\"ok\":";
    if (result.ok())
    {
      json << "true";
      if ((outputPath != nullptr) && (outputPath[0] != 0))
      {
        std::ofstream output(std::filesystem::path(outputPath), std::ios::binary | std::ios::trunc);
        output.write(reinterpret_cast<const char *>(result.mdx.data()), result.mdx.size());
        if (!output)
        {
          throw std::runtime_error("Cannot save preview MDX.");
        }
      }
    }
    else
    {
      json << "false";
    }
    json << ",\"selected\":" << int(result.selected_channel) << ",\"tracks\":[";
    for (size_t channel = 0; channel < result.statistics.tracks.size(); ++channel)
    {
      if (channel != 0)
      {
        json << ',';
      }
      const auto &statistics = result.statistics.tracks[channel];
      json << "{\"total\":" << statistics.total_steps.value << ",\"overflow\":";
      if (statistics.total_steps.overflow)
      {
        json << "true";
      }
      else
      {
        json << "false";
      }
      json << ",\"loop\":";
      if (statistics.loop_steps)
      {
        json << statistics.loop_steps->value;
      }
      else
      {
        json << "null";
      }
      json << '}';
    }
    json << "],\"events\":[";
    bool first = true;
    std::optional<workbench::SourcePositions> sourcePositions;
    for (const auto &event : result.source_events)
    {
      if (event.file != source.u8string())
      {
        continue;
      }
      if (!first)
      {
        json << ',';
      }
      first = false;
      if (!sourcePositions)
      {
        sourcePositions.emplace(voiceUtf8(text), analysisByteWidth);
      }
      json << "{\"track\":" << event.track << ",\"offset\":" << event.offset
           << ",\"line\":" << event.line << ",\"column\":" << event.column
           << ",\"steps\":" << event.steps
           << ",\"char\":" << sourcePositions->character(event.line, event.column) << '}';
    }
    json << "],\"diagnostics\":[";
    first = true;
    for (const auto &diagnosticValue : result.diagnostics)
    {
      if (!first)
      {
        json << ',';
      }
      first = false;
      json << jsonString(notemdx::format_diagnostic(diagnosticValue));
    }
    json << "],\"diagnosticDetails\":[";
    first = true;
    for (const auto &diagnostic : result.diagnostics)
    {
      if (!first)
      {
        json << ',';
      }
      first = false;
      const bool inSource = diagnostic.file == source.u8string();
      int editorLine = diagnostic.editor_line;
      int editorColumn = diagnostic.editor_column;
      if (inSource)
      {
        editorLine = diagnostic.line;
        editorColumn = diagnostic.column;
      }
      const char *errorFlag = "false";
      if (diagnostic.severity == notemdx::Severity::error)
      {
        errorFlag = "true";
      }
      json << "{\"error\":" << errorFlag
           << ",\"file\":" << jsonString(diagnostic.file)
           << ",\"line\":" << diagnostic.line << ",\"column\":" << diagnostic.column
           << ",\"editorLine\":" << editorLine << ",\"editorColumn\":" << editorColumn << '}';
    }
    json << "],\"voices\":[";
    first = true;
    for (const auto &definition : catalog.voice_definitions)
    {
      if (!first)
      {
        json << ',';
      }
      first = false;
      json << "{\"id\":" << definition.id << ",\"file\":" << jsonString(definition.file)
           << ",\"line\":" << definition.line << ",\"endLine\":" << definition.end_line
           << ",\"toneOffset\":" << definition.tone_offset << ",\"bytes\":[";
      for (size_t index = 0; index < definition.bytes.size(); ++index)
      {
        if (index != 0)
        {
          json << ',';
        }
        json << unsigned(definition.bytes[index]);
      }
      json << "]}";
    }
    std::string report;
    if (result.ok())
    {
      report = compilerReport(result.statistics);
    }
    json << "],\"report\":" << jsonString(report) << '}';
    return json.str();
  }
  catch (const std::exception &error)
  {
    if (computed)
    {
      computed->steps.clear();
      computed->spans.clear();
      if (std::string(error.what()).find("MML contains characters outside CP932.") != std::string::npos)
      {
        computed->spans = unsupportedSpans(computed->text);
      }
      computed->gutter = workbench::buildGutterText(computed->text, computed->steps);
    }
    const auto json = "{\"ok\":false,\"diagnostics\":[" +
                      jsonString(std::string("error: ") + error.what()) + "]}";
    return json;
  }
}

int compileMML(const wchar_t *source, const wchar_t *text, const wchar_t *output,
    int bl, int bc, int el, int ec, char *destination, size_t capacity,
    bool mapping, bool publishCatalog = true)
{
  return copyResult(compileMMLReport(source, text, output, bl, bc, el, ec,
    mapping, publishCatalog), destination, capacity);
}

namespace
{
std::mutex exportMutex;
std::condition_variable exportChanged;
bool exportPaused = false;
int exportWorkers=0, exportParked=0;
struct ExportWorker {
 ExportWorker() {std::lock_guard<std::mutex> lock(exportMutex);++exportWorkers;}
 ~ExportWorker() {std::lock_guard<std::mutex> lock(exportMutex);--exportWorkers;exportChanged.notify_all();}
};
std::atomic<bool> exportCancelled{false};
std::atomic<int> vgmProgress{0};
void exportCheckpoint() {
  std::unique_lock<std::mutex> lock(exportMutex);
  if(exportPaused && !exportCancelled) {
    ++exportParked; exportChanged.notify_all();
    exportChanged.wait(lock, [] { return !exportPaused || exportCancelled.load(); });
    --exportParked;
  }
  if (exportCancelled) throw std::runtime_error("Export cancelled.");
}
std::array<std::atomic<int>, 16> wavProgress{};
std::atomic<int> wavJobs{1};
class WavProgressSource final : public mdx::Source
{
  mdx::Source &source;
  int job;
  double total;

public:
  WavProgressSource(mdx::Source &s, int index, double seconds)
      : source(s), job(index), total(std::max(0.001, seconds))
  {
  }
  size_t render(int16_t *audio, size_t frames) override
  {
    exportCheckpoint();
    auto n = source.render(audio, frames);
    exportCheckpoint();
    wavProgress[job] = std::clamp(20 + int(source.seconds() / total * 979), 20, 999);
    return n;
  }
  double seconds() const override { return source.seconds(); }
};
} // namespace
extern "C" __declspec(dllexport) int WB_Compile(const wchar_t *source, const wchar_t *text,
 const wchar_t *output, int bl, int bc, int el, int ec, char *destination, size_t capacity)
{
 return compileMML(source,text,output,bl,bc,el,ec,destination,capacity,true);
}
// Full synchronous compilation returns the same decorations as the editor worker.
// The existing WB_Compile ABI remains available for selection and reference tests.
extern "C" __declspec(dllexport) int WB_CompileDecorated(const wchar_t *source, const wchar_t *text,
 const wchar_t *output, int bl, int bc, int el, int ec, char *destination, size_t capacity)
{
  try
  {
    EditorComputed computed;
    computed.text = voiceUtf8(text ? std::wstring(text) : std::wstring());
    auto result = compileMMLReport(source, text, output, bl, bc, el, ec, true, true, &computed);
    if (result.empty() || result.back() != '}')
    {
      throw std::runtime_error("Invalid compiler report.");
    }
    result.pop_back();
    std::ostringstream decorated;
    decorated << result << ",\"decoration\":{";
    writeEditorFields(decorated, computed);
    decorated << "}}";
    return copyResult(decorated.str(), destination, capacity);
  }
  catch (const std::exception &error)
  {
    setBridgeError(error.what());
    return 0;
  }
}
extern "C" __declspec(dllexport) int WB_SelectionStats(const wchar_t *source, const wchar_t *text,
 const wchar_t *, int bl, int bc, int el, int ec, char *destination, size_t capacity)
{
 if (bl < 1) return 0;
 // Analysis has no playback state side effects, mapping list or output file.
 return compileMML(source,text,nullptr,bl,bc,el,ec,destination,capacity,false);
}

extern "C" __declspec(dllexport) int WB_EditorCheck(const wchar_t *source, const wchar_t *text,
 const wchar_t *, int, int, int, int, char *destination, size_t capacity)
{
  // Mapping/statistics and catalogue are computed without publishing playback state.
  return compileMML(source, text, nullptr, 0, 0, 0, 0, destination, capacity, true, false);
}
extern "C" __declspec(dllexport) void WB_ApplyVoiceCatalog(const uint8_t *entries, size_t count)
{
  if (count > 256 || (count != 0 && entries == nullptr))
  {
    return;
  }
  std::array<std::array<uint8_t, 27>, 256> incoming{};
  std::array<bool, 256> present{};
  for (size_t index = 0; index < count; ++index)
  {
    const auto *entry = entries + index * 28;
    present[entry[0]] = true;
    std::copy(entry + 1, entry + 28, incoming[entry[0]].begin());
  }
  mmlCatalogLoaded = true;
  std::lock_guard<std::mutex> lock(retainedVoices->mutex);
  retainedVoices->loadDefinitions(incoming, present);
}

extern "C" __declspec(dllexport) void WB_ExportStart() {
 std::lock_guard<std::mutex> lock(exportMutex); exportPaused=false; exportCancelled=false; vgmProgress=0;
}
extern "C" __declspec(dllexport) int WB_ExportCheckpoint()
{
  try
  {
    exportCheckpoint();
    return 1;
  }
  catch (...)
  {
    return 0;
  }
}
extern "C" __declspec(dllexport) int WB_ExportCancelled()
{
  return exportCancelled.load() ? 1 : 0;
}
extern "C" __declspec(dllexport) int WB_FinalizeOutput(const wchar_t *source,
    const wchar_t *target, unsigned format, const char *title, size_t titleSize)
{
  try
  {
    if (!source || !target || (titleSize && !title))
    {
      throw std::invalid_argument("Invalid output arguments");
    }
    ExportWorker worker;
    const workbench::OutputControl control{
      [] { exportCheckpoint(); },
      [](int value) { vgmProgress = value; }
    };
    std::string trackTitle;
    if (titleSize)
    {
      trackTitle.assign(title, titleSize);
    }
    workbench::finalizeOutput(source, target, static_cast<workbench::OutputFormat>(format),
      trackTitle, control);
    return 1;
  }
  catch (const std::exception &error)
  {
    setBridgeError(error.what());
    return 0;
  }
  catch (...)
  {
    setBridgeError("Output finalization failed");
    return 0;
  }
}
extern "C" __declspec(dllexport) int WB_PublishOutput(const wchar_t *source, const wchar_t *target)
{
  try
  {
    if (!source || !target) { throw std::invalid_argument("Invalid output paths"); }
    ExportWorker worker;
    workbench::publishOutput(source, target, {[] { exportCheckpoint(); }, {}});
    return 1;
  }
  catch (const std::exception &error)
  {
    setBridgeError(error.what());
    return 0;
  }
  catch (...)
  {
    setBridgeError("Output publication failed");
    return 0;
  }
}
extern "C" __declspec(dllexport) void WB_ExportControl(int action) {
 std::unique_lock<std::mutex> lock(exportMutex);
 if(action==2) exportCancelled=true;
 exportPaused=action==1;
 exportChanged.notify_all();
 if(action==1) exportChanged.wait(lock, [] {return exportParked==exportWorkers;});
}
extern "C" __declspec(dllexport) int WB_VgmProgress() { return vgmProgress.load(); }
extern "C" __declspec(dllexport) void WB_WavStart(int jobs)
{
  wavJobs = std::clamp(jobs, 1, 16);
  for (auto &value : wavProgress)
    value = 0;
}
extern "C" __declspec(dllexport) int WB_WavProgress()
{
  int total = 0, jobs = wavJobs.load();
  for (int i = 0; i < jobs; ++i)
    total += wavProgress[i].load();
  return total / jobs;
}
extern "C" __declspec(dllexport) int
WB_SaveWavFrozen(const wchar_t *songPath, const wchar_t *bankPath, const wchar_t *sourcePath,
                 const wchar_t *outputPath, int channelPlusOne, int loops, int fadeMilliseconds,
                 int masterPercent, int pcmPercent, int fmPercent, int measured, int dac,
                 int backend, int job)
{
  try
  {
    ExportWorker worker;
    exportCheckpoint();
    if (backend < 0 || backend > 1)
      throw std::runtime_error("C86CTL cannot export WAV audio.");
    job = std::clamp(job, 0, 15);
    wavProgress[job] = 1;
    mdx::Song song(mdx::readFile(songPath));
    mdx::Options options;
    options.loops = std::clamp(loops, 1, 1000);
    options.maxSeconds = 0;
    options.fadeSeconds = std::max(0, fadeMilliseconds) / 1000.0;
    options.gain = std::clamp(masterPercent, 0, 400) / 100.0;
    options.pcmGain = std::clamp(pcmPercent, 0, 400) / 100.0;
    options.independentGain = std::clamp(fmPercent, 0, 400) / 100.0;
    options.fmBackend = backend;
    options.measuredOutputTiming = measured != 0;
    options.dacRightDelay = dac != 0;
    if (channelPlusOne > 0)
    {
      const int channel = channelPlusOne - 1;
      if ((channel < 0) || (channel >= CHANNEL_COUNT))
      {
        throw std::runtime_error("Invalid WAV track.");
      }
      options.fmMuteMask = 255;
      options.pcmMuteMask = 255;
      if (channel < FM_COUNT)
      {
        if (backend == 0)
        {
          options.fmMuteMask = 0;
          options.offlineChannel = channel;
        }
        else
          options.fmMuteMask &= ~(1u << channel);
      }
      else
      {
        options.pcmMuteMask &= ~(1u << (channel - FM_COUNT));
      }
    }
    auto bank = mdx::locatePdx(song, sourcePath, bankPath);
    auto analysisOptions = options;
    analysisOptions.analysisOnly = true;
    mdx::Player analyzer(song, bank, analysisOptions);
    const auto timeline = analyzer.analyze([] { exportCheckpoint(); return false; });
    std::unique_ptr<mdx::Source> player;
    if (backend == 0)
      player = mdx::makeOfflineSource(std::move(song), bank, options);
    else
      player = std::make_unique<mdx::Player>(std::move(song), bank, options);
    double seconds =
        timeline.duration +
        std::max(0, options.loops - 1) * std::max(0.0, timeline.loopEnd - timeline.loopStart) +
        options.fadeSeconds;
    WavProgressSource progress(*player, job, seconds);
    mdx::writeWav(progress, outputPath, OUTPUT_RATE);
    if (timeline.loopEnd > timeline.loopStart)
    {
      mdx::appendWavLoop(outputPath, OUTPUT_RATE,
                         static_cast<uint64_t>(std::llround(timeline.loopStart * OUTPUT_RATE)),
                         static_cast<uint64_t>(std::llround(timeline.loopEnd * OUTPUT_RATE)));
    }
    wavProgress[job] = 1000;
    return 1;
  }
  catch (const std::exception &error)
  {
    wavProgress[std::clamp(job, 0, 15)] = 1000;
    setBridgeError(error.what());
    return 0;
  }
}

extern "C" __declspec(dllexport) int WB_SaveWav(const wchar_t *songPath, const wchar_t *bankPath,
                                                const wchar_t *sourcePath,
                                                const wchar_t *outputPath, int channelPlusOne,
                                                int loops, int fadeMilliseconds, int masterPercent,
                                                int pcmPercent)
{
  return WB_SaveWavFrozen(
      songPath, bankPath, sourcePath, outputPath, channelPlusOne, loops, fadeMilliseconds,
      masterPercent, pcmPercent, int(std::lround(liveIndependentGain->load() * 100)),
      liveMeasuredOutputTiming->load(), liveDacRightDelay->load(), selectedOutput.load(), 0);
}

extern "C" __declspec(dllexport) int WB_SaveVgm(const wchar_t *songPath, const wchar_t *bankPath,
                                               const wchar_t *sourcePath, const wchar_t *outputPath)
{
  try {
    ExportWorker worker;
    exportCheckpoint();
    mdx::Song song(mdx::readFile(songPath));
    auto bank = mdx::locatePdx(song, sourcePath, bankPath);
    auto bytes = mdx::makeVgm(std::move(song), bank, [](int value) { exportCheckpoint(); vgmProgress=value; });
    exportCheckpoint();
    std::ofstream file(std::filesystem::path(outputPath), std::ios::binary);
    file.write(reinterpret_cast<const char *>(bytes.data()), std::streamsize(bytes.size()));
    if (!file) throw std::runtime_error("Cannot write VGM output.");
    return 1;
  } catch (const std::exception &error) {
    setBridgeError(error.what());
    return 0;
  }
}

extern "C" __declspec(dllexport) int WB_Score(char *destination, size_t capacity)
{
  std::lock_guard<std::mutex> lock(scoreMutex);
  std::ostringstream json;
  json << "{\"duration\":" << scoreTimeline.duration << ",\"loopStart\":" << scoreTimeline.loopStart
       << ",\"loopEnd\":" << scoreTimeline.loopEnd << ",\"key\":" << scoreTimeline.keyShift;
  json << ",\"trackLoopStart\":[";
  for (int ch = 0; ch < 16; ++ch)
  {
    if (ch)
      json << ',';
    json << scoreTimeline.trackLoopStart[ch];
  }
  json << "],\"trackLoopEnd\":[";
  for (int ch = 0; ch < 16; ++ch)
  {
    if (ch)
      json << ',';
    json << scoreTimeline.trackLoopEnd[ch];
  }
  json << "],\"notes\":[";
  bool first = true;
  for (const auto &note : scoreTimeline.notes)
  {
    if (!first)
    {
      json << ',';
    }
    first = false;
    json << "{\"start\":" << note.begin << ",\"end\":" << note.end << ",\"track\":" << note.channel
         << ",\"note\":" << note.note << ",\"carrier\":[";
    for (int index = 0; index < 4; ++index)
    {
      if (index > 0)
      {
        json << ',';
      }
      json << note.carriers[index];
    }
    json << "]}";
  }
  json << "]}";
  return copyResult(json.str(), destination, capacity);
}

extern "C" __declspec(dllexport) int WB_VoiceEdit(char *destination, size_t capacity)
{
  std::lock_guard<std::mutex> lock(voiceEditsMutex);
  if (voiceEdits.empty())
  {
    return 0;
  }
  auto &edit = voiceEdits.front();
  std::string json =
      "{\"id\":" + std::to_string(edit.id) + ",\"mml\":" + jsonString(edit.mml) + "}";
  int count = copyResult(json, destination, capacity);
  if (capacity >= size_t(count))
  {
    voiceEdits.pop_front();
  }
  return count;
}

extern "C" __declspec(dllexport) int WB_EditorDecoration(HWND editor, const int32_t *spans,
                                                         size_t count)
{
  static std::vector<CHARRANGE> previous;
  IRichEditOle *ole = nullptr;
  ITextDocument *document = nullptr;
  const IID textDocument = {
      0x8cc497c0, 0xa1df, 0x11ce, {0x80, 0x98, 0x00, 0xaa, 0x00, 0x47, 0xbe, 0x5d}};
  SendMessageW(editor, EM_GETOLEINTERFACE, 0, LPARAM(&ole));
  if (!ole || FAILED(ole->QueryInterface(textDocument, reinterpret_cast<void **>(&document))))
  {
    if (ole)
      ole->Release();
    return 0;
  }
  LONG unused = 0;
  document->Undo(tomSuspend, &unused);
  auto events = SendMessageW(editor, EM_SETEVENTMASK, 0, 0);
  auto modified = SendMessageW(editor, EM_GETMODIFY, 0, 0);
  SendMessageW(editor, WM_SETREDRAW, FALSE, 0);
  // TOM ranges change formatting without touching the user's selection or
  // RichEdit's internal Shift-selection anchor (even while unfocused).
  const auto background = [document](LONG begin, LONG end, COLORREF foreground, COLORREF color) {
    ITextRange *range = nullptr;
    ITextFont *font = nullptr;
    if (SUCCEEDED(document->Range(begin, end, &range)) && range) {
      if (SUCCEEDED(range->GetFont(&font)) && font) {
        font->SetForeColor(LONG(foreground));
        font->SetBackColor(LONG(color));
        font->Release();
      }
      range->Release();
    }
  };
  if (count == 0)
    background(0, LONG_MAX, RGB(178, 178, 255), RGB(20, 22, 48));
  else
    for (auto range : previous)
      background(range.cpMin, range.cpMax, RGB(178, 178, 255), RGB(20, 22, 48));
  previous.clear();
  for (size_t i = 0; i < count; ++i) {
    CHARRANGE range{spans[i * 4], spans[i * 4 + 1]};
    background(range.cpMin, range.cpMax, COLORREF(spans[i * 4 + 2]), COLORREF(spans[i * 4 + 3]));
    previous.push_back(range);
  }
  SendMessageW(editor, EM_SETMODIFY, modified, 0);
  SendMessageW(editor, EM_SETEVENTMASK, 0, events);
  document->Undo(tomResume, &unused);
  document->Release();
  ole->Release();
  SendMessageW(editor, WM_SETREDRAW, TRUE, 0);
  InvalidateRect(editor, nullptr, FALSE);
  return 1;
}

extern "C" __declspec(dllexport) int WB_EditorBackground(HWND editor, const int32_t *spans, size_t count)
{
  std::vector<int32_t> colors;
  colors.reserve(count * 4);
  for (size_t index = 0; index < count; ++index)
  {
    colors.push_back(spans[index * 3]);
    colors.push_back(spans[index * 3 + 1]);
    colors.push_back(LONG(RGB(178, 178, 255)));
    colors.push_back(spans[index * 3 + 2]);
  }
  return WB_EditorDecoration(editor, colors.data(), count);
}

extern "C" __declspec(dllexport) int WB_SaveMrl(const wchar_t *songPath, const wchar_t *bankPath,
                                               const wchar_t *sourcePath, const wchar_t *outputPath)
{
    try {
        ExportWorker worker;
        exportCheckpoint();
        mdx::exportRegisterLog(songPath, outputPath, sourcePath, bankPath, 2,
            [](int value) { exportCheckpoint(); if (value >= 0) vgmProgress = value; });
        exportCheckpoint();
        return 1;
    } catch (const std::exception &error) {
        setBridgeError(error.what());
        return 0;
    }
}

namespace
{
struct AnalysisRequest
{
  std::uint64_t generation = 0;
  std::uint64_t revision = 0;
  std::uint32_t begin = 0;
  std::uint32_t end = 0;
  std::wstring path;
  std::string text;
  bool readFile = false;
  bool selection = false;
};
using AnalysisWorker = workbench::LatestWorker<AnalysisRequest>;
std::mutex analysisRegistryMutex;
std::array<std::shared_ptr<AnalysisWorker>, 2> analysisWorkers;

mdx::Bytes readAnalysisSource(const std::wstring &source)
{
  auto path = std::filesystem::absolute(std::filesystem::path(source)).lexically_normal().native();
  if (path.rfind(L"\\\\?\\", 0) != 0 && path.rfind(L"\\\\.\\", 0) != 0)
  {
    if (path.rfind(L"\\\\", 0) == 0)
    {
      path = L"\\\\?\\UNC\\" + path.substr(2);
    }
    else
    {
      path = L"\\\\?\\" + path;
    }
  }
  // Match Go's read/write sharing and binary reads. The MDX reader's nonempty
  // and 128 MiB restrictions do not apply to an editor's MUS source file.
  const auto handle = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
    nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
  if (handle == INVALID_HANDLE_VALUE)
  {
    throw std::system_error(GetLastError(), std::system_category(), "Cannot open analysis source");
  }
  std::unique_ptr<void, decltype(&CloseHandle)> file(handle, &CloseHandle);
  mdx::Bytes data;
  std::array<std::uint8_t, 65536> buffer;
  for (;;)
  {
    DWORD count = 0;
    if (!ReadFile(file.get(), buffer.data(), static_cast<DWORD>(buffer.size()), &count, nullptr))
    {
      throw std::system_error(GetLastError(), std::system_category(), "Cannot read analysis source");
    }
    if (count == 0)
    {
      return data;
    }
    data.insert(data.end(), buffer.begin(), buffer.begin() + count);
  }
}

std::pair<int, int> analysisLocation(const std::wstring &text, std::uint32_t offset)
{
  const auto limit = std::min<std::size_t>(offset, text.size());
  int line = 1;
  std::size_t start = 0;
  for (std::size_t index = 0; index < limit; ++index)
  {
    if (text[index] == L'\r' || text[index] == L'\n')
    {
      if (text[index] == L'\r' && index + 1 < limit && text[index + 1] == L'\n')
      {
        ++index;
      }
      ++line;
      start = index + 1;
    }
  }
  const auto column = WideCharToMultiByte(932, 0, text.data() + start,
    static_cast<int>(limit - start), nullptr, 0, nullptr, nullptr);
  return {line, column + 1};
}
std::string processAnalysis(const AnalysisRequest &request)
{
  EditorComputed computed;
  computed.text = request.text;
  std::string result = "{}";
  std::string error;
  try
  {
    if (request.readFile)
    {
      computed.text = workbench::normalizeSourceText(voiceUtf8(decodeInclude(readAnalysisSource(request.path))));
    }
    const auto text = analysisWide(computed.text);
    int bl = 0;
    int bc = 0;
    int el = 0;
    int ec = 0;
    if (request.selection)
    {
      const auto begin = analysisLocation(text, request.begin);
      const auto end = analysisLocation(text, request.end);
      bl = begin.first;
      bc = begin.second;
      el = end.first;
      ec = end.second;
    }
    EditorComputed *decoration = nullptr;
    if (!request.selection)
    {
      decoration = &computed;
    }
    result = compileMMLReport(request.path.c_str(), text.c_str(), nullptr,
      bl, bc, el, ec, !request.selection, false, decoration);
  }
  catch (const std::exception &exception)
  {
    error = exception.what();
  }
  std::string readFlag = "false";
  if (request.readFile)
  {
    readFlag = "true";
  }
  std::ostringstream output;
  output << "{\"generation\":" << request.generation << ",\"revision\":" << request.revision
    << ",\"begin\":" << request.begin << ",\"end\":" << request.end
    << ",\"path\":" << jsonString(voiceUtf8(request.path))
    << ",\"text\":" << jsonString(computed.text)
    << ",\"readFile\":" << readFlag
    << ",\"error\":" << jsonString(error) << ",\"result\":" << result << ',';
  writeEditorFields(output, computed);
  output << '}';
  return output.str();
}
std::shared_ptr<AnalysisWorker> analysisWorker(unsigned kind)
{
  if (kind >= analysisWorkers.size())
  {
    throw std::invalid_argument("Unknown analysis worker");
  }
  std::lock_guard<std::mutex> lock(analysisRegistryMutex);
  return analysisWorkers[kind];
}
}

extern "C" __declspec(dllexport) int WB_AnalysisStart(unsigned kind)
{
  try
  {
    if (kind >= analysisWorkers.size())
    {
      throw std::invalid_argument("Unknown analysis worker");
    }
    std::lock_guard<std::mutex> lock(analysisRegistryMutex);
    if (analysisWorkers[kind])
    {
      throw std::logic_error("Analysis worker is already running");
    }
    analysisWorkers[kind] = std::make_shared<AnalysisWorker>(processAnalysis);
    return 1;
  }
  catch (const std::exception &error)
  {
    setBridgeError(error.what());
    return 0;
  }
  catch (...)
  {
    setBridgeError("Cannot start analysis worker");
    return 0;
  }
}
extern "C" __declspec(dllexport) void WB_AnalysisStop(unsigned kind)
{
  std::shared_ptr<AnalysisWorker> worker;
  {
    std::lock_guard<std::mutex> lock(analysisRegistryMutex);
    if (kind >= analysisWorkers.size())
    {
      return;
    }
    worker = std::move(analysisWorkers[kind]);
  }
  if (worker)
  {
    worker->stop();
  }
}
void stopAnalysisWorkers()
{
  WB_AnalysisStop(0);
  WB_AnalysisStop(1);
}
extern "C" __declspec(dllexport) int WB_AnalysisSubmit(unsigned kind,
    std::uint64_t generation, std::uint64_t revision, std::uint32_t begin, std::uint32_t end,
    const wchar_t *path, const char *text, std::size_t size, int readFile)
{
  try
  {
    if (!path || (size && !text) || (readFile != 0 && readFile != 1) || (kind == 1 && readFile))
    {
      throw std::invalid_argument("Invalid analysis request");
    }
    auto worker = analysisWorker(kind);
    if (!worker)
    {
      throw std::logic_error("Analysis worker is not running");
    }
    AnalysisRequest request;
    request.generation = generation;
    request.revision = revision;
    request.begin = begin;
    request.end = end;
    request.path = path;
    if (size)
    {
      request.text.assign(text, size);
    }
    request.readFile = readFile != 0;
    request.selection = kind == 1;
    return worker->submit(std::move(request));
  }
  catch (const std::exception &error)
  {
    setBridgeError(error.what());
    return 0;
  }
  catch (...)
  {
    setBridgeError("Cannot submit analysis request");
    return 0;
  }
}
extern "C" __declspec(dllexport) std::int64_t WB_AnalysisTake(unsigned kind,
    char *destination, std::size_t capacity, int wait)
{
  try
  {
    auto worker = analysisWorker(kind);
    if (!worker)
    {
      return 0;
    }
    return static_cast<std::int64_t>(worker->receive(destination, capacity, wait != 0));
  }
  catch (const std::exception &error)
  {
    setBridgeError(error.what());
    return -1;
  }
  catch (...)
  {
    setBridgeError("Cannot receive analysis result");
    return -1;
  }
}
