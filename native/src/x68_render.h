#pragma once
#include "x68_protocol.h"
#include <array>
#include <stdexcept>
namespace mdx {
// Preserve the 44100/62500 accumulator across IPC blocks. No audio is generated
// across a register change; each GetPcm call covers one register-stable span.
class X68BlockRenderer {
    uint32_t phase = 0;
    std::array<int16_t, 2> held{};

  public:
    template <class Write, class Generate> void render(X68Shared &s, Write write, Generate generate) {
        if (s.count > x68Events)
            throw std::runtime_error("Invalid bridge event count");
        std::array<int16_t, x68Frames * 2> block{};
        uint32_t e = 0, f = 0;
        while (f < x68Frames) {
            while (e < s.count && s.events[e].frame == f) {
                write(uint8_t(s.events[e].address), uint8_t(s.events[e].value));
                ++e;
            }
            uint32_t end = e < s.count ? s.events[e].frame : x68Frames;
            if (end <= f || end > x68Frames)
                throw std::runtime_error("Invalid bridge event order");
            unsigned count = (phase + (end - f) * 44100) / 62500;
            if (count)
                generate(block.data(), count);
            unsigned index = 0;
            for (; f < end; ++f) {
                phase += 44100;
                if (phase >= 62500) {
                    held[0] = block[index * 2];
                    held[1] = block[index * 2 + 1];
                    ++index;
                    phase -= 62500;
                }
                s.audio[f * 2] = held[0];
                s.audio[f * 2 + 1] = held[1];
            }
        }
        if (e != s.count)
            throw std::runtime_error("Invalid bridge register timestamps");
    }
};
} // namespace mdx
