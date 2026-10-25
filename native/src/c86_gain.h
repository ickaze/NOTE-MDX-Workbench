#pragma once
#include <algorithm>
#include <array>
#include <cmath>
namespace mdx {
// Register-level gain for physical OPM output. No chip reset or timing policy.
class C86Gain {
    std::array<int, 256> raw{};
    std::array<int, 256> sent{};
    double gain = .8;
  public:
    C86Gain() { reset(); }
    void reset() { raw.fill(0); sent.fill(-1); }
    template<class Emit> void write(int a, int value, Emit emit) {
        a &= 255;
        value &= 255;
        const int previous = raw[a];
        raw[a] = value;
        int out = value;
        if (a >= 0x60 && a < 0x80) {
            constexpr int carriers[] = {8, 8, 8, 8, 12, 14, 14, 15};
            const int ch = a & 7, op = (a - 0x60) / 8;
            if (carriers[raw[0x20 + ch] & 7] & (1 << op)) {
                const int atten = gain > 0 ? int(std::lround(-20 * std::log10(gain) / .75)) : 127;
                out = std::clamp((value & 127) + atten, 0, 127);
            }
            // Integer TL often stays unchanged across LFO ticks or volume updates.
            if (sent[a] == out) return;
        }
        if (a >= 0x20 && a < 0x28 && gain == 0) out &= 63;
        emit(a, out);
        sent[a] = out;
        // Pan/feedback writes do not change which operators receive gain.
        if (a >= 0x20 && a < 0x28 && ((previous ^ value) & 7))
            for (int op = 0; op < 4; ++op)
                write(0x60 + op * 8 + (a & 7), raw[0x60 + op * 8 + (a & 7)], emit);
    }
    template<class Emit> void setGain(double master, double fm, Emit emit) {
        double next = std::clamp(master * fm, 0.0, 32.0);
        if (gain == next) return;
        gain = next;
        for (int ch = 0; ch < 8; ++ch) {
            write(0x20 + ch, raw[0x20 + ch], emit);
            for (int op = 0; op < 4; ++op)
                write(0x60 + op * 8 + ch, raw[0x60 + op * 8 + ch], emit);
        }
    }
};
} // namespace mdx
