// SPDX-License-Identifier: 0BSD
// Copyright (C) 2026 by I.C.KaZe
#pragma once
#include <cstdint>
namespace independent_opm { namespace detail {
// Measured steady-state model. Boundary policies: docs/PERIODIC_REPLACEMENT.md.
struct PeriodicModulator {
    unsigned wait_samples = 0, order = 0, position = 0;
    static constexpr unsigned interval(std::uint8_t freq) noexcept {
        return 1u << (18u - (freq >> 4)); // master clocks 2^(24-H), native clock/64
    }
    static constexpr unsigned reverse_nibble(unsigned v) noexcept {
        return ((v&1u)<<3) | ((v&2u)<<1) | ((v&4u)>>1) | ((v&8u)>>3);
    }
    void frequency_written() noexcept { wait_samples = 0; }
    void hold_written() noexcept { position = 0; }
    unsigned next_increment(std::uint8_t freq) noexcept {
        if (++wait_samples < interval(freq)) return 0;
        wait_samples = 0;
        const unsigned extra = reverse_nibble(order) < (freq&15u) ? 1u : 0u;
        order = (order+1u)&15u;
        return 1u+extra;
    }
    void advance(std::uint8_t freq, unsigned shape, bool held) noexcept {
        // Approximation: hold freezes divider/order and forces position zero.
        if (held) { position = 0; return; }
        const unsigned n = next_increment(freq);
        if (shape != 3) position = (position + n*(shape==2 ? 2u : 1u))&511u;
    }
};
struct ModulationValues { int am; int pm; };
inline ModulationValues periodic_values(unsigned shape, unsigned position) noexcept {
    const unsigned p = position&511u;
    const int u = static_cast<int>(p&255u);
    if (shape==0) return {255-u, u<128 ? u : u-255};
    // Square PM +/-128 supported by prior 16 plus new measured pitch response.
    if (shape==1) return u<128 ? ModulationValues{255,128} : ModulationValues{0,-128};
    const int d = static_cast<int>(p&127u);
    int pitch = 0;
    switch (p>>7) {
    case 0: pitch=d; break;
    case 1: pitch=127-d; break;
    case 2: pitch=-d; break;
    default: pitch=d-127; break;
    }
    // Odd positions after waveform switches are interpolated, not fully verified.
    return {p<256 ? 255-static_cast<int>(p) : static_cast<int>(p)-256,pitch};
}
constexpr unsigned attenuation_units(unsigned wave, unsigned depth) noexcept {
    return (wave*(depth&127u))>>7;
}
constexpr int signed_depth(int wave, unsigned depth) noexcept {
    const int m=static_cast<int>((static_cast<unsigned>(wave<0 ? -wave : wave)*(depth&127u))>>7);
    return wave<0 ? -m : m;
}
constexpr int pitch_units(int depth, unsigned sensitivity) noexcept {
    const unsigned p=sensitivity&7u;
    if (!p) return 0;
    unsigned m=static_cast<unsigned>(depth<0 ? -depth : depth);
    m=p<=5 ? m>>(6u-p) : m<<(p-5u);
    return depth<0 ? -static_cast<int>(m) : static_cast<int>(m);
}
inline double attenuation_db(unsigned units, unsigned sensitivity) noexcept {
    const unsigned a=sensitivity&3u;
    // AMS1 acoustic calibration. AMS2/3 scaling remains a documented approximation.
    return a ? units*(1u<<(a-1u))*0.0940893095546545 : 0.0;
}
} }
