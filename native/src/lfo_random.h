#pragma once
#include <cstdint>
namespace mdx {
// Arithmetic and recurrence independently inferred from register captures.
inline int randomPitchDepth(int depth) {
    int byte = depth & 255;
    return byte < 128 ? byte : byte - 256;
}
inline int randomAmplitude(int sample, int depth) {
    const int64_t product = int64_t(sample) * depth;
    const int64_t shifted = product >= 0 ? product / 256 : -((-product + 255) / 256);
    return int(uint64_t(shifted) & 255);
}
class SoftwareLfoRandom {
    uint32_t state = 0x1234;
public:
    int next() {
        // Keep the full multiplication result for the returned value; only
        // the feedback state is truncated to 16 bits.
        const uint32_t product = state * 0xc549u + 0xcu;
        state = product & 0xffffu;
        const int word = int((product >> 8) & 0xffffu);
        return word < 32768 ? word : word - 65536;
    }
};
}
