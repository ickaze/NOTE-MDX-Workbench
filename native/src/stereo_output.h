#pragma once
#include "player.h"
#include <algorithm>
#include <cmath>
namespace mdx {
struct AudioStem {
    Stereo fm, pcm;
    Stereo mixed() const { return {fm.left + pcm.left, fm.right + pcm.right}; }
};
// Native FM frames arrive once per 64 master clocks. Hold each frame;
// shift only FM R by 28 clocks, then integrate over each output frame.
// gcd(64,28)=4: this grid represents both events exactly. No clock/56 stage.
class Clock64Output {
    AudioStem current;
    std::array<double, 7> rightHistory{};
    uint64_t tick = 0;
    double remaining = 0;
    Stereo held;
    template <class Reader> bool nextTick(Reader &next, bool dac, Stereo &out) {
        if ((tick % 16) == 0 && !next(current))
            return false;
        size_t slot = static_cast<size_t>(tick % 7);
        double delayed = rightHistory[slot];
        rightHistory[slot] = current.fm.right;
        out = {current.fm.left + current.pcm.left, (dac ? delayed : current.fm.right) + current.pcm.right};
        ++tick;
        return true;
    }

  public:
    template <class Reader> size_t render(int16_t *out, size_t count, int rate, bool dac, Reader next) {
        const double width = 4000000.0 / (4.0 * rate);
        size_t n = 0;
        for (; n < count; ++n) {
            Stereo sum;
            double needed = width;
            bool had = false;
            while (needed > 1e-9) {
                if (remaining <= 1e-9) {
                    if (!nextTick(next, dac, held))
                        break;
                    remaining = 1;
                }
                double take = std::min(remaining, needed);
                had = true;
                sum.left += held.left * take;
                sum.right += held.right * take;
                remaining -= take;
                needed -= take;
            }
            if (!had)
                break;
            out[n * 2] = static_cast<int16_t>(std::clamp(std::lround(sum.left / width), -32768L, 32767L));
            out[n * 2 + 1] =
                static_cast<int16_t>(std::clamp(std::lround(sum.right / width), -32768L, 32767L));
        }
        return n;
    }
};
} // namespace mdx
