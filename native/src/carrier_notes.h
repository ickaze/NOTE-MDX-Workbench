#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
namespace mdx {
using DisplayNotes = std::array<int, 4>;
using DisplayChannels = std::array<DisplayNotes, 16>;
inline bool hasDisplayNote(const DisplayNotes &notes, int note) {
    return std::find(notes.begin(), notes.end(), note) != notes.end();
}
inline DisplayNotes carrierNotes(int base, const std::array<uint8_t, 256> &regs, int channel, bool enabled) {
    DisplayNotes result{-1, -1, -1, -1};
    if (base < 0)
        return result;
    if (!enabled || channel >= 8) {
        result[0] = base;
        return result;
    }
    // Register slot order M1,M2,C1,C2, not diagram/MML order.
    constexpr int carriers[] = {8, 8, 8, 8, 12, 14, 14, 15};
    constexpr double detune[] = {0, 6, 7.81, 9.50};
    int index = 0;
    for (int slot = 0; slot < 4; ++slot) {
        if (!(carriers[regs[0x20 + channel] & 7] & (1 << slot)))
            continue;
        int ml = regs[0x40 + slot * 8 + channel] & 15;
        int dt2 = regs[0xc0 + slot * 8 + channel] >> 6;
        double shift = 12 * std::log2(ml ? double(ml) : .5) + detune[dt2];
        int note = int(std::lround(base + (regs[0x30 + channel] >> 2) / 64.0 + shift));
        result[index++] = note >= 0 && note < 96 ? note : -1;
    }
    return result;
}
} // namespace mdx
