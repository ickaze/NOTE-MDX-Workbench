// SPDX-License-Identifier: 0BSD
// Copyright (C) 2026 by I.C.KaZe
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>
#include "measured_lfo.hpp"

// Original, specification-based functional model. NOT a cycle-exact core.
namespace independent_opm {
namespace detail {
// Degree/taps inferred from measurements without a preset order. See research/inference.
// Reset seed and register transitions remain unverified.
// bit0 is LSB. Shift right, feed bit0 XOR bit3 into bit16.
constexpr std::uint32_t sequence_step(std::uint32_t state) noexcept {
    const std::uint32_t feedback = (state ^ (state >> 3)) & 1u;
    return ((state >> 1) | (feedback << 16)) & 0x1ffffu;
}
// Exact composition of 16 inferred updates. No table, loop, or dynamic allocation.
constexpr std::uint32_t sequence_advance16(std::uint32_t s) noexcept {
    const std::uint32_t q = s ^ (s >> 3);
    return (s >> 16) ^ (((q << 1) ^ (q << 15)) & 0x1ffffu);
}
// Recording-derived equivalent model; see docs/RANDOM_LFO_EVIDENCE.md.
// This is not a reconstruction of the physical register pipeline.
struct MeasuredRandom {
    std::uint32_t state = 1; // deterministic choice, not a measured reset seed
    unsigned age = 0, latch_age = 0;
    std::uint8_t am = 0;
    static std::uint32_t shift(std::uint32_t s, int offset) noexcept {
        while (offset > 0) { s = sequence_step(s); --offset; }
        while (offset < 0) {
            const auto old_bit0 = ((s >> 16) ^ (s >> 2)) & 1u;
            s = ((s << 1) & 0x1ffffu) | old_bit0;
            ++offset;
        }
        return s;
    }
    static unsigned period(std::uint8_t frequency) noexcept {
        return 1u << (24 - (frequency >> 4));
    }
    void change_nfrq() noexcept {
        // Fallback: preserve sequence, restart divider. Same-value writes do nothing.
        age = 0;
    }
    void capture() noexcept {
        unsigned code = 0;
        for (unsigned j = 0; j < 8; ++j) {
            // Equivalent sample at t-4*j clocks; one preceding burst suffices.
            const int prior = age < 4*j ? -16 : 0;
            const int offset = prior - 7 - int(j) + (j >= 6 ? 16 : 0);
            code = (code << 1) | (shift(state, offset) & 1u);
        }
        am = static_cast<std::uint8_t>(code ^ 255u);
    }
    void advance(unsigned clocks, std::uint8_t nfrq, std::uint8_t frequency,
                 bool hold) noexcept {
        const unsigned divider = 32u * (32u - (nfrq & 31u));
        const unsigned latch_period = period(frequency);
        while (clocks) {
            unsigned n = clocks < divider-age ? clocks : divider-age;
            if (!hold && n > latch_period-latch_age) n = latch_period-latch_age;
            age += n;
            if (!hold) latch_age += n;
            clocks -= n;
            if (age == divider) { age = 0; state = sequence_advance16(state); }
            if (!hold && latch_age == latch_period) { latch_age = 0; capture(); }
        }
    }
};

}

struct Stereo { double left = 0; double right = 0; };
enum class Envelope { off, attack, decay1, decay2, release };
struct OperatorInfo { double attenuation_db; double frequency_hz; Envelope stage; bool key; };

struct LfoInfo {
    std::uint16_t phase = 0, am = 0;
    std::int16_t pm = 0;
    double am_after_depth = 0, pm_after_depth = 0;
    bool uses_unverified_random = false;
};

class Ym2151 {
public:
    using Sink = void (*)(void*, Stereo);
    explicit Ym2151(std::uint32_t clock_hz = 3579545);
    void reset();
    std::vector<std::uint8_t> save_state() const;
    // Invalid/incompatible state returns false and leaves the instance unchanged.
    bool load_state(const void* data, std::size_t size);
    // Integration API: immediate register update, bypasses BUSY rejection.
    void write_register(std::uint8_t address, std::uint8_t value);
    // Bus API: rejected writes have NO side effects. Address latch is instantaneous.
    void write_address(std::uint8_t address) { address_ = address; }
    bool write_data(std::uint8_t value);
    std::uint8_t status() const;
    bool irq() const { return (flags_ & 3) != 0; }
    std::uint8_t control_outputs() const { return registers_[0x1b] >> 6; }
    void advance(std::uint64_t clocks, Sink sink = nullptr, void* context = nullptr);
    Stereo last_sample() const { return last_; }
    // Pre-pan, normalized native-rate channel output; updated at synthesis boundaries.
    const std::array<Stereo, 8>& last_channel_samples() const { return channel_samples_; }
    std::uint64_t clock_count() const { return clocks_; }
    std::uint32_t clock_hz() const { return clock_hz_; }
    double native_rate() const { return clock_hz_ / 64.0; }
    double sample_fraction() const { return sample_phase_ / 64.0; }
    OperatorInfo inspect(unsigned channel, unsigned slot) const;
    LfoInfo inspect_lfo() const { return lfo_info_; }
    // Calibration model: output carriers may use the previous native sample.
    // Bits 0..3 are M1/M2/C1/C2 in register order. No automatic hardware mapping.
    // Applies only to audible carriers of the selected algorithm; FM routing is unchanged.
    void set_output_sample_delays(unsigned channel, std::uint8_t left_mask,
                                  std::uint8_t right_mask);
    void clear_output_sample_delays();
    // Recording-derived ALG5/ALG7 carrier frame selection; common capture delay excluded.
    // Other algorithms retain their existing output. Explicit masks take precedence.
    void set_measured_output_timing(bool enabled) { measured_alg5_timing_ = enabled; }
    // Compatibility alias; now enables the measured ALG5 and ALG7 models.
    void set_measured_alg5_timing(bool enabled) { set_measured_output_timing(enabled); }
    // All state is value-owned: copying the instance saves/restores exact state
    // within the same executable. No pointers/callbacks are retained.
private:
    template<class Archive> void state_fields(Archive& ar);
    struct Operator {
        std::uint32_t phase = 0;
        double attenuation = 96;
        Envelope stage = Envelope::off;
        bool key = false;
    };
    std::uint32_t clock_hz_;
    std::array<std::uint8_t, 256> registers_{};
    std::array<std::array<Operator, 4>, 8> operators_{};
    std::array<std::array<double, 2>, 8> feedback_{};
    std::array<std::uint8_t, 8> manual_keys_{};
    std::uint64_t clocks_ = 0;
    std::uint32_t timer_a_ = 0, timer_b_ = 0, busy_ = 0;
    unsigned sample_phase_ = 0;
    std::uint8_t address_ = 0, flags_ = 0, amd_ = 0, pmd_ = 0;
    detail::PeriodicModulator periodic_{};
    detail::MeasuredRandom random_{};
    LfoInfo lfo_info_{};
    bool csm_release_ = false;
    Stereo last_{};
    std::array<Stereo, 8> channel_samples_{};
    std::array<std::uint8_t, 8> left_previous_mask_{}, right_previous_mask_{};
    bool measured_alg5_timing_ = false;
    std::array<std::array<double, 4>, 8> previous_outputs_{};
    // Derived caches are not serialized; reset/load invalidate them.
    std::array<std::array<double,4>,8> cached_cents_{}, cached_amplitude_attenuation_{};
    std::array<std::array<std::uint32_t,4>,8> cached_increment_{};
    std::array<std::array<double,4>,8> cached_amplitude_{};
    std::array<std::array<bool,4>,8> increment_valid_{}, amplitude_valid_{};
    unsigned reg(unsigned base, unsigned channel, unsigned slot) const;
    unsigned rate(unsigned channel, unsigned slot, unsigned raw) const;
    double frequency(unsigned channel, unsigned slot, double cents) const;
    std::uint32_t period_a() const;
    std::uint32_t period_b() const;
    void key(unsigned channel, unsigned slot, bool on, bool force = false);
    void envelope(unsigned channel, unsigned slot);
    Stereo synthesize();
};

// Causal 33-tap windowed-sinc output filter; delay = 16 native samples.
// It is a host-side resampler, not a model of YM3012 or the analog output stage.
class Resampler {
public:
    Resampler(double native_rate, std::uint32_t output_rate);
    std::vector<std::uint8_t> save_state() const;
    bool load_state(const void* data, std::size_t size);
    void push(Stereo frame);
    Stereo output(double native_fraction) const;
    static void receive(void* context, Stereo frame) {
        static_cast<Resampler*>(context)->push(frame);
    }
private:
    template<class Archive> void state_fields(Archive& ar);
    static constexpr unsigned taps = 33, phases = 1024;
    std::array<Stereo, taps> history_{};
    std::array<std::array<double, taps>, phases> weights_{};
    unsigned head_ = 0;
};
}
