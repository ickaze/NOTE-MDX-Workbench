#include "mrl_export.h"
#include "mrl.h"
#include <cmath>
#ifdef _WIN32
#include <windows.h>
#endif
namespace mdx {
uint32_t dataCrc(const uint8_t *data, size_t size) {
  uint32_t crc = ~0u;
  for (size_t at = 0; at < size; ++at) {
    crc ^= data[at];
    for (int bit = 0; bit < 8; ++bit)
      crc = (crc >> 1) ^ ((crc & 1) ? 0xedb88320u : 0);
  }
  return ~crc;
}
void exportRegisterLog(const std::filesystem::path &input,
                       const std::filesystem::path &target,
                       const std::filesystem::path &source,
                       const std::filesystem::path &bankPath, unsigned loops,
                       const std::function<void(int)> &checkpoint) {
  if (std::filesystem::exists(target))
    throw std::runtime_error("Output already exists");
  auto data = readFile(input);
  uint32_t crc = ~0u;
  for (uint8_t value : data) {
    crc ^= value;
    for (int bit = 0; bit < 8; ++bit)
      crc = (crc >> 1) ^ ((crc & 1) ? 0xedb88320u : 0);
  }
  Song song(std::move(data));
  std::filesystem::create_directories(target.parent_path());
  std::filesystem::path temporary;
  for (unsigned i = 0; i < 10000; ++i) {
    temporary = target;
    temporary += ".work-" + std::to_string(i);
    if (std::filesystem::create_directory(temporary))
      break;
    temporary.clear();
  }
  if (temporary.empty())
    throw std::runtime_error("Cannot create log temporary directory");
  struct Cleanup {
    std::filesystem::path p;
    ~Cleanup() {
      std::error_code e;
      std::filesystem::remove_all(p, e);
    }
  } cleanup{temporary};
  auto file = temporary / "output.mrl";
  std::ofstream out(file, std::ios::binary);
  if (!out)
    throw std::runtime_error("Cannot create register log");
  std::vector<unsigned char> header(128);
  auto put = [](auto &b, size_t at, uint64_t value, unsigned bytes) {
    for (unsigned i = 0; i < bytes; ++i)
      b[at + i] = static_cast<unsigned char>(value >> (8 * i));
  };
  const char magic[8] = {'M', 'D', 'X', 'R', 'L', 'O', 'G', 0};
  std::copy(magic, magic + 8, header.begin());
  put(header, 8, 1, 2);
  put(header, 10, 2, 2);
  put(header, 12, header.size(), 4);
  put(header, 16, 16, 4);
  put(header, 20, 3, 4);
  put(header, 24, 4000000, 8);
  put(header, 48, loops, 4);
  put(header, 52, 1, 4);
  put(header, 56, ~crc, 4);
  put(header, 60, 64, 4);
  std::shared_ptr<Pdx> bank;
  if (true) {
    try {
      bank = locatePdx(song, source, bankPath);
    } catch (const std::exception &e) {
      // Missing banks still permit a portable FM/PCM operation log.
      if (std::string(e.what()).rfind("Required PDX not found:", 0) != 0)
        throw;
    }
    put(header, 64, song.title.size(), 4);
    put(header, 68, song.pdxName.size(), 4);
    if (bank) {
      put(header, 72, bank->bytes.size(), 8);
      put(header, 80, dataCrc(bank->bytes.data(), bank->bytes.size()), 4);
    }
    bool embedded = true && bool(bank);
    put(header, 84, embedded, 4);
    uint64_t offset = 128 + song.title.size() + song.pdxName.size() +
                      (embedded ? bank->bytes.size() : 0);
    put(header, 12, offset, 4);
    put(header, 88, offset, 8);
  }
  out.write(reinterpret_cast<const char *>(header.data()), header.size());
  if (true) {
    out.write(song.title.data(), song.title.size());
    out.write(song.pdxName.data(), song.pdxName.size());
    if (bank && true)
      out.write(reinterpret_cast<const char *>(bank->bytes.data()),
                bank->bytes.size());
  }
  uint64_t events = 0, lastTime = 0, frames = 0;
  Options options;
  options.loops = loops;
  options.fadeSeconds = 0;
  options.maxSeconds = 0;
  options.sampleRate = 62500;
  options.resampleOutput = false;
  options.skipFmSynthesis = true;
  options.skipPcmSynthesis = true;
  Player scan(song, {}, options);
  auto estimate = scan.analyze([&] { checkpoint(0); return false; });
  if (estimate.truncated)
    throw std::runtime_error("Register log timeline exceeds 3600 seconds");
  double expectedSeconds = estimate.duration + .5;
  if (estimate.loopEnd > estimate.loopStart)
    expectedSeconds = estimate.loopEnd + (loops - 1) * (estimate.loopEnd - estimate.loopStart);
  const uint64_t expectedFrames = std::max<uint64_t>(1, uint64_t(expectedSeconds * 62500));

  auto record = [&](uint64_t tick, int chip, int address, int value,
                    uint32_t argument) {
    uint64_t time = tick * 64;
    if (time < lastTime)
      throw std::runtime_error("Non-monotonic register log timestamp");
    lastTime = time;
    std::array<unsigned char, 16> event{};
    put(event, 0, time, 8);
    event[8] = uint8_t(chip);
    event[9] = uint8_t(address);
    event[10] = uint8_t(value);
    put(event, 12, argument, 4);
    out.write(reinterpret_cast<const char *>(event.data()), event.size());
    ++events;
    if (events % 1024 == 0)
      checkpoint(-1);
    if (!out)
      throw std::runtime_error("Cannot write register log");
  };
  options.registerLogOutput = [&](uint64_t tick, int a, int v) {
    record(tick, 0, a, v, 0);
  };
  if (true)
    options.pcmLogOutput = [&](uint64_t tick, int ch, int op, uint32_t arg,
                               int format) {
      record(tick, ch + 1, op, format, arg);
    };
  options.diagnostic = [](const std::string &message) {
    throw std::runtime_error(message);
  };
  Player player(std::move(song), {}, options);
  std::array<int16_t, 2048> scratch{};
  for (;;) {
    checkpoint(int(std::min<uint64_t>(850, frames * 850 / expectedFrames)));
    auto n = player.render(scratch.data(), 1024);
    if (!n)
      break;
    frames += n;
    if (frames > uint64_t(3600) * 62500)
      throw std::runtime_error(
          "Register log time limit exceeded; no partial file published");
  }
  if (!false && events) {
    auto timeline = player.recordedTimeline();
    uint64_t begin = uint64_t(std::llround(timeline.loopStart * 4000000));
    uint64_t end = uint64_t(std::llround(timeline.loopEnd * 4000000));
    // Finite scores repeat their full captured performance.
    if (end <= begin) {
      begin = 0;
      end = std::max(frames * 64, lastTime);
    }
    out.flush();
    std::ifstream scan(file, std::ios::binary);
    uint64_t offset = 0;
    for (unsigned i = 0; i < 4; ++i)
      offset |= uint64_t(header[12 + i]) << (i * 8);
    scan.seekg(std::streamoff(offset));
    uint64_t first = events, last = events;
    std::array<unsigned char, 16> row{};
    for (uint64_t i = 0; i < events; ++i) {
      if (!scan.read(reinterpret_cast<char *>(row.data()), 16))
        throw std::runtime_error("Cannot index log loop");
      uint64_t t = 0;
      for (unsigned j = 0; j < 8; ++j)
        t |= uint64_t(row[j]) << (j * 8);
      if (first == events && t >= begin)
        first = i;
      if (t >= end) {
        last = i;
        break;
      }
    }
    if (first < last && end > begin) {
      put(header, 96, begin, 8);
      put(header, 104, end, 8);
      put(header, 112, first, 8);
      put(header, 120, last, 8);
    }
  }
  put(header, 32, events, 8);
  put(header, 40, std::max(frames * 64, lastTime), 8);
  out.seekp(0);
  out.write(reinterpret_cast<const char *>(header.data()), header.size());
  out.close();
  if (!out)
    throw std::runtime_error("Cannot finalize register log");
  checkpoint(900);
#ifdef _WIN32
  if (!MoveFileW(file.c_str(), target.c_str()))
    throw std::runtime_error("Cannot publish register log");
#else
  std::filesystem::create_hard_link(file, target);
#endif
}

} // namespace mdx
