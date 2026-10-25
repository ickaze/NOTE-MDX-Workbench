#pragma once
#include "pcm_pitch_shift.h"
#include <array>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace mdx {
using Bytes = std::vector<uint8_t>;
Bytes readFile(const std::filesystem::path &path);
std::string utf8(const std::string &sjis);
std::filesystem::path sjisPath(const std::string &sjis);
struct Stereo {
    double left = 0;
    double right = 0;
};
struct MonitorFrame {
    std::array<std::size_t, 16> sourceOffset{};
    static constexpr size_t points = 256;
    std::array<std::array<float, points>, 16> waveform{};
    std::array<int, 16> note;
    size_t cursor = 0;
    double seconds = 0;
    MonitorFrame() { note.fill(-1); }
};
struct Monitor {
    std::atomic<bool> waveforms{true};
    std::mutex mutex;
    MonitorFrame frame;
};
struct Pdx;
struct VoiceState {
    std::atomic<uint32_t> midiFmOverride{0};
    std::array<std::atomic<int>, 16> midiNotes;
    VoiceState() {
        for (auto &n : midiNotes)
            n = -1;
    }
    std::array<std::atomic<int>, 8> requestedVoice{{-1, -1, -1, -1, -1, -1, -1, -1}};
    std::array<std::array<uint8_t, 27>, 256> definitions{};
    std::array<bool, 256> defined{}, used{};
    std::array<std::array<uint8_t, 27>, 256> initialDefinitions{};
    std::array<bool, 256> initialDefined{};
    // Call with mutex held. Unchanged source definitions retain unsaved playback edits.
    void loadDefinitions(const std::array<std::array<uint8_t,27>,256>& incoming,
                         const std::array<bool,256>& present) {
        bool changed = false;
        for (int id=0; id<256; ++id) {
            if (initialDefined[id] != present[id] || (present[id] && initialDefinitions[id] != incoming[id])) {
                definitions[id] = incoming[id]; defined[id] = present[id]; changed = true;
            }
        }
        initialDefinitions = incoming; initialDefined = present;
        if(changed) ++catalogRevision;
    }
    bool modified(int id) const {
        return id>=0 && id<256 && defined[id] && (!initialDefined[id] || definitions[id] != initialDefinitions[id]);
    }
    uint64_t catalogRevision = 0;
    std::shared_ptr<Pdx> pdx;
    std::array<int, 8> pcmBank{}, pcmFormat{4, 4, 4, 4, 4, 4, 4, 4};
    std::mutex mutex;
    std::array<uint8_t, 256> registers{};
    std::array<std::array<uint8_t, 27>, 8> voices{};
    std::array<bool, 8> valid{};
    std::array<int, 8> ids{};
};
struct ScoreNote
{
    double begin = 0;
    double end = 0;
    int channel = 0;
    int note = -1;
    std::array<int, 4> carriers{{-1, -1, -1, -1}};
};
struct Timeline {
    double duration = 0, loopStart = 0, loopEnd = 0;
    int channel = -1;
    std::vector<ScoreNote> notes;
    int keyShift = 0;
    std::array<double,16> trackLoopStart{}, trackLoopEnd{};
    bool truncated = false;
};
struct Options {
    bool analysisOnly = false;
    bool skipFmSynthesis = false;
    bool skipPcmSynthesis = false;
    std::function<void(uint64_t,int,int)> registerLogOutput;
    std::function<void(uint64_t,int,int,uint32_t,int)> pcmLogOutput;
    int offlineChannel = -1; // Extract an FM channel from the DLL pre-pan output.
    bool skipOfflineFmMonitor = false; // VGM captures registers, without synthesizing FM waveforms.
    bool offlineFM = false; // Generate sequencer/PCM blocks for a separate batched FM worker.
    bool fastSeek = false;
    bool repeatSelection = false; // Restart finite selected tracks together at the longest end.
    std::shared_ptr<std::atomic<int>> liveSpeed; // Interactive sequencer speed, 0..200 percent.
    int fmBackend = 0; // 0: Independent OPM, 1: X68Sound bridge.
    std::shared_ptr<VoiceState> voiceState;
    std::function<void(uint64_t, int, int)> registerOutput;
    bool externalFm = false;
    std::shared_ptr<Monitor> monitor;
    bool resampleOutput = true;       // 64-clock production output stage. False only for core diagnostics.
    bool measuredOutputTiming = true; // Independent OPM ALG5/ALG7 carrier timing.
    std::shared_ptr<std::atomic<bool>> liveMeasuredOutputTiming;
    bool dacRightDelay = false; // User-defined YM3012 timing model: R delayed 28 clocks.
    std::shared_ptr<std::atomic<bool>> liveDacRightDelay;
    double independentGain = 2.0; // FM output level multiplier (+6.02 dB default).
    uint32_t pcmMuteMask = 0;     // Bits 0..7 mute PCM P..W, preserving sample position.
    std::shared_ptr<std::atomic<uint32_t>> livePcmMuteMask;
    uint32_t fmMuteMask = 0; // Bits 0..7 mute FM A..H without stopping synthesis.
    std::shared_ptr<std::atomic<uint32_t>> liveMuteMask;
    std::shared_ptr<std::atomic<double>> liveIndependentGain;
    int keyShift = 0; // Semitones, -24..24.
    int keyMode = 1;  // 0: FM only, 1: FM + PCM rate, 2: FM + PCM duration-preserving pitch.
    std::shared_ptr<std::atomic<int>> liveKeyMode;
    std::shared_ptr<std::atomic<int>> liveKeyShift;
    std::function<void(const std::string &)> diagnostic;
    std::shared_ptr<std::atomic<int>> liveLoops; // 0 unlimited; ignored after fade starts.
    int loops = 2;           // 0: unlimited traversals; finite tracks still end.
    double maxSeconds = 600; // 0: no time limit.
    double fadeSeconds = 3;
    std::shared_ptr<std::atomic<double>> liveGain, livePcmGain;
    std::shared_ptr<std::atomic<bool>> liveMidiPolyphonic;
    std::shared_ptr<std::atomic<bool>> liveAftertouch;
    int fmDelayMs = 0, pcmDelayMs = 0; // C86CTL only, 0..2000 ms; applied at playback start.
    double gain = 0.40;
    double pcmGain = 1.0; // PCM-only mix trim (0..4).
    int sampleRate = 48000;
};
struct Pdx {
    struct Entry {
        uint32_t offset = 0;
        uint32_t length = 0;
    };
    Bytes bytes;
    std::vector<Entry> entries;
    std::filesystem::path sourcePath;
    explicit Pdx(Bytes input);
    std::vector<int16_t> decode(size_t index, int format = 4) const;
};
struct Song {
    Bytes bytes;
    std::string title;
    std::string pdxName;
    size_t base = 0;
    std::vector<size_t> tracks;
    std::array<std::array<uint8_t, 27>, 256> voices{};
    std::array<bool, 256> hasVoice{};
    explicit Song(Bytes input);
};
class Source {
  public:
    virtual ~Source() = default;
    virtual size_t render(int16_t *stereo, size_t frames) = 0;
    virtual double seconds() const = 0;
    virtual bool servicePending() const { return false; }
    virtual bool service() { return false; }
    virtual bool seek(double, const std::function<bool()> & = {}) { return false; }
    virtual double duration() const { return 0; }
};
struct NativeFrame {Stereo pcm; double fmScale=0; uint8_t pan=0;};
struct NativeEvent {uint32_t clock_offset; uint32_t address; uint32_t value;};
struct NativeBlock {std::vector<NativeFrame> frames; std::vector<NativeEvent> events;};
class Player final : public Source {
  public:
    Player(Song song, std::shared_ptr<Pdx> pdx, Options options = {});
    ~Player();
    size_t render(int16_t *stereo, size_t frames) override;
    double seconds() const override;
    NativeBlock renderNativeBlock(size_t frames);
    const std::vector<std::string> &warnings() const;
    Timeline recordedTimeline() const;
    Timeline analyze(const std::function<bool()> &cancelled = {}, bool captureNotes = false);
    bool seek(double seconds, const std::function<bool()> &cancelled = {}) override;
    double position(const Timeline &timeline) const;
    void syncRegisters();
    bool silentEnd() const;
    bool unlimited() const;
    // Optional deterministic trace for compatibility testing (native sample time).
    std::function<void(uint64_t, int, int)> registerTrace;

  private:
    struct Impl;
    std::unique_ptr<Impl> p;
};
// Finite WAV rendering: sequencer/PCM prefetch and batched DLL FM synthesis run concurrently.
std::unique_ptr<Source> makeOfflineSource(Song song,std::shared_ptr<Pdx> pdx,Options options);
class SamplePlayer final : public Source {
  public:
    SamplePlayer(const Pdx &pdx, size_t sample, int format, Options options = {});
    size_t render(int16_t *stereo, size_t frames) override;
    double seconds() const override;
    double duration() const override;
    bool seek(double seconds, const std::function<bool()> &cancelled = {}) override;

  private:
    PcmPitchShift pitchShift;
    MonitorFrame monitorFrame;
    int sampleNote = 0;
    std::vector<int16_t> pcm;
    Options opts;
    double position = 0;
    double rate = 0;
    uint64_t framesDone = 0;
};
std::shared_ptr<Pdx> locatePdx(const Song &song, const std::filesystem::path &mdx,
                               const std::filesystem::path &overridePath = {});
void writeWav(Source &source, const std::filesystem::path &output, int rate,
              const std::function<bool()> &cancelled = {});
void appendWavLoop
(
    const std::filesystem::path& output,
    int rate,
    uint64_t begin,
    uint64_t end
);
} // namespace mdx
