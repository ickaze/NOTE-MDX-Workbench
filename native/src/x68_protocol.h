#pragma once
#include <cstdint>
namespace mdx {
// Fixed-width POD only: identical layout in the x64 parent and x86 helper.
constexpr uint32_t x68Magic = 0x58363809, x68Frames = 1024, x68Events = 8192;
struct X68Event {
    uint32_t frame, address, value;
};
struct X68Shared {
    uint32_t magic, command, count;
    int32_t status;
    char error[240];
    X68Event events[x68Events];
    int16_t audio[x68Frames * 2];
};
static_assert(sizeof(X68Event) == 12);
static_assert(sizeof(X68Shared) == 256 + x68Events * sizeof(X68Event) + x68Frames * 4);
} // namespace mdx
