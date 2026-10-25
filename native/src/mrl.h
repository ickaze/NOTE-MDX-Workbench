#pragma once
#include "player.h"
#include <fstream>
namespace mdx {
struct MrlHeader {
  uint64_t offset = 64, count = 0, end = 0, hz = 0, pdxSize = 0;
  uint32_t pdxCrc = 0, loops = 0, sourceCrc = 0;
  unsigned minor = 0, flags = 0;
  bool embedded = false;
  std::string title, pdxName;
  std::filesystem::path file;
  uint64_t bankOffset = 0;
  uint64_t loopStart = 0, loopEnd = 0, loopEvent = 0, loopEndEvent = 0;
};
inline uint64_t mrlNumber(const uint8_t *p, unsigned n) {
  uint64_t v = 0;
  for (unsigned i = 0; i < n; ++i)
    v |= uint64_t(p[i]) << (i * 8);
  return v;
}
inline MrlHeader readMrlHeader(std::istream &in, uint64_t size) {
  auto fail = [] {
    throw std::runtime_error("Invalid or unsupported MRL header");
  };
  std::array<uint8_t, 128> b{};
  if (size < 64 || !in.read(reinterpret_cast<char *>(b.data()), 64))
    fail();
  auto n = [&](size_t p, unsigned len) { return mrlNumber(b.data() + p, len); };
  if (!std::equal(b.begin(), b.begin() + 8, "MDXRLOG\0") || n(8, 2) != 1 ||
      n(10, 2) > 2 || n(16, 4) != 16 || n(52, 4) != 1 || !n(60, 4) ||
      n(48, 4) < 1 || n(48, 4) > 1000)
    fail();
  MrlHeader h;
  h.minor = unsigned(n(10, 2));
  h.flags = unsigned(n(20, 4));
  h.offset = n(12, 4);
  h.hz = n(24, 8);
  h.count = n(32, 8);
  h.end = n(40, 8);
  h.loops = uint32_t(n(48, 4));
  h.sourceCrc = uint32_t(n(56, 4));
  if (!h.hz || double(h.end) / h.hz > 86400 || h.offset > size ||
      h.count != (size - h.offset) / 16 || (size - h.offset) % 16)
    fail();
  if (!h.minor) {
    if (h.offset != 64 || h.flags != 1)
      fail();
    return h;
  }
  if (size < 128 || !in.read(reinterpret_cast<char *>(b.data() + 64), 64))
    fail();
  auto titleSize = n(64, 4), nameSize = n(68, 4);
  h.pdxSize = n(72, 8);
  h.pdxCrc = uint32_t(n(80, 4));
  h.embedded = n(84, 4) == 1;
  if (n(84, 4) > 1 || n(88, 8) != h.offset || h.flags != 3 ||
      titleSize > 1048576 || nameSize > 65536 ||
      h.pdxSize > 128u * 1024 * 1024 ||
      h.offset != 128 + titleSize + nameSize + (h.embedded ? h.pdxSize : 0))
    fail();
  if (h.minor == 2) {
    h.loopStart = n(96, 8); h.loopEnd = n(104, 8);
    h.loopEvent = n(112, 8); h.loopEndEvent = n(120, 8);
    if (h.loopEnd && (h.loopStart >= h.loopEnd || h.loopEnd > h.end ||
        h.loopEvent >= h.loopEndEvent || h.loopEndEvent > h.count)) fail();
    if (!h.loopEnd && (h.loopStart || h.loopEvent || h.loopEndEvent)) fail();
  } else for (size_t i = 96; i < 128; ++i) if (b[i]) fail();
  h.title.resize(size_t(titleSize));
  h.pdxName.resize(size_t(nameSize));
  if (!in.read(h.title.data(), h.title.size()) ||
      !in.read(h.pdxName.data(), h.pdxName.size()))
    fail();
  h.bankOffset = 128 + titleSize + nameSize;
  return h;
}
struct MrlRecord {
  uint64_t time = 0;
  uint8_t chip = 0, op = 0, value = 0, phase = 0;
  uint32_t argument = 0;
};
inline MrlRecord readMrlRecord(const uint8_t *b, const MrlHeader &h) {
  MrlRecord r{mrlNumber(b, 8), b[8],  b[9],
              b[10],           b[11], uint32_t(mrlNumber(b + 12, 4))};
  if (r.time > h.end || r.phase > 2 || (r.phase == 1 && r.time))
    throw std::runtime_error("Invalid MRL event time/phase");
  if (r.chip == 0) {
    if (r.argument)
      throw std::runtime_error("Invalid MRL FM reserved value");
  } else if (h.minor >= 1 && r.chip >= 1 && r.chip <= 8) {
    if (r.phase || r.op > 4 ||
        (r.op == 0 && (r.value > 6 || r.argument >= 24576)) ||
        (r.op == 1 && (r.argument || r.value)) ||
        (r.op == 2 && (r.argument > 3 || r.value)) ||
        (r.op == 3 && (r.argument > 8 * 65536 || r.value)) ||
        (r.op == 4 && (r.argument > 6 || r.value)))
      throw std::runtime_error("Invalid MRL PCM event");
  } else
    throw std::runtime_error("Invalid MRL chip");
  return r;
}
} // namespace mdx
