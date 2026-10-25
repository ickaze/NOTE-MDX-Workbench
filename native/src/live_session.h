#pragma once
#include "midi_synth.h"
namespace mdx {
// Used only by the interactive player. Pause freezes the song, not MIDI input.
class LiveSession final : public Source {
    Source *song;
    Player *music;
    MidiSynth midi;
    Timeline timeline;
    std::shared_ptr<VoiceState> state;
    std::array<std::array<uint8_t, 27>, 8> savedVoices{};
    std::array<uint8_t, 256> savedRegisters{};
    std::array<int, 8> savedIds{}, savedBanks{}, savedFormats{};
    std::array<bool, 8> savedValid{};
    bool snapshot = false;
    void saveVoices() {
        std::lock_guard<std::mutex> lock(state->mutex);
        savedVoices = state->voices;
        savedRegisters = state->registers;
        savedIds = state->ids;
        savedValid = state->valid;
        savedBanks = state->pcmBank;
        savedFormats = state->pcmFormat;
        snapshot = true;
    }
    void restoreVoices() {
        if (!snapshot)
            return;
        std::lock_guard<std::mutex> lock(state->mutex);
        state->voices = savedVoices;
        state->registers = savedRegisters;
        state->ids = savedIds;
        state->valid = savedValid;
        state->pcmBank = savedBanks;
        state->pcmFormat = savedFormats;
        for (auto &id : state->requestedVoice)
            id = -1;
        midi.clearPrograms();
        snapshot = false;
    }
    std::atomic<bool> &paused;
    std::atomic<double> &seekRequest, &current, &total;
    std::function<bool()> cancelled;
    std::function<void()> resetHardware;
    bool wasPaused = false;
    double gain, fmGain, pcmGain;
    std::shared_ptr<std::atomic<double>> liveGain, liveFmGain, livePcmGain;
    std::shared_ptr<std::atomic<bool>> polyMode, aftertouch;
    std::shared_ptr<std::atomic<int>> speed;
    std::shared_ptr<std::atomic<bool>> measuredTiming;
    bool defaultMeasuredTiming;
    bool songPaused() const { return paused.load() || (speed && speed->load() == 0); }
    bool wantsPoly() const { return (!song || songPaused()) && (!polyMode || polyMode->load()); }

  public:
    LiveSession(Source *s, Player *p, Timeline t, std::shared_ptr<VoiceState> voices,
                std::shared_ptr<MidiQueue> queue, const Options &o, std::atomic<bool> &pause,
                std::atomic<double> &seek, std::atomic<double> &position, std::atomic<double> &duration,
                std::function<bool()> stop, std::function<void()> reset = {},
                std::function<void(int, int)> sink = {})
        : song(s), music(p), midi(voices, queue, o.sampleRate, o.externalFm, sink, o.fmBackend, o.monitor),
          timeline(t), state(voices), paused(pause), seekRequest(seek), current(position), total(duration),
          cancelled(stop), resetHardware(reset), gain(o.gain), fmGain(o.independentGain), pcmGain(o.pcmGain),
          liveGain(o.liveGain), liveFmGain(o.liveIndependentGain), livePcmGain(o.livePcmGain),
          polyMode(o.liveMidiPolyphonic), aftertouch(o.liveAftertouch), speed(o.liveSpeed),
          measuredTiming(o.liveMeasuredOutputTiming), defaultMeasuredTiming(o.measuredOutputTiming) {
        midi.setPolyphonic(wantsPoly());
        if (song)
            total = music ? timeline.duration : song->duration();
    }
    ~LiveSession() {
        if (snapshot)
            try {
                midi.silence();
                restoreVoices();
            } catch (...) {
            }
    }
    bool servicePending() const override { return songPaused() != wasPaused || seekRequest.load() >= 0; }
    bool service() override {
        bool flush = false;
        bool pause = songPaused();
        if (pause != wasPaused) {
            if (pause)
                saveVoices();
            midi.silence();
            if (resetHardware)
                resetHardware();
            if (!pause) {
                restoreVoices();
                if (music)
                    music->syncRegisters();
            }
            midi.setPolyphonic(wantsPoly());
            wasPaused = pause;
            flush = true;
        }
        double target = seekRequest.exchange(-1);
        if (song && target >= 0) {
            current = std::min(target, total.load());
            if (resetHardware)
                resetHardware();
            song->seek(std::min(target, total.load()),
                       [&] { return (cancelled && cancelled()) || seekRequest.load() >= 0; });
            if (music && !pause)
                music->syncRegisters();
            midi.restoreHardware();
            current = music ? music->position(timeline) : song->seconds();
            flush = true;
        }
        return flush;
    }
    size_t render(int16_t *out, size_t frames) override {
        std::fill_n(out, frames * 2, 0);
        size_t count = frames;
        if (song && !songPaused())
            count = song->render(out, frames);
        if (!count && music && music->unlimited() && music->silentEnd() &&
            !(cancelled && cancelled())) {
            if(resetHardware) resetHardware();
            if(!music->seek(0,cancelled)) return 0;
            music->syncRegisters();
            midi.restoreHardware();
            current=0;
            count=song->render(out,frames);
        }
        if (!count)
            return 0;
        midi.setPolyphonic(wantsPoly());
        midi.setAftertouch(aftertouch && aftertouch->load());
        midi.setMeasuredOutputTiming(measuredTiming ? measuredTiming->load() : defaultMeasuredTiming);
        midi.setWaveformExclusive(!song || songPaused());
        midi.mix(out, count, liveGain ? liveGain->load() : gain, liveFmGain ? liveFmGain->load() : fmGain,
                 livePcmGain ? livePcmGain->load() : pcmGain);
        if (song) {
            current = music ? music->position(timeline) : song->seconds();
            if (!music)
                total = song->duration();
        }
        return count;
    }
    double seconds() const override { return song ? song->seconds() : 0; }
};
} // namespace mdx
