#pragma once
#include "player.h"

namespace mdx {
// Selected part and allocated audition/MIDI channels, counted once each.
inline std::array<double, MonitorFrame::points> instrumentWaveform(
    const MonitorFrame &frame, int selected, uint32_t auditionMask) {
    std::array<double, MonitorFrame::points> mixed{};
    for (size_t ch = 0; ch < frame.waveform.size(); ++ch)
        if (int(ch) == selected || (auditionMask & (1u << ch)))
            for (size_t n = 0; n < mixed.size(); ++n)
                mixed[n] += frame.waveform[ch][n];
    return mixed;
}
}
