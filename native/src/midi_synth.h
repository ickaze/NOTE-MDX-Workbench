#pragma once
#include "opm_backend.h"
#include "player.h"
#include <algorithm>
#include <cmath>
#include <deque>
namespace mdx {
struct MidiQueue {
    std::mutex mutex;
    std::deque<uint32_t> messages;
    void push(uint32_t message) {
        std::lock_guard<std::mutex> lock(mutex);
        if (messages.size() >= 4096) {
            messages.clear();
            for (int ch = 0; ch < 16; ++ch)
                messages.push_back(0xb0 | ch | (120 << 8));
        }
        messages.push_back(message);
    }
    void clear() {
        std::lock_guard<std::mutex> lock(mutex);
        messages.clear();
    }
};
// One monophonic voice per MIDI channel: 1..8=FM A..H, 9..16=PCM P..W.
// Dedicated voices intentionally bypass the MDX channel masks.
class MidiSynth {
    OpmBackend chip;
    std::shared_ptr<Monitor> waveformView;
    MonitorFrame waveformFrame;
    uint64_t waveformSamples = 0;
    bool waveformExclusive = false;
    std::shared_ptr<VoiceState> state;
    std::shared_ptr<MidiQueue> queue;
    std::array<int, 16> note, velocity{}, volume{}, expression{}, bend{}, pressure{};
    std::array<int, 8> programs;
    std::array<int, 16> banks{};
    uint64_t catalogRevision = 0;
    std::array<bool, 16> sustain{}, released{};
    std::array<std::array<uint8_t, 27>, 8> voices{};
    std::array<std::vector<int16_t>, 8> pcm;
    std::array<double, 8> position{}, pcmRate{};
    std::array<PcmPitchShift, 8> shifters;
    std::function<void(int, int)> output;
    bool external, polyphonic = false;
    std::array<int, 16> owner;
    std::array<uint64_t, 16> order{};
    uint64_t serial = 0;
    bool aftertouch = false;
    std::array<std::array<bool, 128>, 16> pressed{};
    std::array<int, 16> inputVolume{}, inputExpression{}, inputBend{}, inputPressure{};
    std::array<bool, 16> inputSustain{};
    void route(uint32_t msg) {
        // Private viewer events carry their allocation mode; external MIDI keeps
        // the existing playback/paused policy. Restore the policy on every exit.
        struct RestoreMode {
            bool &value;
            bool saved;
            ~RestoreMode() { value = saved; }
        } restore{polyphonic, polyphonic};
        if (msg & 0x80000000u)
            polyphonic = (msg & 0x40000000u) != 0;
        int input = msg & 15, kind = msg & 240, a = (msg >> 8) & 127, b = (msg >> 16) & 127;
        bool held = std::any_of(pressed[input].begin(), pressed[input].end(), [](bool v) { return v; });
        if (kind == 0xd0 || kind == 0xa0) {
            if (!aftertouch || !held || (kind == 0xa0 && !pressed[input][a]))
                return;
        }
        if (kind == 0x90 && b) {
            if (!held)
                inputPressure[input] = 127;
            pressed[input][a] = true;
        }
        if (kind == 0x80 || (kind == 0x90 && !b))
            pressed[input][a] = false;
        if (kind == 0xb0 && (a == 120 || a == 123))
            pressed[input].fill(false);
        if (!std::any_of(pressed[input].begin(), pressed[input].end(), [](bool v) { return v; })) {
            inputPressure[input] = 127;
            for (int c = 0; c < 16; ++c)
                if (polyphonic ? owner[c] == input : c == input) {
                    pressure[c] = 127;
                    if (note[c] >= 0)
                        level(c);
                }
        }
        if (!polyphonic) {
            if (kind == 0xd0)
                inputPressure[input] = a;
            if (kind == 0x90 && b)
                pressure[input] = inputPressure[input];
            message(msg);
            return;
        }
        if (kind == 0x90 && b) {
            int base = input < 8 ? 0 : 8, slot = -1;
            std::array<bool, 8> eligible{};
            eligible.fill(true);
            if (base == 0) {
                std::lock_guard<std::mutex> lock(state->mutex);
                eligible.fill(state->valid[input] ||
                              (programs[input] >= 0 && state->defined[programs[input]]));
            }
            if (std::none_of(eligible.begin(), eligible.end(), [](bool v) { return v; }))
                return;
            for (int c = base; c < base + 8; ++c)
                if (owner[c] == input && note[c] == a) {
                    slot = c;
                    break;
                }
            if (slot < 0)
                for (int i = 0; i < 8; ++i) {
                    int c = base + ((input - base + i) % 8);
                    if (note[c] < 0 && eligible[c - base]) {
                        slot = c;
                        break;
                    }
                }
            if (slot < 0) {
                for (int c = base; c < base + 8; ++c)
                    if (eligible[c - base] && (slot < 0 || order[c] < order[slot]))
                        slot = c;
            }
            if (note[slot] >= 0)
                off(slot);
            owner[slot] = input;
            order[slot] = ++serial;
            volume[slot] = inputVolume[input];
            expression[slot] = inputExpression[input];
            bend[slot] = inputBend[input];
            pressure[slot] = inputPressure[input];
            sustain[slot] = inputSustain[input];
            message((msg & ~15u) | unsigned(slot));
        } else if (kind == 0x80 || (kind == 0x90 && !b)) {
            for (int c = 0; c < 16; ++c)
                if (owner[c] == input && note[c] == a)
                    message((msg & ~15u) | unsigned(c));
        } else if (kind == 0xa0) {
            for (int c = 0; c < 16; ++c)
                if (owner[c] == input && note[c] == a)
                    message((msg & ~15u) | unsigned(c));
        } else if (kind == 0xe0 || kind == 0xd0 || (kind == 0xb0 && a != 0)) {
            if (kind == 0xd0)
                inputPressure[input] = a;
            if (kind == 0xe0)
                inputBend[input] = a + (b << 7);
            if (kind == 0xb0) {
                if (a == 7)
                    inputVolume[input] = b;
                if (a == 11)
                    inputExpression[input] = b;
                if (a == 64)
                    inputSustain[input] = b >= 64;
                if (a == 121) {
                    inputVolume[input] = inputExpression[input] = inputPressure[input] = 127;
                    inputBend[input] = 8192;
                    inputSustain[input] = false;
                }
            }
            for (int c = 0; c < 16; ++c)
                if (owner[c] == input && note[c] >= 0)
                    message((msg & ~15u) | unsigned(c));
        } else
            message(msg);
    }
    int sampleRate;
    double phase = 0;
    size_t releaseFrames = 0;
    OpmBackend::Frame held{};
    void write(int a, int v) {
        chip.write_register(uint8_t(a), uint8_t(v));
        if (output)
            output(a, v);
        std::lock_guard<std::mutex> lock(state->mutex);
        state->registers[a] = uint8_t(v);
    }
    void pitch(int ch) {
        if (ch >= 8 || note[ch] < 0)
            return;
        int value =
            std::clamp(int((note[ch] - 15) * 64 + 5 + (bend[ch] - 8192) * 128.0 / 8192), 0, 95 * 64 + 63);
        constexpr int kc[] = {0, 1, 2, 4, 5, 6, 8, 9, 10, 12, 13, 14};
        int semitone = value / 64;
        write(0x28 + ch, semitone / 12 * 16 + kc[semitone % 12]);
        write(0x30 + ch, (value % 64) * 4);
    }
    void level(int ch) {
        if (ch >= 8)
            return;
        constexpr int carriers[] = {8, 8, 8, 8, 12, 14, 14, 15};
        double amplitude =
            velocity[ch] / 127.0 * volume[ch] / 127.0 * expression[ch] / 127.0 * pressure[ch] / 127.0;
        int attenuation = amplitude > 0 ? int(std::lround(-20 * std::log10(amplitude) / .75)) : 127;
        for (int op = 0; op < 4; ++op)
            write(0x60 + op * 8 + ch,
                  std::min(127, int(voices[ch][7 + op] & 127) +
                                    ((carriers[voices[ch][1] & 7] & (1 << op)) ? attenuation : 0)));
    }
    void programVoice(int ch) {
        auto &v = voices[ch];
        write(0x20 + ch, (v[1] & 63) | (external ? 0 : 192));
        for (int g = 0; g < 6; ++g)
            for (int op = 0; op < 4; ++op)
                if (!external || g != 1)
                    write(0x40 + g * 32 + op * 8 + ch, v[3 + g * 4 + op]);
        level(ch);
        if (external)
            write(0x20 + ch, (v[1] & 63) | 192);
    }
    void off(int ch) {
        if (ch < 8) {
            write(8, ch);
            state->midiFmOverride.fetch_and(~(1u << ch));
        } else
            pcm[ch - 8].clear();
        note[ch] = -1;
        state->midiNotes[ch] = -1;
        owner[ch] = -1;
        released[ch] = false;
    }
    void message(uint32_t msg) {
        int ch = msg & 15, kind = msg & 240, a = (msg >> 8) & 127, b = (msg >> 16) & 127;
        if (kind == 0xf0 && ch < 8) {
            int id = (msg >> 8) & 255;
            {
                std::lock_guard<std::mutex> lock(state->mutex);
                if (!state->defined[id])
                    return;
                programs[ch] = id;
                catalogRevision = state->catalogRevision;
                voices[ch] = state->definitions[id];
                state->voices[ch] = voices[ch];
                state->valid[ch] = true;
                state->ids[ch] = id;
                state->used[id] = true;
            }
            for (int slot=0;slot<8;++slot) {
                if (slot!=ch && (programs[slot]==id || (polyphonic && owner[slot]==ch))) {
                    std::lock_guard<std::mutex> lock(state->mutex);
                    voices[slot]=state->definitions[id];
                    state->voices[slot]=voices[slot];
                }
                if ((slot==ch || programs[slot]==id || (polyphonic && owner[slot]==ch)) && note[slot]>=0) programVoice(slot);
            }
        } else if (kind == 0x90 && b) {
            note[ch] = a;
            velocity[ch] = b;
            released[ch] = false;
            if (ch < 8) {
                {
                    std::lock_guard<std::mutex> lock(state->mutex);
                    if (catalogRevision != state->catalogRevision) {
                        programs.fill(-1);
                        catalogRevision = state->catalogRevision;
                    }
                    int source = polyphonic && owner[ch] >= 0 ? owner[ch] : ch;
                    int id = programs[source];
                    if (id >= 0 && state->defined[id])
                        voices[ch] = state->definitions[id];
                    else {
                        if (!state->valid[source]) {
                            note[ch] = -1;
                            return;
                        }
                        voices[ch] = state->voices[source];
                    }
                    if (id >= 0 && !polyphonic) {
                        state->voices[ch] = voices[ch];
                        state->valid[ch] = true;
                        state->ids[ch] = id;
                        state->used[id] = true;
                    }
                }
                state->midiFmOverride.fetch_or(1u << ch);
                write(8, ch);
                auto &v = voices[ch];
                programVoice(ch);
                pitch(ch);
                chip.advance(64);
                write(8, ch | ((v[2] & 15) << 3));
            } else {
                std::lock_guard<std::mutex> lock(state->mutex);
                int c = ch - 8;
                int source = polyphonic && owner[ch] >= 8 ? owner[ch] - 8 : c;
                if (state->pdx && a >= 15 && a <= 110) {
                    try {
                        pcm[c] = state->pdx->decode(state->pcmBank[source] * 96 + a - 15,
                                                    state->pcmFormat[source]);
                    } catch (...) {
                        pcm[c].clear();
                    }
                    constexpr double rates[] = {3906.25, 4000000.0 / 768, 7812.5, 8000000.0 / 768,
                                                15625,   15625,           15625};
                    pcmRate[c] = rates[state->pcmFormat[source]];
                    position[c] = 0;
                    shifters[c].reset();
                }
            }
        } else if (kind == 0x80 || (kind == 0x90 && !b)) {
            if (note[ch] == a) {
                if (sustain[ch])
                    released[ch] = true;
                else
                    off(ch);
            }
        } else if (kind == 0xc0 && ch < 8) {
            std::lock_guard<std::mutex> lock(state->mutex);
            if (catalogRevision != state->catalogRevision) {
                programs.fill(-1);
                catalogRevision = state->catalogRevision;
            }
            int id = (banks[ch] & 1) * 128 + a;
            if (state->defined[id]) {
                programs[ch] = id;
                state->voices[ch] = state->definitions[id];
                state->valid[ch] = true;
                state->ids[ch] = id;
                state->used[id] = true;
            }
        } else if (kind == 0xb0) {
            if (a == 0)
                banks[ch] = b;
            if (a == 7) {
                volume[ch] = b;
                level(ch);
            }
            if (a == 11) {
                expression[ch] = b;
                level(ch);
            }
            if (a == 64) {
                sustain[ch] = b >= 64;
                if (!sustain[ch] && released[ch])
                    off(ch);
            }
            if (a == 120 || a == 123)
                off(ch);
            if (a == 121) {
                volume[ch] = expression[ch] = pressure[ch] = 127;
                bend[ch] = 8192;
                sustain[ch] = false;
                if (released[ch])
                    off(ch);
                level(ch);
                pitch(ch);
            }
        } else if (kind == 0xd0 || (kind == 0xa0 && note[ch] == a)) {
            pressure[ch] = kind == 0xd0 ? a : b;
            level(ch);
        } else if (kind == 0xe0) {
            bend[ch] = a + (b << 7);
            pitch(ch);
        }
    }

  public:
    MidiSynth(std::shared_ptr<VoiceState> s, std::shared_ptr<MidiQueue> q, int rate, bool hardware = false,
              std::function<void(int, int)> sink = {}, int backend = 0, std::shared_ptr<Monitor> view = {})
        : chip(bool(view), !hardware, backend), waveformView(view), state(s), queue(q), output(sink),
          external(hardware), sampleRate(rate) {
        note.fill(-1);
        owner.fill(-1);
        inputPressure.fill(127);
        pressure.fill(127);
        inputVolume.fill(127);
        inputExpression.fill(127);
        inputBend.fill(8192);
        programs.fill(-1);
        volume.fill(127);
        expression.fill(127);
        bend.fill(8192);
        chip.set_measured_output_timing(true);
    }
    ~MidiSynth() {
        try {
            for (int ch = 0; ch < 16; ++ch)
                if (note[ch] >= 0)
                    off(ch);
        } catch (...) {
        }
        state->midiFmOverride = 0;
    }
    void setAftertouch(bool enabled) {
        if (aftertouch == enabled)
            return;
        aftertouch = enabled;
        inputPressure.fill(127);
        pressure.fill(127);
        for (int c = 0; c < 8; ++c)
            if (note[c] >= 0)
                level(c);
    }
    void silence() {
        for (auto &keys : pressed)
            keys.fill(false);
        inputPressure.fill(127);
        pressure.fill(127);
        queue->clear();
        for (int ch = 0; ch < 16; ++ch) {
            off(ch);
            if (ch < 8)
                for (int op = 0; op < 4; ++op)
                    write(0x60 + op * 8 + ch, 127);
        }
        releaseFrames = 0;
        held = {};
        phase = 0;
    }
    void setPolyphonic(bool enabled) {
        if (polyphonic != enabled) {
            for (int ch = 0; ch < 16; ++ch)
                if (note[ch] >= 0)
                    off(ch);
            for (auto &keys : pressed)
                keys.fill(false);
            inputPressure.fill(127);
            pressure.fill(127);
            polyphonic = enabled;
        }
    }
    void setMeasuredOutputTiming(bool enabled) { chip.set_measured_output_timing(enabled); }
    void setWaveformExclusive(bool enabled) { waveformExclusive = enabled; }
    void clearPrograms() { programs.fill(-1); }
    void restoreHardware() {
        if (!external)
            return;
        for (int ch = 0; ch < 8; ++ch)
            if (note[ch] >= 0) {
                auto &v = voices[ch];
                programVoice(ch);
                pitch(ch);
                write(8, ch | ((v[2] & 15) << 3));
            }
    }
    void mix(int16_t *out, size_t frames, double gain = .4, double fmGain = 2, double pcmGain = 1) {
        std::deque<uint32_t> pending;
        {
            std::lock_guard<std::mutex> lock(queue->mutex);
            pending.swap(queue->messages);
        }
        for (auto m : pending)
            route(m);
        for (int c = 0; c < 16; ++c)
            state->midiNotes[c] = note[c];
        bool heldFm = false;
        for (int ch = 0; ch < 8; ++ch)
            heldFm |= note[ch] >= 0;
        if (heldFm)
            releaseFrames = sampleRate * 2;
        bool captureWaves=waveformView && waveformView->waveforms.load(std::memory_order_relaxed);
        chip.set_monitor_enabled(captureWaves);
        bool renderFm = (!external || captureWaves) && (heldFm || releaseFrames > 0);
        releaseFrames = releaseFrames > frames ? releaseFrames - frames : 0;
        for (size_t i = 0; i < frames; ++i) {
            if (renderFm)
                phase += 62500.0 / sampleRate;
            while (phase >= 1) {
                chip.advance(64);
                held = chip.last_sample();
                phase -= 1;
            }
            double mono = 0;
            std::array<double, 8> pcmWave{};
            for (int c = 0; c < 8; ++c)
                if (position[c] < pcm[c].size()) {
                    pcmWave[c] = shifters[c].read(pcm[c], position[c], 0, 0) * velocity[c + 8] / 127.0 *
                                 volume[c + 8] / 127.0 * expression[c + 8] / 127.0 * pressure[c + 8] / 127.0 *
                                 pcmGain;
                    mono += pcmWave[c];
                    position[c] +=
                        pcmRate[c] * std::exp2((bend[c + 8] - 8192) * 2.0 / 8192 / 12) / sampleRate;
                }
            if (captureWaves && waveformSamples++ % 8 == 0) {
                for (int c = 0; c < 8; ++c) {
                    auto sample = chip.channel_sample(c);
                    waveformFrame.waveform[c][waveformFrame.cursor] =
                        renderFm ? float((sample.left + sample.right) * .5 * fmGain * gain) : 0;
                    waveformFrame.waveform[c + 8][waveformFrame.cursor] = float(pcmWave[c] * gain / 32768);
                }
                waveformFrame.cursor = (waveformFrame.cursor + 1) % MonitorFrame::points;
            }
            double l = (!renderFm || external ? 0 : held.left * 32767 * fmGain) + mono,
                   r = (!renderFm || external ? 0 : held.right * 32767 * fmGain) + mono;
            out[i * 2] = int16_t(std::clamp(std::lround(out[i * 2] + l * gain), -32768L, 32767L));
            out[i * 2 + 1] = int16_t(std::clamp(std::lround(out[i * 2 + 1] + r * gain), -32768L, 32767L));
        }
        if (waveformView) {
            std::unique_lock<std::mutex> lock(waveformView->mutex, std::try_to_lock);
            if (lock.owns_lock()) {
                auto &frame = waveformView->frame;
                if (waveformExclusive)
                    frame.cursor = waveformFrame.cursor;
                for (int c = 0; c < 16; ++c)
                    if (waveformExclusive || note[c] >= 0) {
                        if(captureWaves) for (size_t j = 0; j < MonitorFrame::points; ++j)
                            frame.waveform[c][(frame.cursor + j) % MonitorFrame::points] =
                                waveformFrame.waveform[c][(waveformFrame.cursor + j) % MonitorFrame::points];
                        frame.note[c] = note[c] >= 15 ? note[c] - 15 : -1;
                    }
            }
        }
        for (int c = 0; c < 8; ++c)
            if (note[c + 8] >= 0 && position[c] >= pcm[c].size())
                off(c + 8);
    }
};
} // namespace mdx
