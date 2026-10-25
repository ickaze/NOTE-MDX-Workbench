#include "opm_backend.h"
#include "player.h"
#include "lfo_random.h"
#include <thread>
#include <mutex>
#include <condition_variable>
#include <deque>
#include "carrier_notes.h"
#include "stereo_output.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <sstream>
#include <stdexcept>

namespace mdx {
namespace {
constexpr int nativeRate = 62500; // 4 MHz YM2151 / 64 chip clocks per frame.
struct Lfo {
  bool enabled = false;
  int wave = 0;
  int period = 1;
  int delta = 0;
  int amplitudeScale = 1;
  uint64_t phase = 0;
  double squareInitial = 0;
  double randomValue = 0;
  double value(bool pitch = false) const {
    if (!enabled)
      return 0;
    double x =
        static_cast<double>(phase % static_cast<uint64_t>(period)) / period;
    const double depth = double(delta) * amplitudeScale;
    if (wave == 3)
      return randomValue;
    if (wave == 0)
      return pitch ? (double((phase + period / 2) % uint64_t(period)) -
                      period / 2) *
                         depth
                   : x * period * depth + std::max(0.0, -period * depth);
    if (wave == 1)
      // Depth already uses 8 fractional bits for both MP and MA.
      // WAVE+4 expansion has already been applied above.
      return pitch ? phase == 0 ? squareInitial : ((phase - 1) / period) % 2 ? -depth : depth
             : ((phase / period) % 2 ? depth : 0.0) + std::max(0.0, -depth);
    // Observed triangle conventions differ between pitch and amplitude.
    // Integer overflow behavior and random-wave internals remain unverified.
    // Register traces distinguish MP's bipolar triangle from MA's unipolar one.
    // MP starts at +delta, turns after half a period, then every full period.
    double t = double((phase + (pitch ? period / 2 : 0)) % (uint64_t(period) * 2));
    double ramp = t <= period ? t : 2.0 * period - t;
    // Negative MA depth starts at maximum attenuation, then approaches zero.
    // This offset is observed in register captures; MP remains bipolar.
    if (!pitch && depth < 0)
      return (ramp - period) * depth;
    return (ramp + (pitch ? 1.0 - period / 2 : 0.0)) * depth;
  }
};
struct Track {
    size_t pc = 0, referencedOffset = 0;
    size_t sourceOffset = 0;
    int referencedValue = -1;
    int wait = 0, gate = 0, delay = 0, keyDelay = 0, pending = -1;
    bool ended = false, waiting = false, signal = false;
    bool held = false, tieNext = false, noKeyOff = false;
    std::array<uint8_t, 27> instrument{};
    bool configured = false;
    int voice = -1, volume = 8, pan = 3, quantize = 8;
    int note = -1, detune = 0, transpose = 0, porta = 0, nextPorta = 0;
    double slide = 0;
    int bank = 0, format = 4, age = 0, lfoDelay = 0;
    uint64_t loops = 0;
    std::map<size_t, uint64_t> firstVisit;
    double firstEnd = -1, loopStart = 0, loopEnd = 0;
    size_t loopJumpSource = 0;
    int hardwareSensitivity = 0;
    bool hardwareSync = false, hardwareEnabled = true;
    int pendingVoice = -1;
    Lfo pitchLfo, volumeLfo;
    struct Repeat {
        size_t counter;
        size_t body;
        int left;
    };
    std::vector<Repeat> repeats;
    const std::vector<int16_t> *pcm = nullptr;
    double pcmPosition = 0;
    PcmPitchShift pitchShift;
};
int signedWord(unsigned value) {
    return value < 32768 ? static_cast<int>(value) : static_cast<int>(value) - 65536;
}
int signedByte(unsigned value) {
    return value < 128 ? static_cast<int>(value) : static_cast<int>(value) - 256;
}
} // namespace
struct Player::Impl {
    Player &owner;
    Song song;
    std::shared_ptr<Pdx> pdx;
    Options options;
    OpmBackend independent;
    uint32_t muteMask = 0;
    uint32_t pcmMuteMask = 0;
    double independentGain = 2;
    double nativeFmScale = 0;
    int keyShift = 0, keyMode = 1;
    Clock64Output outputStage;
    MonitorFrame monitorFrame;
    bool captureWaves=true;
    std::array<int, 8> deferredKeyOn;
    std::array<uint64_t, 8> lastKeyOff;

    std::array<uint8_t, 256> registers{};
    std::vector<Track> tracks;
    std::map<int, std::vector<int16_t>> decoded;
    size_t decodedSamples = 0;
    std::vector<std::string> notices;
    int tempo = 200;
    double samplesToTick = 0, scoreTime = 0, playbackSpeed = 1;
    int amd = 0, pmd = 0;
    SoftwareLfoRandom randomLfo;
    uint64_t selectionCycleStart = 0;
    double selectionScoreStart = 0;
    bool selectionRestarted = false;
    uint64_t nativeTime = 0, outputTime = 0;
    int releaseRemaining = -1;
    unsigned quietSamples = 0;
    bool endedSilently = false;
    bool done = false, reconstructing = false;
    double fadeStart = -1, fadeLength = 0;
    // Resampling uses a box integral over the exact source/output interval.
    // Native sample generation therefore never runs ahead of the sequencer.
    Stereo current;
    double sourceRemaining = 0;
    Impl(Player &o, Song s, std::shared_ptr<Pdx> bank, Options opts)
        : owner(o), song(std::move(s)), pdx(std::move(bank)), options(opts),
          independent(bool(opts.monitor) || ((opts.loops == 0 || opts.liveLoops) && !opts.analysisOnly && !opts.skipOfflineFmMonitor), !opts.externalFm && !opts.analysisOnly && !opts.offlineFM && !opts.skipFmSynthesis, opts.fmBackend) {
        if (opts.keyMode < 0 || opts.keyMode > 2 || opts.keyShift < -24 || opts.keyShift > 24 ||
            opts.sampleRate < 8000 || opts.sampleRate > 192000 || opts.loops < 0 || opts.loops > 1000 ||
            !std::isfinite(opts.maxSeconds) || opts.maxSeconds < 0 || opts.maxSeconds > 3600 ||
            !std::isfinite(opts.gain) || opts.gain < 0 || opts.gain > 4 || !std::isfinite(opts.fadeSeconds) ||
            opts.fadeSeconds < 0 || opts.fadeSeconds > 60 || !std::isfinite(opts.independentGain) ||
            opts.independentGain < 0 || opts.independentGain > 8 || opts.fmMuteMask > 255 ||
            opts.fmDelayMs < 0 || opts.fmDelayMs > 2000 || opts.pcmDelayMs < 0 || opts.pcmDelayMs > 2000 ||
            opts.pcmMuteMask > 255 || !std::isfinite(opts.pcmGain) || opts.pcmGain < 0 || opts.pcmGain > 4)
            throw std::runtime_error("Invalid playback options");
        deferredKeyOn.fill(-1);
        lastKeyOff.fill(UINT64_MAX);
        muteMask = opts.fmMuteMask;
        pcmMuteMask = opts.pcmMuteMask;
        independentGain = opts.independentGain;
        keyShift = opts.keyShift;
        keyMode = opts.keyMode;
        independent.set_measured_output_timing(opts.measuredOutputTiming);
        for (size_t pos : song.tracks) {
            Track t;
            t.pc = pos;
            tracks.push_back(t);
        }
    }
    void warn(const std::string &message) {
        if (std::find(notices.begin(), notices.end(), message) == notices.end() && notices.size() < 512) {
            notices.push_back(message);
            if (options.diagnostic)
                options.diagnostic(message);
        }
    }
    std::string detail(size_t channel, const std::string &message, const std::string &expected, int64_t value,
                       size_t offset) {
        std::ostringstream text;
        text << message << " (channel " << (channel < 8 ? char('A' + channel) : char('P' + channel - 8))
             << ", file offset 0x" << std::hex << offset << std::dec << ", value=";
        if (value < 0 && message == "Unexpected end of MDX command stream")
            text << "EOF";
        else
            text << value << " (0x" << std::hex << value << std::dec << ")";
        text << ", expected=" << expected << ")";
        return text.str();
    }
    [[noreturn]] void fail(size_t channel, const std::string &message,
                           const std::string &expected = "valid command/state for this operation") {
        auto &t = tracks[channel];
        throw std::runtime_error(detail(channel, message, expected, t.referencedValue, t.referencedOffset));
    }
    [[noreturn]] void badValue(size_t ch, const std::string &message, int64_t value,
                               const std::string &expected, size_t offset) {
        throw std::runtime_error(detail(ch, message, expected, value, offset));
    }
    int byte(size_t ch) {
        Track &t = tracks[ch];
        t.referencedOffset = t.pc;
        if (t.pc >= song.bytes.size()) {
            t.referencedValue = -1;
            fail(ch, "Unexpected end of MDX command stream",
                 "byte 0..255 within file size " + std::to_string(song.bytes.size()));
        }
        return t.referencedValue = song.bytes[t.pc++];
    }
    int word(size_t ch) {
        size_t offset = tracks[ch].pc;
        int high = byte(ch);
        int value = high * 256 + byte(ch);
        tracks[ch].referencedOffset = offset;
        tracks[ch].referencedValue = value;
        return value;
    }
    void jump(size_t ch, int64_t address) {
        size_t low = song.base + 2 + tracks.size() * 2;
        if (address < int64_t(low) || address >= int64_t(song.bytes.size()))
            badValue(ch, "MDX jump outside sequence data", address,
                     std::to_string(low) + ".." + std::to_string(song.bytes.size() - 1),
                     tracks[ch].referencedOffset);
        tracks[ch].pc = static_cast<size_t>(address);
    }
    void send(int address, int value) {
        if (options.analysisOnly || reconstructing)
            return;
        int channel = address == 8 ? value & 7 : address >= 0x20 ? address & 7 : -1;
        // Pan gating removes channel output; MIDI has a separate voice path.
        if (address >= 0x20 && address <= 0x27 && (muteMask & (1u << (address - 0x20))))
            value &= 0x3f;
        independent.write_register(static_cast<uint8_t>(address), static_cast<uint8_t>(value));
        if(options.offlineFM && owner.registerTrace) owner.registerTrace(nativeTime,address,value);
        if (channel >= 0 && (muteMask & (1u << channel)) && !(address >= 0x20 && address <= 0x27))
            return;
        if (options.registerOutput)
            options.registerOutput(nativeTime, address, value);
        if (owner.registerTrace && !options.offlineFM)
            owner.registerTrace(nativeTime, address, value);
    }
    void write(int address, int value) {
        address &= 255;
        value &= 255;
        if (options.registerLogOutput && !reconstructing && !options.analysisOnly)
            options.registerLogOutput(nativeTime, address, value);
        registers[address] = static_cast<uint8_t>(value);
        if (address == 0x19) {
            if (value & 128)
                pmd = value & 127;
            else
                amd = value & 127;
        }
        if (address == 8) {
            unsigned ch = value & 7;
            if (!(value & 0x78)) {
                lastKeyOff[ch] = nativeTime;
                deferredKeyOn[ch] = -1;
            } else if (lastKeyOff[ch] == nativeTime) {
                // Preserve a distinct key-off sample before retriggering (16 us).
                deferredKeyOn[ch] = value;
                return;
            }
        }
        send(address, value);
    }
    static int coarseAttenuation(int level) {
        // MXDRV's 16 levels: 2 dB nominal spacing, quantized to TL steps.
        return 42 - (8 * level) / 3;
    }
    int attenuation(const Track &t) const {
        return (t.volume & 128) ? (t.volume & 127) : coarseAttenuation(t.volume);
    }
  double pcmVolume(const Track &t) const {
    // MDX @v stores attenuation, not a PCM8A extended volume byte.
    // Convert through MXDRV's attenuation bands before the PCM8 gain law.
    int att = attenuation(t);
    if (att > 42)
      return 0;
    int level = 15;
    while (level > 0 && coarseAttenuation(level) < att)
      --level;
    constexpr int steps[16] = {-21, -17, -15, -12, -10, -7, -5, -3,
                               0,   2,   4,   7,   10,  12, 15, 17};
    constexpr int linearUnits[16] = {2,  3,  4,  5,  6,  8,  10, 12,
                                     16, 20, 24, 32, 40, 48, 64, 80};
    double gain =
        t.format <= 4 ? std::pow(1.1, steps[level]) : linearUnits[level] / 16.0;
    return gain;
  }
  void pcmEvent(size_t ch, int op, uint32_t arg = 0, int format = 0) {
    if (ch >= 8 && options.pcmLogOutput && !reconstructing &&
        !options.analysisOnly)
      options.pcmLogOutput(nativeTime, int(ch - 8), op, arg, format);
  }
  void volume(size_t ch) {
    Track &t = tracks[ch];
    if (ch >= 8) {
      pcmEvent(ch, 3, uint32_t(std::llround(pcmVolume(t) * 65536)));
      return;
    }
    if (!t.configured)
      return;
    const auto &voice = t.instrument;
    // Operator order in MDX/register groups: M1,M2,C1,C2.
    constexpr int carriers[8] = {8, 8, 8, 8, 12, 14, 14, 15};
    double add = attenuation(t);
    // MD counts the note-on tick: MD1 has no waiting ticks, MD4 has three.
    // During the wait, use the unmodulated attenuation for either depth sign.
    if (t.age >= std::max(0, t.lfoDelay - 1))
      add += std::trunc(t.volumeLfo.value() / 256.0);
    for (int op = 0; op < 4; ++op) {
      int level = voice[7 + op] & 127;
      if (carriers[voice[1] & 7] & (1 << op))
        level = static_cast<int>(std::clamp(level + add, 0.0, 127.0));
      write(0x60 + op * 8 + static_cast<int>(ch), level);
    }
  }
  void pan(size_t ch) {
    if (ch >= 8)
      pcmEvent(ch, 2, tracks[ch].pan);
    if (ch < 8)
      write(0x20 + static_cast<int>(ch),
            (registers[0x20 + ch] & 63) | (tracks[ch].pan << 6));
  }
    void voice(size_t ch, int id) {
        Track &t = tracks[ch];
        if (ch >= 8) {
            t.bank = id;
            return;
        }
        if (options.voiceState) {
            std::lock_guard<std::mutex> lock(options.voiceState->mutex);
            if (options.voiceState->defined[id]) {
                song.voices[id] = options.voiceState->definitions[id];
                song.hasVoice[id] = true;
            }
        }
        if (!song.hasVoice[id]) {
            std::string ids;
            for (int i = 0; i < 256; ++i)
                if (song.hasVoice[i]) {
                    if (!ids.empty())
                        ids += ",";
                    ids += std::to_string(i);
                }
            warn("ERROR: " + detail(ch, "Referenced FM voice is missing", "defined voice IDs {" + ids + "}",
                                    id, t.referencedOffset));
            off(ch);
            t.voice = -1;
            t.configured = false;
            return;
        }
        t.voice = id;
        t.instrument = song.voices[id];
        if (options.voiceState) {
            std::lock_guard<std::mutex> lock(options.voiceState->mutex);
            options.voiceState->used[id] = true;
        }
        t.configured = true;
        auto &v = t.instrument;
        // Physical register writes are serial: hide the partially programmed voice.
        // Do not reset/key-off the chip or wait; retain envelopes and note timing.
        write(0x20 + static_cast<int>(ch), (v[1] & 63) | (options.externalFm ? 0 : t.pan << 6));
        for (int group = 0; group < 6; ++group)
            for (int op = 0; op < 4; ++op)
                if (!options.externalFm || group != 1)
                    write(0x40 + group * 32 + op * 8 + static_cast<int>(ch), v[3 + group * 4 + op]);
        volume(ch); // Send final volume-adjusted TL once, never the raw loud intermediate.
        if (options.externalFm)
            write(0x20 + static_cast<int>(ch), (v[1] & 63) | (t.pan << 6));
    }
  void pitch(size_t ch) {
    auto &t = tracks[ch];
    if (ch >= 8 || t.note < 0)
      return;
    // At 4 MHz, MDX note 0 maps to KC 0 with a 5/64 correction.
    // 64*(2-12*log2(4000000/3579545)) rounds to 5, not 128.
    double raw =
        (t.note + t.transpose) * 64.0 + 5 + t.detune + t.slide;
    if (t.age >= t.lfoDelay)
      raw += t.pitchLfo.value(true) / 256.0;
    // Driver pitch arithmetic wraps as a signed 16-bit value before the
    // chip's playable range is applied. Host transposition follows that step.
    const uint16_t word = uint16_t(int64_t(std::floor(raw)));
    const int signedPitch = word < 32768 ? int(word) : int(word) - 65536;
    int pitch64 = std::clamp(signedPitch + keyShift * 64, 0, 95 * 64 + 63);
    int semitone = pitch64 / 64;
    constexpr int key[12] = {0, 1, 2, 4, 5, 6, 8, 9, 10, 12, 13, 14};
    write(0x28 + static_cast<int>(ch),
          (semitone / 12) * 16 + key[semitone % 12]);
    write(0x30 + static_cast<int>(ch), (pitch64 % 64) * 4);
  }
  void off(size_t ch) {
    Track &t = tracks[ch];
    if (ch < 8)
      write(8, static_cast<int>(ch));
    pcmEvent(ch, 1);
    t.pcm = nullptr;
    t.held = false;
    t.pending = -1;
  }
    void hardwareLfo(size_t ch) {
        if (ch < 8) {
            auto &t = tracks[ch];
            write(0x38 + int(ch), t.hardwareEnabled ? t.hardwareSensitivity : 0);
        }
    }
  void on(size_t ch, int note) {
    Track &t = tracks[ch];
    if (ch < 8 && t.pendingVoice >= 0) {
      int id = t.pendingVoice;
      t.pendingVoice = -1;
      voice(ch, id);
    }
    bool retrigger = !t.held;
    bool changed = t.note != note || t.slide != 0;
    t.note = note;
    t.slide = 0;
    if (retrigger)
      t.age = 0;
    if (retrigger) {
      // MP square retains its last displacement at the note boundary; the
      // restarted counter applies the positive level on the following tick.
      t.pitchLfo.squareInitial = t.pitchLfo.value(true);
      t.pitchLfo.phase = 0;
      // MA with MD0 runs continuously across ordinary note retriggers.
      if (t.lfoDelay > 0) {
        t.volumeLfo.phase = 0;
        if (t.volumeLfo.wave == 3)
          t.volumeLfo.randomValue = std::max(0.0,
              -double(t.volumeLfo.delta) * t.volumeLfo.amplitudeScale);
      }
    }
    if (ch < 8) {
      if (changed || retrigger)
        pitch(ch);
      if (retrigger)
        volume(ch);
      if (retrigger && t.configured) {
        if (t.hardwareSync) {
          write(1, 2);
          write(1, 0);
        }
        int mask = t.instrument[2] & 15;
        write(8, static_cast<int>(ch) | (mask << 3));
      }
    } else {
      if (retrigger) {
        pcmEvent(ch, 2, t.pan);
        pcmEvent(ch, 3, uint32_t(std::llround(pcmVolume(t) * 65536)));
        pcmEvent(ch, 4, t.format);
        pcmEvent(ch, 0, uint32_t(t.bank * 96 + note), t.format);
      }
      if (options.skipPcmSynthesis) {
        t.pcm = nullptr;
        t.held = true;
        return;
      }
      if (!pdx) {
        warn("ERROR: PCM note without a PDX sample bank; PCM is silent while "
             "FM continues");
        t.pcm = nullptr;
        t.held = true;
        return;
      }
      int index = t.bank * 96 + note;
      int key = index * 8 + t.format;
      if (!decoded.count(key)) {
        std::vector<int16_t> samples;
        try {
          samples = pdx->decode(index, t.format);
        } catch (const std::exception &e) {
          warn("ERROR: " + detail(ch, e.what(),
                                  "existing PDX sample 0.." +
                                      std::to_string(pdx->entries.size() - 1),
                                  index, t.referencedOffset));
        }
        if (decodedSamples + samples.size() > 64 * 1024 * 1024)
          fail(ch, "Decoded PCM cache exceeds 128 MiB",
               "decoded cache at most 67108864 samples");
        decodedSamples += samples.size();
        decoded.emplace(key, std::move(samples));
      }
      if (retrigger) {
        t.pcm = &decoded.at(key);
        t.pcmPosition = 0;
        t.pitchShift.reset();
      }
    }
    t.held = true;
  }
  void lfo(size_t ch, Lfo &oscillator) {
    int wave = byte(ch);
    if (wave & 0x80) {
      oscillator.enabled = (wave & 1) != 0;
      if (oscillator.enabled) {
        oscillator.phase = 0;
        oscillator.squareInitial = 0;
        oscillator.randomValue = 0;
      }
      return;
    }
    // Bits 0..2 select waveform/scaling; other low-mode bits are unused.
    oscillator.wave = wave & 3;
    oscillator.amplitudeScale = (wave & 4) ? 256 : 1;
    oscillator.period = std::max(1, word(ch));
    oscillator.delta = signedWord(word(ch));
    oscillator.phase = 0;
    oscillator.squareInitial = 0;
    oscillator.randomValue = &oscillator == &tracks[ch].volumeLfo
        ? std::max(0.0, -double(oscillator.delta) * oscillator.amplitudeScale) : 0.0;
    oscillator.enabled = true;

  }
  void stepRandom(Lfo &oscillator, bool pitchLfo) {
    if (!oscillator.enabled || oscillator.wave != 3)
      return;
    const uint64_t phase = oscillator.phase;
    const bool due = pitchLfo ? phase > 0 && (phase - 1) % oscillator.period == 0
                             : phase > 0 && phase % oscillator.period == 0;
    if (!due)
      return;
    const int sample = randomLfo.next();
    const int depth = oscillator.delta * oscillator.amplitudeScale;
    oscillator.randomValue = pitchLfo
        ? double(sample * randomPitchDepth(depth))
        : double(randomAmplitude(sample, depth)) * 256;
  }
    void skipRepeat(size_t ch) {
        int depth = 0;
        for (int budget = 0; budget < 65536; ++budget) {
            int cmd = byte(ch);
            if (cmd < 128)
                continue;
            if (cmd < 224) {
                byte(ch);
                continue;
            }
            int count = 0;
            switch (cmd) {
            case 246:
                ++depth;
                count = 2;
                break;
            case 245:
                word(ch);
                if (depth-- == 0)
                    return;
                continue;
            case 255:
            case 253:
            case 252:
            case 251:
            case 248:
            case 240:
            case 239:
            case 237:
            case 233:
                count = 1;
                break;
            case 254:
            case 244:
            case 243:
            case 242:
                count = 2;
                break;
            case 250:
            case 249:
            case 247:
            case 238:
            case 232:
                break;
            case 236:
            case 235:
                count = (byte(ch) & 0x80) ? 0 : 4;
                break;
            case 234:
                count = (byte(ch) & 0x80) ? 0 : 4;
                break;
            case 231: {
                int ex = byte(ch);
                if (ex == 1 || ex == 3)
                    count = 1;
                else
                    fail(ch, "Unsupported extension inside repeat escape", "supported E6/E7 subcommand");
                break;
            }
            case 230: {
                int ex = byte(ch);
                if (ex == 1)
                    count = 2;
                else if (ex == 2 || ex == 3)
                    count = 1;
                else
                    fail(ch, "Unsupported extension inside repeat escape", "supported E6/E7 subcommand");
                break;
            }
            default:
                fail(ch, "Unterminated or malformed repeat escape",
                     "balanced F6/F5 repeat with valid escape");
            }
            while (count--)
                byte(ch);
        }
        fail(ch, "Repeat escape scan limit exceeded", "repeat end within 65536 scan steps");
    }
  void commands(size_t ch) {
    Track &t = tracks[ch];
    for (int budget = 0; budget < 8192; ++budget) {
      if (t.loops == 0)
        t.firstVisit.emplace(t.pc, nativeTime);
      int cmd = byte(ch);
      if (cmd <= 127) {
        t.sourceOffset = t.referencedOffset;
        off(ch);
        t.porta = 0; // A rest ends the preceding note's slide.
        t.wait = cmd + 1;
        return;
      }
      if (cmd <= 223) {
        t.sourceOffset = t.referencedOffset;
        int duration = byte(ch) + 1;
        t.wait = duration;
        bool tie = t.tieNext || t.noKeyOff;
        t.tieNext = false;
        t.gate = tie ? -1
                 : t.quantize < 128
                     ? std::max(1, duration * t.quantize / 8)
                     : std::max(1, duration + signedByte(t.quantize));
        t.porta = t.nextPorta;
        t.nextPorta = 0;
        if (t.keyDelay) {
          // Pitch is prepared at the note boundary;
          // voice programming and key-on follow at delay expiration.
          if (ch < 8) {
            t.note = cmd - 128;
            t.slide = 0;
            pitch(ch);
          }
          t.pending = cmd - 128;
          t.delay = t.keyDelay;
        } else
          on(ch, cmd - 128);
        return;
      }
      switch (cmd) {
      case 255:
        tempo = byte(ch);
        break;
      case 254: {
        int address = byte(ch), value = byte(ch);
        // y writes target the encoded absolute channel, including other tracks.
        if (address >= 0x20) {
          auto &target = tracks[address & 7];
          if (!target.configured)
            target.instrument[2] = 15;
          target.configured = true;
          if (address < 0x28) {
            target.instrument[1] = value & 63;
            target.pan = value >> 6;
          } else if (address >= 0x38 && address < 0x40) {
            // Direct writes do not replace MH's saved sensitivity.
          } else if (address >= 0x40) {
            int group = (address - 0x40) / 32, op = (address / 8) & 3;
            target.instrument[3 + group * 4 + op] = uint8_t(value);
          }
        }
        write(address, value);
        if (address == 0x12)
          tempo = value;
        break;
      }
      case 253: {
        int id = byte(ch);
        if (ch < 8 && song.hasVoice[id])
          t.pendingVoice = id;
        else
          voice(ch, id);
        break;
      }
      case 252:
        t.pan = byte(ch) & 3;
        pan(ch);
        break;
      case 251:
        t.volume = byte(ch);
        if (t.volume < 128 && t.volume > 15)
          fail(ch, "Unsupported coarse volume outside v0..v15",
               "0..15 or 128..255");
        volume(ch);
        break;
      case 250: // FA: lower volume (the older secondary format list reverses
                // this).
        t.volume = t.volume & 128 ? 128 | std::min(127, (t.volume & 127) + 1)
                                  : std::max(0, t.volume - 1);
        volume(ch);
        break;
      case 249: // F9: raise volume.
        t.volume = t.volume & 128 ? 128 | std::max(0, (t.volume & 127) - 1)
                                  : std::min(15, t.volume + 1);
        volume(ch);
        break;
      case 248:
        t.quantize = byte(ch);
        // Raw q is a byte; proportional values above eight are used by MDX
        // data.
        break;
      case 247:
        t.tieNext = true;
        break;
      case 246: {
        size_t counter = t.pc;
        int count = byte(ch);
        byte(ch);
        if (t.repeats.size() >= 64)
          badValue(ch, "Invalid repeat count or excessive repeat nesting",
                   count, "count 1..255; nesting below 64", counter);
        t.repeats.push_back({counter, t.pc, count ? count : 256});
        break;
      }
      case 245: {
        int offset = signedWord(word(ch));
        if (t.repeats.empty())
          fail(ch, "Repeat end without repeat start",
               "an active repeat start (F6)");
        auto &repeat = t.repeats.back();
        // F5 relative pointer addresses the mutable counter byte.
        if (offset >= 0)
          fail(ch, "Repeat end requires a backward offset",
               "signed offset -32768..-1");
        if (--repeat.left)
          jump(ch, static_cast<int64_t>(repeat.body));
        else
          t.repeats.pop_back();
        break;
      }
      case 244: {
        int offset = signedWord(word(ch));
        if (t.repeats.empty())
          fail(ch, "Repeat escape without repeat start",
               "an active repeat start (F6)");
        if (t.repeats.back().left == 1) {
          // Resolve the matching terminator structurally. This avoids
          // relying on the mutable-counter layout of the old driver.
          if (offset < 0)
            fail(ch, "Repeat escape requires a forward offset",
                 "signed offset 0..32767");
          skipRepeat(ch);
          t.repeats.pop_back();
        }
        break;
      }
      case 243:
        t.detune = signedWord(word(ch));
        pitch(ch);
        break;
      case 242:
        t.nextPorta = signedWord(word(ch));
        break;
      case 241: {
        int high = byte(ch);
        if (!high) {
          off(ch);
          t.ended = true;
          if (t.firstEnd < 0)
            t.firstEnd = double(nativeTime) / nativeRate;
          return;
        }
        int offset = signedWord(high * 256 + byte(ch));
        t.referencedValue = offset;
        t.referencedOffset = t.pc - 2;
        const int64_t target = static_cast<int64_t>(t.pc) + offset;
        auto found = t.firstVisit.find(size_t(target));
        if (t.loops > 0 && t.pc != t.loopJumpSource) {
          jump(ch, target);
          break;
        }
        // Relocation can go in either direction. Only returning to an
        // already executed address establishes the first song loop.
        if (t.loops == 0 && found == t.firstVisit.end()) {
          jump(ch, target);
          break;
        }
        if (t.loops == 0) {
          t.loopJumpSource = t.pc;
          t.loopEnd = double(nativeTime) / nativeRate;
          t.firstEnd = t.loopEnd;
          t.loopStart = double(found->second) / nativeRate;
        }
        ++t.loops;
        // Keep all channels running through the final fade. Ended
        // finite tracks are excluded from the global loop barrier.
        jump(ch, static_cast<int64_t>(t.pc) + offset);
        break;
      }
      case 240:
        t.keyDelay = byte(ch);
        break;
      case 239: {
        int destination = byte(ch);
        if (destination >= static_cast<int>(tracks.size())) {
          warn("ERROR: " +
               detail(ch, "Ignored sync signal outside the track table",
                      "channel 0.." + std::to_string(tracks.size() - 1),
                      destination, t.referencedOffset));
          break;
        }
        tracks[destination].signal = true;
        break;
      }
      case 238:
        if (t.signal)
          t.signal = false;
        else {
          t.waiting = true;
          return;
        }
        break;
      case 237: {
        int value = byte(ch);
        if (ch < 8)
          write(15, value);
        else {
          if (value > 6)
            fail(ch, "Unsupported PCM frequency/format (F0..F6 supported)",
                 "0..6");
          t.format = value;
          pcmEvent(ch, 4, value);
          if (value >= 5)
            warn("F5/F6 linear PCM extension: interpreted as big-endian P16 / "
                 "signed P8 at 15625 "
                 "Hz.");
        }
        break;
      }
      case 236:
        lfo(ch, t.pitchLfo);
        pitch(ch);
        break;
      case 235:
        lfo(ch, t.volumeLfo);
        volume(ch);
        break;
      case 234: {
        int mode = byte(ch);
        if (mode & 0x80) {
          if (ch >= 8)
            break; // Consume PCM-track MH switches without touching OPM.
          t.hardwareEnabled = (mode & 1) != 0;
          hardwareLfo(ch);
        } else {
          int frequency = byte(ch), pm = byte(ch), am = byte(ch),
              sensitivity = byte(ch);
          if (ch >= 8)
            break; // All operands consumed; PCM tracks cannot program OPM LFO.
          t.hardwareEnabled = true;
          t.hardwareSync = (mode & 64) != 0;
          t.hardwareSensitivity = sensitivity & 0x73;
          write(0x1b, mode & 3);
          write(0x18, frequency);
          // MH operands already carry the PMD/AMD register-bank bit.
          // Preserve both bytes exactly, including intentional same-bank writes.
          write(0x19, pm);
          write(0x19, am);
          // Explicit MH settings take effect immediately and are independent of
          // MD.
          write(0x38 + int(ch), t.hardwareSensitivity);
        }
        break;
      }
      case 233:
        t.lfoDelay = byte(ch); // MD affects MP/MA only.
        break;
      case 232:
        if (tracks.size() != 16)
          badValue(ch, "PCM8 declaration requires a 16-track header",
                   tracks.size(), "16 tracks", t.referencedOffset);
        break;
      case 231: {
        int extension = byte(ch);
        if (extension == 1) {
          int speed = byte(ch);
          startFade(std::max(0.1, speed * (256 - tempo) * 0.000256 * 64));
          warn("Embedded fade speed is approximated; use the player's fade "
               "option for a defined "
               "duration.");
        } else if (extension == 3) {
          t.noKeyOff = byte(ch) != 0;
        } else
          fail(ch, "Unsupported E7 extension " + std::to_string(extension),
               "1 or 3");
        break;
      }
      case 230: {
        int extension = byte(ch);
        if (extension == 1)
          t.detune = signedWord((t.detune + signedWord(word(ch))) & 65535);
        else if (extension == 2)
          t.transpose = signedByte(byte(ch));
        else if (extension == 3)
          t.transpose =
              std::clamp(t.transpose + signedByte(byte(ch)), -255, 255);
        else
          fail(ch, "Unsupported E6 extension " + std::to_string(extension),
               "1..3");
        pitch(ch);
        break;
      }
      default:
        fail(ch, "Unsupported MDX opcode " + std::to_string(cmd),
             "0..223 (rest/note), 230..255 (supported commands)");
      }
    }
    fail(ch, "Too many zero-time commands (possible cyclic/corrupt sequence)",
         "at most 8192 zero-time commands");
  }
    void startFade(double length) {
        if (fadeStart >= 0)
            return;
        fadeStart = static_cast<double>(nativeTime) / nativeRate;
        fadeLength = length;
        if (length == 0)
            done = true;
    }
  void tick() {
    for (size_t ch = 0; ch < tracks.size(); ++ch) {
      Track &t = tracks[ch];
      if (!t.ended && t.gate > 0 && --t.gate == 0) {
        int pending = t.pending;
        off(ch);
        t.pending = pending; // A delayed note due on this tick still starts.
      }
      if (!t.ended && t.pending >= 0 && --t.delay <= 0) {
        int note = t.pending;
        t.pending = -1;
        on(ch, note);
      }
      if (t.ended)
        continue;
      // Keep full-note portamento independent of the software LFO gate.
      const bool sliding = t.wait > 0 && t.pending < 0 && t.porta != 0;
      if (sliding)
        t.slide = std::clamp(t.slide + t.porta / 256.0, -65536.0, 65536.0);
      // Logical modulation continues through the release and rests until
      // disabled.
      if (t.held || t.pitchLfo.enabled || t.volumeLfo.enabled) {
        if (t.age < 0x7fffffff)
          ++t.age;
        if (t.age >= t.lfoDelay) {
          ++t.pitchLfo.phase;
          ++t.volumeLfo.phase;
          stepRandom(t.pitchLfo, true);
          stepRandom(t.volumeLfo, false);
        }
        if (sliding || t.pitchLfo.enabled)
          pitch(ch);
        if (t.volumeLfo.enabled)
          volume(ch);
      } else if (sliding) {
        pitch(ch);
      }
      if (t.waiting) {
        if (!t.signal)
          continue;
        t.waiting = false;
        t.signal = false;
      }
      if (t.wait > 0 && --t.wait > 0)
        continue;
      commands(ch);
    }
    bool allEnded = true, allLooped = true, allBlocked = true;
    for (auto &t : tracks)
      if (!t.ended) {
        allEnded = false;
        if (options.loops == 0 || t.loops < uint64_t(options.loops))
          allLooped = false;
        if (!t.waiting || t.signal)
          allBlocked = false;
      }
    if (allEnded && releaseRemaining < 0 && options.repeatSelection && !options.analysisOnly &&
        nativeTime > selectionCycleStart) {
      // Keep chip/audio time monotonic, but restore sequencer state for the
      // selected excerpt. Ended shorter tracks wait until this shared boundary.
      selectionCycleStart = nativeTime;
      selectionScoreStart = scoreTime;
      selectionRestarted = true;
      tempo = 200;
      randomLfo = SoftwareLfoRandom{};
      for (size_t ch = 0; ch < tracks.size(); ++ch) {
        tracks[ch] = Track{};
        tracks[ch].pc = song.tracks[ch];
      }
      // Restore global modulation/noise defaults without resetting envelopes,
      // the output backend, sample caches, live edits, or audio buffers.
      write(1, 0);
      write(0x0f, 0);
      write(0x18, 0);
      write(0x19, 0);
      write(0x19, 128);
      write(0x1b, 0);
      tick();
      return;
    }
    if (allEnded && releaseRemaining < 0)
      releaseRemaining =
          nativeRate / 2 +
          (options.externalFm
               ? std::max(options.fmDelayMs, options.pcmDelayMs) * nativeRate /
                     1000
               : (options.fmBackend == 1 ? 2048 : 0));
    if (!allEnded && allLooped)
      startFade(options.fadeSeconds);
    if (!allEnded && allBlocked)
      for (size_t ch = 0; ch < tracks.size(); ++ch)
        if (!tracks[ch].ended)
          fail(ch,
               "All active MDX tracks are waiting for sync; playback cannot "
               "continue",
               "at least one runnable track or pending sync signal");
  }
    std::vector<Stereo> pcmDelay;
    size_t pcmDelayCursor = 0;
    AudioStem nativeSample() {
        if (done)
            return {};
        for (unsigned ch = 0; ch < 8; ++ch)
            if (deferredKeyOn[ch] >= 0 && lastKeyOff[ch] < nativeTime) {
                int value = deferredKeyOn[ch];
                deferredKeyOn[ch] = -1;
                send(8, value);
            }
        if (samplesToTick <= 0) {
            const int interval = (256 - tempo) * 16;
            tick();
            samplesToTick += selectionRestarted ? (256 - 200) * 16 : interval;
            selectionRestarted = false;
        }
        double speed = playbackSpeed;
        samplesToTick -= speed;
        scoreTime += speed;
        if (!reconstructing && !options.skipFmSynthesis)
            independent.advance(64);
        auto sample = independent.last_sample();
        AudioStem result;
        result.fm = options.externalFm ? Stereo{}
                                       : Stereo{sample.left * 32767.0 * independentGain,
                                                sample.right * 32767.0 * independentGain};
        const bool capture = !reconstructing && options.monitor && nativeTime % 8 == 0;
        if (capture) {
            constexpr int notes[16] = {0, 1, 2, 2, 3, 4, 5, 5, 6, 7, 8, 8, 9, 10, 11, 11};
            for (unsigned ch = 0; ch < 8; ++ch) {
                if (captureWaves)
                {
                    auto frame = independent.channel_sample(ch);
                    monitorFrame.waveform[ch][monitorFrame.cursor] =
                        float((frame.left + frame.right) * .5 * independentGain * options.gain);
                }
                bool active = false;
                for (unsigned slot = 0; slot < 4; ++slot)
                    active |= independent.inspect(ch, slot).key;
                int kc = registers[0x28 + ch];
                monitorFrame.note[ch] = active ? std::min(95, (kc >> 4) * 12 + notes[kc & 15]) : -1;
            }
            for (unsigned ch = 8; ch < 16; ++ch) {
                if (captureWaves)
                {
                    monitorFrame.waveform[ch][monitorFrame.cursor] = 0;
                }
                monitorFrame.note[ch] = -1;
            }
        }
        constexpr double rates[] = {3906.25, 4000000.0 / 768, 7812.5, 8000000.0 / 768, 15625, 15625, 15625};
        for (size_t i = 8; i < tracks.size(); ++i) {
            auto &t = tracks[i];
            if (!t.pcm || t.pcmPosition >= t.pcm->size())
                continue;
            double value = reconstructing ? 0 : t.pitchShift.read(*t.pcm, t.pcmPosition, keyShift, keyMode);
            value *= pcmVolume(t);
            // Gate the mixer only; advance muted samples on the same clock.
            if (pcmMuteMask & (1u << (i - 8)))
                value = 0;
            if (capture) {
                if (captureWaves)
                {
                    monitorFrame.waveform[i][monitorFrame.cursor] = float(
                        value / 32768.0 * options.gain * options.pcmGain * ((t.pan & 1) + bool(t.pan & 2)) * .5);
                }
                monitorFrame.note[i] = t.note;
            }
            if (t.pan & 1)
                result.pcm.left += value;
            if (t.pan & 2)
                result.pcm.right += value;
            t.pcmPosition += rates[t.format] * (keyMode == 1 ? std::exp2(keyShift / 12.0) : 1.0) / nativeRate;
        }
        if (capture)
            monitorFrame.cursor = (monitorFrame.cursor + 1) % MonitorFrame::points;
        double now = static_cast<double>(nativeTime) / nativeRate;
        if (options.maxSeconds > 0 && now >= std::max(0.0, options.maxSeconds - options.fadeSeconds))
            startFade(std::min(options.fadeSeconds, options.maxSeconds));
        double gain = options.gain, fade = 1;
        if (fadeStart >= 0) {
            double fraction = fadeLength > 0 ? (now - fadeStart) / fadeLength : 1;
            fade = std::clamp(1.0 - fraction, 0.0, 1.0);
            gain *= fade;
            if (fraction >= 1)
                done = true;
        }
        if (capture && options.gain > 0)
            for (auto &channel : monitorFrame.waveform)
                channel[(monitorFrame.cursor + MonitorFrame::points - 1) % MonitorFrame::points] *=
                    float(gain / options.gain);
        nativeFmScale=32767.0*independentGain*gain;
        result.fm.left *= gain;
        result.fm.right *= gain;
        result.pcm.left *= fade;
        result.pcm.right *= fade;
        size_t delay = options.externalFm ? size_t(std::clamp(options.pcmDelayMs, 0, 2000)) * 62500 / 1000
                                          : (options.fmBackend == 1 ? 2048 : 0);
        if (delay && !reconstructing) {
            if (pcmDelay.size() != delay)
                pcmDelay.assign(delay, {});
            std::swap(result.pcm, pcmDelay[pcmDelayCursor]);
            pcmDelayCursor = (pcmDelayCursor + 1) % delay;
        }
        result.pcm.left *= options.gain * options.pcmGain;
        result.pcm.right *= options.gain * options.pcmGain;
        if (releaseRemaining >= 0) {
            if (options.loops == 0 && !options.analysisOnly && !reconstructing) {
                // Inspect every channel separately: mixing can cancel non-silent signals.
                bool quiet = true;
                for (unsigned ch=0;ch<8;++ch) {
                    const auto value=independent.channel_sample(ch);
                    quiet &= std::abs(value.left)<1.0/32768 && std::abs(value.right)<1.0/32768;
                }
                for(size_t ch=8;ch<tracks.size();++ch)
                    quiet &= !tracks[ch].pcm || tracks[ch].pcmPosition>=tracks[ch].pcm->size();
                quietSamples = quiet ? quietSamples+1 : 0;
                // Drain configured output delay and require sustained silence, not a zero crossing.
                const unsigned drain=unsigned(options.externalFm ?
                    std::max(options.fmDelayMs,options.pcmDelayMs)*nativeRate/1000 :
                    (options.fmBackend==1 ? 2048 : 0));
                if(quietSamples >= unsigned(nativeRate/10)+drain) done=endedSilently=true;
            } else if (--releaseRemaining <= 0) done = true;
        }
        ++nativeTime;
        if (options.maxSeconds > 0 && nativeTime >= static_cast<uint64_t>(options.maxSeconds * nativeRate))
            done = true;
        return result;
    }
};
Player::Player(Song song, std::shared_ptr<Pdx> pdx, Options options)
    : p(std::make_unique<Impl>(*this, std::move(song), std::move(pdx), options)) {}
Player::~Player() = default;
NativeBlock Player::renderNativeBlock(size_t count)
{
    NativeBlock block;
    block.frames.reserve(count);
    uint64_t start = p->nativeTime;
    registerTrace  = [&](uint64_t sample, int address, int value)
    { block.events.push_back({uint32_t((sample - start) * 64), uint32_t(address), uint32_t(value)}); };
    while (block.frames.size() < count && !p->done)
    {
        auto stem = p->nativeSample();
        block.frames.push_back({stem.pcm, p->nativeFmScale, uint8_t(p->options.offlineChannel>=0 ? p->registers[0x20+p->options.offlineChannel] : 0)});
    }
    registerTrace = {};
    return block;
}
namespace
{
class OfflineSource final : public Source
{
    Player                             sequencer;
    OpmApi                            &api;
    iopm_handle                        chip = nullptr;
    std::thread                        sequenceThread;
    std::thread                        synthesisThread;
    std::mutex                         mutex;
    std::condition_variable            changed;
    std::deque<NativeBlock>            nativeBlocks;
    std::deque<std::vector<AudioStem>> audioBlocks;
    std::exception_ptr                 failure;
    bool                               cancelled     = false;
    bool                               sequenceEnded = false;
    bool                               ended         = false;
    std::vector<AudioStem>             stems;
    size_t                             cursor = 0;
    Clock64Output                      output;
    Options                            options;
    uint64_t                           count = 0;

    static Options sequenceOptions(Options options)
    {
        options.offlineFM = true;
        return options;
    }
    void fail()
    {
        std::lock_guard<std::mutex> lock(mutex);
        failure   = std::current_exception();
        cancelled = true;
        changed.notify_all();
    }
    void sequence()
    {
        try
        {
            for (;;)
            {
                {
                    std::unique_lock<std::mutex> lock(mutex);
                    changed.wait(lock, [&] { return cancelled || (nativeBlocks.size() < 2); });
                    if (cancelled)
                    {
                        return;
                    }
                }
                auto                        block = sequencer.renderNativeBlock(8192);
                std::lock_guard<std::mutex> lock(mutex);
                if (block.frames.empty())
                {
                    sequenceEnded = true;
                    changed.notify_all();
                    return;
                }
                nativeBlocks.push_back(std::move(block));
                changed.notify_all();
            }
        }
        catch (...)
        {
            fail();
        }
    }
    void synthesize()
    {
        try
        {
            for (;;)
            {
                NativeBlock block;
                {
                    std::unique_lock<std::mutex> lock(mutex);
                    changed.wait(lock,
                                 [&]
                                 {
                                     return cancelled ||
                                            (!nativeBlocks.empty() && (audioBlocks.size() < 2)) ||
                                            (sequenceEnded && nativeBlocks.empty());
                                 });
                    if (cancelled)
                    {
                        return;
                    }
                    if (nativeBlocks.empty())
                    {
                        ended = true;
                        changed.notify_all();
                        return;
                    }
                    block = std::move(nativeBlocks.front());
                    nativeBlocks.pop_front();
                    changed.notify_all();
                }
                std::vector<float>      samples(block.frames.size() * 2);
                std::vector<iopm_event> events;
                events.reserve(block.events.size());
                for (const auto &event : block.events)
                {
                    uint32_t value=event.value;
                    events.push_back({event.clock_offset, event.address, value});
                }
                std::vector<float> channels;
                if (options.offlineChannel>=0) channels.resize(block.frames.size()*16);
                if (chip && options.offlineChannel>=0)
                    OpmApi::check(api.render_events_channels_f32(chip,samples.data(),channels.data(),uint32_t(block.frames.size()),events.data(),uint32_t(events.size())));
                else if (chip)
                {
                    OpmApi::check(api.render_events_f32(chip, samples.data(), uint32_t(block.frames.size()),
                                                        events.data(), uint32_t(events.size())));
                }
                std::vector<AudioStem> ready(block.frames.size());
                for (size_t i = 0; i < ready.size(); ++i)
                {
                    const auto &frame = block.frames[i];
                    if(options.offlineChannel>=0) {
                        const auto index=i*16+options.offlineChannel*2;
                        samples[i*2]=(frame.pan&64) ? channels[index] : 0;
                        samples[i*2+1]=(frame.pan&128) ? channels[index+1] : 0;
                    }
                    ready[i]          = {{samples[i * 2] * frame.fmScale, samples[i * 2 + 1] * frame.fmScale},
                                         frame.pcm};
                }
                std::lock_guard<std::mutex> lock(mutex);
                audioBlocks.push_back(std::move(ready));
                changed.notify_all();
            }
        }
        catch (...)
        {
            fail();
        }
    }
    bool refill()
    {
        std::unique_lock<std::mutex> lock(mutex);
        changed.wait(lock, [&] { return failure || !audioBlocks.empty() || ended; });
        if (failure)
        {
            std::rethrow_exception(failure);
        }
        if (audioBlocks.empty())
        {
            return false;
        }
        stems = std::move(audioBlocks.front());
        audioBlocks.pop_front();
        cursor = 0;
        changed.notify_all();
        return true;
    }
    void join()
    {
        {
            std::lock_guard<std::mutex> lock(mutex);
            cancelled = true;
            changed.notify_all();
        }
        if (sequenceThread.joinable())
        {
            sequenceThread.join();
        }
        if (synthesisThread.joinable())
        {
            synthesisThread.join();
        }
    }

  public:
    OfflineSource(Song song, std::shared_ptr<Pdx> bank, Options opts)
        : sequencer(std::move(song), std::move(bank), sequenceOptions(opts)), api(OpmApi::get()),
          options(opts)
    {
        if (opts.fmMuteMask != 255)
        {
            OpmApi::check(api.create(4000000, 62500, &chip));
        }
        try
        {
            if (chip)
            {
                OpmApi::check(api.set_measured_output_timing(chip, opts.measuredOutputTiming));
            }
            sequenceThread  = std::thread([this] { sequence(); });
            synthesisThread = std::thread([this] { synthesize(); });
        }
        catch (...)
        {
            join();
            api.destroy(chip);
            chip = nullptr;
            throw;
        }
    }
    ~OfflineSource()
    {
        join();
        if (chip)
        {
            api.destroy(chip);
        }
    }
    size_t render(int16_t *target, size_t frames) override
    {
        size_t n = output.render(target, frames, options.sampleRate, options.dacRightDelay,
                                 [&](AudioStem &stem)
                                 {
                                     if ((cursor >= stems.size()) && !refill())
                                     {
                                         return false;
                                     }
                                     stem = stems[cursor++];
                                     return true;
                                 });
        count += n;
        return n;
    }
    double seconds() const override
    {
        return double(count) / options.sampleRate;
    }
};
} // namespace
std::unique_ptr<Source> makeOfflineSource(Song song, std::shared_ptr<Pdx> pdx, Options options)
{
    if (options.loops <= 0 || options.externalFm || options.fmBackend != 0 || !options.resampleOutput)
    {
        throw std::runtime_error("Invalid offline render options");
    }
    return std::make_unique<OfflineSource>(std::move(song), std::move(pdx), options);
}
size_t Player::render(int16_t *out, size_t frames) {
    if(p->options.liveLoops && p->fadeStart < 0)
        p->options.loops=std::clamp(p->options.liveLoops->load(),0,1000);
    p->playbackSpeed=p->options.liveSpeed ? std::clamp(p->options.liveSpeed->load(),0,200)/100.0 : 1.0;
    bool waves=p->options.monitor && p->options.monitor->waveforms.load(std::memory_order_relaxed);
    if(!waves && p->captureWaves) for(auto &channel:p->monitorFrame.waveform) channel.fill(0);
    p->captureWaves=waves;
    p->independent.set_monitor_enabled(waves || ((p->options.loops == 0 || p->options.liveLoops) && !p->options.analysisOnly));
    struct Publish {
        Impl *p;
        ~Publish() {
            if (p->reconstructing)
                return;
            if (p->options.voiceState) {
                std::lock_guard<std::mutex> lock(p->options.voiceState->mutex);
                auto &state = *p->options.voiceState;
                state.registers = p->registers;
                state.pdx = p->pdx;
                for (size_t ch = 8; ch < p->tracks.size(); ++ch) {
                    state.pcmBank[ch - 8] = p->tracks[ch].bank;
                    state.pcmFormat[ch - 8] = p->tracks[ch].format;
                }
                for (size_t ch = 0; ch < 8 && ch < p->tracks.size(); ++ch)
                    if (p->tracks[ch].configured) {
                        state.voices[ch] = p->tracks[ch].instrument;
                        state.valid[ch] = true;
                        state.ids[ch] = p->tracks[ch].voice;
                        if (state.ids[ch] >= 0)
                            state.used[size_t(state.ids[ch])] = true;
                    }
            }
            if (!p->options.monitor)
                return;
            std::unique_lock<std::mutex> lock(p->options.monitor->mutex, std::try_to_lock);
            if (lock.owns_lock()) {
                for (size_t ch = 0; ch < p->tracks.size(); ++ch)
                {
                    p->monitorFrame.sourceOffset[ch] = 0;
                    if (!p->tracks[ch].ended)
                    {
                        p->monitorFrame.sourceOffset[ch] = p->tracks[ch].sourceOffset;
                    }
                }
                p->monitorFrame.seconds = double(p->outputTime) / p->options.sampleRate;
                p->options.monitor->frame = p->monitorFrame;
            }
        }
    } publish{p.get()};
    if (!p->reconstructing && p->options.voiceState)
        for (int ch = 0; ch < 8; ++ch) {
            int id = p->options.voiceState->requestedVoice[ch].exchange(-1);
            if (id >= 0 && id < 256) {
                {std::lock_guard<std::mutex> lock(p->options.voiceState->mutex);
                 if(p->options.voiceState->defined[id]) {
                     p->song.voices[id]=p->options.voiceState->definitions[id];p->song.hasVoice[id]=true;
                 }}
                if(p->song.hasVoice[id]) p->voice(ch,id);
            }
        }
    if (p->options.liveKeyMode)
        p->keyMode = std::clamp(p->options.liveKeyMode->load(), 0, 2);
    if (p->options.liveKeyShift) {
        int shift = std::clamp(p->options.liveKeyShift->load(), -24, 24);
        if (shift != p->keyShift) {
            p->keyShift = shift;
            for (size_t ch = 0; ch < 8; ++ch)
                p->pitch(ch);
        }
    }
    if (p->options.liveMeasuredOutputTiming)
        p->independent.set_measured_output_timing(
            p->options.liveMeasuredOutputTiming->load(std::memory_order_relaxed));
    if (p->options.livePcmMuteMask)
        p->pcmMuteMask = p->options.livePcmMuteMask->load(std::memory_order_relaxed) & 255;
    if (p->options.liveMuteMask) {
        uint32_t mask = p->options.liveMuteMask->load(std::memory_order_relaxed) & 255;
        if (mask != p->muteMask) {
            uint32_t released = p->muteMask & ~mask;
            p->muteMask = mask;
            for (int ch = 0; ch < 8; ++ch) {
                p->send(0x20 + ch, p->registers[0x20 + ch]);
                if ((released & (1u << ch)) && p->options.registerOutput) {
                    for (int a = 0x28 + ch; a < 256; a += 8)
                        p->options.registerOutput(p->nativeTime, a, p->registers[a]);
                    auto &t = p->tracks[ch];
                    p->options.registerOutput(p->nativeTime, 8,
                                              ch | (t.held ? (t.instrument[2] & 15) << 3 : 0));
                }
            }
        }
    }
    if (p->options.liveGain)
        p->options.gain = std::clamp(p->options.liveGain->load(), 0.0, 4.0);
    if (p->options.livePcmGain)
        p->options.pcmGain = std::clamp(p->options.livePcmGain->load(), 0.0, 4.0);
    if (p->options.liveIndependentGain) {
        double gain = p->options.liveIndependentGain->load(std::memory_order_relaxed);
        if (std::isfinite(gain))
            p->independentGain = std::clamp(gain, 0.0, 8.0);
    }
    if (p->options.resampleOutput) {
        bool dac =
            p->options.liveDacRightDelay ? p->options.liveDacRightDelay->load() : p->options.dacRightDelay;
        size_t n = p->outputStage.render(out, frames, p->options.sampleRate, dac, [&](AudioStem &stem) {
            if (p->done)
                return false;
            stem = p->nativeSample();
            return true;
        });
        p->outputTime += n;
        return n;
    }
    size_t n = 0;
    const double width = static_cast<double>(nativeRate) / p->options.sampleRate;
    for (; n < frames && (!p->done || p->sourceRemaining > 1e-9); ++n) {
        Stereo sum;
        double remaining = width;
        while (remaining > 1e-9) {
            if (p->sourceRemaining <= 1e-9) {
                if (p->done)
                    break;
                p->current = p->nativeSample().mixed();
                p->sourceRemaining = 1;
            }
            double amount = std::min(remaining, p->sourceRemaining);
            sum.left += amount * p->current.left;
            sum.right += amount * p->current.right;
            remaining -= amount;
            p->sourceRemaining -= amount;
        }
        out[n * 2] = static_cast<int16_t>(std::clamp(std::lround(sum.left / width), -32768L, 32767L));
        out[n * 2 + 1] = static_cast<int16_t>(std::clamp(std::lround(sum.right / width), -32768L, 32767L));
        ++p->outputTime;
    }

    return n;
}
Timeline Player::recordedTimeline() const {
  Timeline result;
  for (const auto &t : p->tracks) {
    result.duration = std::max(result.duration, t.firstEnd);
    if (t.loopEnd > result.loopEnd) {
      result.loopStart = t.loopStart;
      result.loopEnd = t.loopEnd;
    }
  }
  if (result.loopEnd < result.duration) result.loopStart = result.loopEnd = 0;
  return result;
}
Timeline Player::analyze(const std::function<bool()> &cancelled, bool captureNotes) {
    auto opts = p->options;
    opts.analysisOnly = true;
    opts.monitor.reset();
    opts.voiceState.reset();
    opts.registerOutput = {};
    opts.loops = 0;
    opts.maxSeconds = 0;
    Player scan(p->song, p->pdx, opts);
    Timeline result;
    result.keyShift = opts.keyShift;
    std::array<int, 16> open;
    open.fill(-1);
    std::array<std::size_t, 16> offsets{};
    while (scan.p->nativeTime < uint64_t(nativeRate) * 3600) {
        if (cancelled && cancelled())
            return {};
        const int selectionInterval = (256 - scan.p->tempo) * 16;
        scan.p->tick();
        if (captureNotes)
        {
            const double now = double(scan.p->nativeTime) / nativeRate;
            for (std::size_t channel = 0; channel < scan.p->tracks.size(); ++channel)
            {
                const auto& track = scan.p->tracks[channel];
                int note = -1;
                if (track.held && track.firstEnd<0)
                {
                    note = track.note;
                    if (channel < 8)
                    {
                        constexpr int keyMap[] = {0, 1, 2, 2, 3, 4, 5, 5, 6, 7, 8, 8, 9, 10, 11, 11};
                        const auto key = scan.p->registers[0x28 + channel];
                        note = (key >> 4) * 12 + keyMap[key & 15];
                    }
                }
                const auto carriers = carrierNotes(note, scan.p->registers, static_cast<int>(channel), true);
                const int previous = open[channel];
                if
                (
                       (previous >= 0)
                    && (
                              (note != result.notes[previous].note)
                           || (carriers != result.notes[previous].carriers)
                           || (offsets[channel] != track.sourceOffset)
                       )
                )
                {
                    result.notes[previous].end = now;
                    open[channel] = -1;
                }
                if ((note >= 0) && (open[channel] < 0))
                {
                    ScoreNote item;
                    item.begin = now;
                    item.channel = static_cast<int>(channel);
                    item.note = note;
                    item.carriers = carriers;
                    open[channel] = static_cast<int>(result.notes.size());
                    result.notes.push_back(item);
                }
                offsets[channel] = track.sourceOffset;
            }
        }
        bool finished = true;
        for (const auto &t : scan.p->tracks)
            if (t.firstEnd < 0)
                finished = false;
        if (finished)
            break;
        scan.p->nativeTime += opts.repeatSelection ? selectionInterval : (256 - scan.p->tempo) * 16;
    }
    for (size_t ch = 0; ch < scan.p->tracks.size(); ++ch) {
        const auto &t = scan.p->tracks[ch];
        double end = t.firstEnd;
        if (end < 0) {
            result.truncated = true;
            end = double(scan.p->nativeTime) / nativeRate;
        }
        result.trackLoopStart[ch]=t.loopStart;
        result.trackLoopEnd[ch]=t.loopEnd;
        result.duration = std::max(result.duration, end);
        if (t.loopEnd > result.loopEnd) {
            result.loopEnd = t.loopEnd;
            result.loopStart = t.loopStart;
            result.channel = int(ch);
        }
    }
    for (int index : open)
    {
        if (index >= 0)
        {
            result.notes[index].end = result.duration;
        }
    }
    // A short auxiliary loop must not wrap the transport while longer
    // finite tracks are still playing. Use a loop only at the song boundary.
    if (result.loopEnd < result.duration) {
        result.loopStart = result.loopEnd = 0;
    }
    if (opts.repeatSelection && !result.truncated && result.duration > 0) {
        result.loopStart = 0;
        result.loopEnd = result.duration;
        result.trackLoopStart.fill(0);
        result.trackLoopEnd.fill(result.duration);
    }
    if (!result.loopEnd && !opts.repeatSelection)
        result.duration += .5;
    return result;
}
bool Player::seek(double target, const std::function<bool()> &cancelled) {
    if (!std::isfinite(target) || target < 0 || target > 3600)
        throw std::runtime_error("Seek time outside 0..3600 seconds");
    auto opts = p->options;
    auto fresh = std::make_unique<Impl>(*this, p->song, p->pdx, opts);
    p = std::move(fresh);
    p->reconstructing = opts.fastSeek;
    p->options.liveSpeed.reset(); // Seek uses score seconds, independent of current tempo multiplier.
    auto output = p->options.registerOutput;
    auto trace = registerTrace;
    p->options.registerOutput = {};
    registerTrace = {};
    std::array<int16_t, 4096> buffer{};
    uint64_t remaining = uint64_t(target * p->options.sampleRate);
    try {
        if (opts.fastSeek) {
            uint64_t end = uint64_t(target * nativeRate);
            while (p->nativeTime < end && !p->done) {
                if ((p->nativeTime % 2048) == 0 && cancelled && cancelled())
                    break;
                p->nativeSample();
            }
            p->outputTime = uint64_t(double(p->nativeTime) * opts.sampleRate / nativeRate);
            remaining = 0;
        }
        while (remaining && !(cancelled && cancelled())) {
            size_t n = render(buffer.data(), size_t(std::min<uint64_t>(remaining, 2048)));
            if (!n)
                break;
            remaining -= n;
        }
    } catch (...) {
        p->options.registerOutput = output;
        p->options.liveSpeed = opts.liveSpeed;
        registerTrace = trace;
        throw;
    }
    p->options.registerOutput = output;
    p->options.liveSpeed = opts.liveSpeed;
    registerTrace = trace;
    p->reconstructing = false;
    if (opts.fastSeek)
        syncRegisters();
    return !(cancelled && cancelled());
}
void Player::syncRegisters() {
    p->send(0x19, p->amd);
    p->send(0x19, p->pmd | 128);
    for (int a = 0; a < 256; ++a)
        if (a != 8 && a != 0x14 && a != 0x19)
            p->send(a, p->registers[a]);
    for (int ch = 0; ch < 8; ++ch)
        p->send(8, ch | (p->tracks[ch].held ? (p->tracks[ch].instrument[2] & 15) << 3 : 0));
}
double Player::position(const Timeline &t) const {
    double time = (p->scoreTime - (p->options.repeatSelection ? p->selectionScoreStart : 0)) / nativeRate;
    if (t.loopEnd > t.loopStart && time >= t.loopEnd)
        time = t.loopStart + std::fmod(time - t.loopStart, t.loopEnd - t.loopStart);
    return std::min(time, t.duration);
}
bool Player::unlimited() const { return p->options.loops == 0 && p->fadeStart < 0; }
bool Player::silentEnd() const { return p->endedSilently; }
double Player::seconds() const { return static_cast<double>(p->outputTime) / p->options.sampleRate; }
const std::vector<std::string> &Player::warnings() const { return p->notices; }
} // namespace mdx
