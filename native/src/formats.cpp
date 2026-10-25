#include "player.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <iconv.h>
#endif
namespace mdx {
[[noreturn]] static void dataError(const std::string &message, size_t offset, const std::string &value,
                                   const std::string &expected) {
    std::ostringstream text;
    text << message << " (file offset 0x" << std::hex << offset << std::dec << ", value=" << value
         << ", expected=" << expected << ")";
    throw std::runtime_error(text.str());
}

static uint32_t be(const Bytes &b, size_t pos, size_t length) {
    if (pos > b.size() || length > b.size() - pos)
        dataError("Truncated file", pos, "EOF",
                  std::to_string(length) + " bytes within file size " + std::to_string(b.size()));
    uint32_t value = 0;
    while (length--)
        value = (value << 8) | b[pos++];
    return value;
}
Bytes readFile(const std::filesystem::path &path) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in)
        throw std::runtime_error("Cannot open: " + path.u8string());
    auto length = in.tellg();
    if (length <= 0 || length > 128 * 1024 * 1024)
        throw std::runtime_error("File size must be 1..128 MiB");
    Bytes b(static_cast<size_t>(length));
    in.seekg(0);
    if (!in.read(reinterpret_cast<char *>(b.data()), length))
        throw std::runtime_error("File read failed");
    return b;
}
std::string utf8(const std::string &s) {
#ifdef _WIN32
    int n = MultiByteToWideChar(932, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring wide(n, L'\0');
    MultiByteToWideChar(932, 0, s.data(), static_cast<int>(s.size()), wide.data(), n);
    int m = WideCharToMultiByte(CP_UTF8, 0, wide.data(), n, nullptr, 0, nullptr, nullptr);
    std::string result(m, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.data(), n, result.data(), m, nullptr, nullptr);
    return result;
#else
    iconv_t conv = iconv_open("UTF-8", "CP932");
    if (conv == reinterpret_cast<iconv_t>(-1))
        return s;
    std::string result(s.size() * 4 + 16, '\0');
    char *input = const_cast<char *>(s.data());
    char *output = result.data();
    size_t left = s.size(), room = result.size();
    auto status = iconv(conv, &input, &left, &output, &room);
    iconv_close(conv);
    if (status == static_cast<size_t>(-1))
        return s;
    result.resize(result.size() - room);
    return result;
#endif
}
std::filesystem::path sjisPath(const std::string &s) { return std::filesystem::u8path(utf8(s)); }
Song::Song(Bytes input) : bytes(std::move(input)) {
    const Bytes terminator{13, 10, 26};
    auto end = std::search(bytes.begin(), bytes.end(), terminator.begin(), terminator.end());
    if (end == bytes.end())
        dataError("Not an MDX: title terminator missing", bytes.size(), "EOF", "0D 0A 1A title terminator");
    title = utf8(std::string(bytes.begin(), end));
    size_t start = static_cast<size_t>(end - bytes.begin()) + 3;
    auto zero = std::find(bytes.begin() + start, bytes.end(), 0);
    if (zero == bytes.end())
        dataError("PDX filename terminator missing", bytes.size(), "EOF", "00 filename terminator");
    pdxName.assign(bytes.begin() + start, zero);
    base = static_cast<size_t>(zero - bytes.begin()) + 1;
    size_t voice = base + be(bytes, base, 2);
    size_t first = be(bytes, base + 2, 2);
    // The first payload boundary, not physical channel order, determines the
    // compact table size. Both tracks and voices may precede the other payloads.
    auto firstPayload = [&](int channels) {
        size_t tableSize=size_t(channels+1)*2;
        if(base>bytes.size() || tableSize>bytes.size()-base) return size_t(0);
        size_t boundary=voice-base;
        for(int ch=0;ch<channels;++ch)
            boundary=std::min(boundary,size_t(be(bytes,base+2+ch*2,2)));
        return boundary;
    };
    int count=firstPayload(9)==20 ? 9 : firstPayload(16)==34 ? 16 : 0;
    if (!count)
        dataError("Unsupported MDX track table (expected 9 or 16 compact tracks)", base + 2,
                  std::to_string(first), "first payload boundary 20 or 34 (9 or 16 tracks)");
    const size_t tableSize=size_t(count+1)*2;
    for (int i = 0; i < count; ++i) {
        size_t offset = be(bytes, base + 2 + i * 2, 2);
        if (offset < tableSize || base + offset >= bytes.size())
            dataError("Invalid MDX track offset", base + 2 + i * 2, std::to_string(offset),
                      std::to_string(tableSize) + ".." + std::to_string(bytes.size() - base - 1));
        tracks.push_back(base + offset);
    }
    if (voice < base + tableSize || voice > bytes.size())
        dataError("Invalid voice offset", base, std::to_string(voice - base),
                  std::to_string(tableSize) + ".." + std::to_string(bytes.size() - base));
    size_t voiceEnd = bytes.size();
    for (size_t pos : tracks)
        if (pos > voice)
            voiceEnd = std::min(voiceEnd, pos);
    for (size_t pos = voice; pos + 27 <= voiceEnd; pos += 27) {
        int id = bytes[pos];
        if (hasVoice[id])
            continue; // Retain the first definition of each instrument ID.
        std::copy_n(bytes.begin() + pos, 27, voices[id].begin());
        hasVoice[id] = true;
    }
}
Pdx::Pdx(Bytes input) : bytes(std::move(input)) {
    if (bytes.size() < 768)
        throw std::runtime_error("PDX header is shorter than 96 entries");
    size_t boundary = bytes.size();
    // Only inspect complete 96-entry banks before the earliest sample payload.
    for (size_t bank = 0; bank < 256 && (bank + 1) * 768 <= boundary; ++bank) {
        for (size_t i = 0; i < 96; ++i) {
            size_t pos = bank * 768 + i * 8;
            Entry entry{be(bytes, pos, 4), be(bytes, pos + 4, 4)};
            if (entry.length) {
                if (entry.offset < (bank + 1) * 768 || entry.offset > bytes.size() ||
                    entry.length > bytes.size() - entry.offset)
                    throw std::runtime_error(
                        "Invalid PDX sample offset/length (compressed PDX is unsupported)");
                boundary = std::min(boundary, static_cast<size_t>(entry.offset));
            }
            entries.push_back(entry);
        }
        if ((bank + 1) * 768 == boundary)
            break;
    }
}
std::vector<int16_t> Pdx::decode(size_t index, int format) const {
    if (index >= entries.size())
        throw std::runtime_error("PDX sample/bank does not exist");
    if (format < 0 || format > 6)
        throw std::runtime_error("PCM format must be F0..F6");
    auto e = entries[index];
    std::vector<int16_t> result;
    result.reserve(static_cast<size_t>(e.length) * 2);
    if (format == 5) {
        if (e.length % 2)
            throw std::runtime_error("P16 sample has odd byte length");
        for (size_t i = 0; i < e.length; i += 2)
            result.push_back(static_cast<int16_t>(be(bytes, e.offset + i, 2)));
    } else if (format == 6) {
        for (size_t i = 0; i < e.length; ++i)
            result.push_back(static_cast<int8_t>(bytes[e.offset + i]) * 256);
    } else {
        // OKI 4-bit differential coding. Low nibble is transmitted first.
        // Compute the quantizer from its defining progression; no decoder
        // source or lookup tables from other players are included.
        int predictor = 0, stepIndex = 0;
        for (size_t i = 0; i < e.length * size_t(2); ++i) {
            int code = (bytes[e.offset + i / 2] >> ((i % 2) * 4)) & 15;
            int step = static_cast<int>(std::floor(16.0 * std::pow(1.1, stepIndex)));
            int delta = step / 8;
            if (code & 1)
                delta += step / 4;
            if (code & 2)
                delta += step / 2;
            if (code & 4)
                delta += step;
            predictor = std::clamp(predictor + ((code & 8) ? -delta : delta), -2048, 2047);
            int magnitude = code & 7;
            stepIndex = std::clamp(stepIndex + (magnitude < 4 ? -1 : 2 * (magnitude - 3)), 0, 48);
            result.push_back(static_cast<int16_t>(predictor * 16));
        }
    }
    return result;
}
std::shared_ptr<Pdx> locatePdx(const Song &song, const std::filesystem::path &mdxPath,
                               const std::filesystem::path &overridePath) {
    auto load = [](const std::filesystem::path &path) {
        auto bank = std::make_shared<Pdx>(readFile(path));
        bank->sourcePath = path;
        return bank;
    };
    if (!overridePath.empty())
        return load(overridePath);
    if (song.pdxName.empty())
        return {};
    auto name = sjisPath(song.pdxName).filename();
    if (!name.has_extension())
        name += ".PDX";
    auto directory = mdxPath.parent_path();
    if (directory.empty())
        directory = ".";
    auto candidate = directory / name;
    if (std::filesystem::exists(candidate))
        return load(candidate);
    auto lower = [](std::string s) {
        for (char &c : s)
            if (c >= 'A' && c <= 'Z')
                c += 'a' - 'A';
        return s;
    };
    std::vector<std::filesystem::path> exact, numbered;
    for (auto &file : std::filesystem::directory_iterator(directory)) {
        if (!file.is_regular_file())
            continue;
        if (lower(file.path().filename().u8string()) == lower(name.u8string())) {
            exact.push_back(file.path());
            continue;
        }
        // Recognize only a numeric download suffix, such as B8(2).pdx.
        if (lower(file.path().extension().u8string()) != lower(name.extension().u8string()))
            continue;
        std::string stem = file.path().stem().u8string();
        size_t open = stem.rfind('(');
        if (open == std::string::npos || stem.back() != ')' || open + 2 >= stem.size())
            continue;
        bool digits = true;
        for (size_t i = open + 1; i + 1 < stem.size(); ++i)
            if (stem[i] < '0' || stem[i] > '9')
                digits = false;
        if (!digits)
            continue;
        stem.resize(open);
        while (!stem.empty() && stem.back() == ' ')
            stem.pop_back();
        if (lower(stem) == lower(name.stem().u8string()))
            numbered.push_back(file.path());
    }
    if (exact.size() == 1)
        return load(exact.front());
    if (exact.size() > 1)
        throw std::runtime_error("Ambiguous PDX filename; specify --pdx / Choose PDX");
    if (numbered.size() == 1)
        return load(numbered.front());
    if (numbered.size() > 1)
        throw std::runtime_error("Multiple numbered PDX copies found; specify --pdx / Choose PDX");

    throw std::runtime_error("Required PDX not found: " + name.u8string() +
                             ". Place it beside the MDX or specify --pdx.");
}
static double pcmRate(int format) {
    constexpr double rates[] = {
        4000000.0 / 1024, 4000000.0 / 768, 4000000.0 / 512, 8000000.0 / 768, 8000000.0 / 512, 15625, 15625};
    return rates[format];
}
SamplePlayer::SamplePlayer(const Pdx &pdx, size_t sample, int format, Options options)
    : pcm(pdx.decode(sample, format)), opts(options), rate(pcmRate(format)) {
    if (opts.keyMode < 0 || opts.keyMode > 2 || opts.keyShift < -24 || opts.keyShift > 24 ||
        opts.sampleRate < 8000 || opts.sampleRate > 192000 || !std::isfinite(opts.gain) || opts.gain < 0 ||
        opts.gain > 4 || !std::isfinite(opts.maxSeconds) || opts.maxSeconds < 0 || opts.maxSeconds > 3600 ||
        opts.pcmMuteMask > 255)
        throw std::runtime_error("Invalid sample playback options");
    sampleNote = static_cast<int>(sample % 96);
}
size_t SamplePlayer::render(int16_t *out, size_t frames) {
    uint32_t mask = opts.livePcmMuteMask ? opts.livePcmMuteMask->load() : opts.pcmMuteMask;
    int shift = opts.liveKeyShift ? std::clamp(opts.liveKeyShift->load(), -24, 24) : opts.keyShift;
    int mode = opts.liveKeyMode ? std::clamp(opts.liveKeyMode->load(), 0, 2) : opts.keyMode;
    size_t n = 0;
    for (; n < frames && position < pcm.size() && (opts.maxSeconds == 0 || seconds() < opts.maxSeconds);
         ++n) {
        double mix = pitchShift.read(pcm, position, shift, mode);
        int value =
            (mask & 1)
                ? 0
                : static_cast<int>(std::lround(mix * (opts.liveGain ? opts.liveGain->load() : opts.gain) *
                                               (opts.livePcmGain ? opts.livePcmGain->load() : opts.pcmGain)));
        if (opts.monitor && framesDone % 8 == 0) {
            monitorFrame.note[8] = sampleNote;
            monitorFrame.waveform[8][monitorFrame.cursor] = float(value / 32768.0);
            monitorFrame.cursor = (monitorFrame.cursor + 1) % MonitorFrame::points;
        }
        out[n * 2] = out[n * 2 + 1] = static_cast<int16_t>(std::clamp(value, -32768, 32767));
        position += rate * (mode == 1 ? std::exp2(shift / 12.0) : 1.0) / opts.sampleRate *
                    (opts.liveSpeed ? std::clamp(opts.liveSpeed->load(), 0, 200) / 100.0 : 1.0);
        ++framesDone;
    }
    if (opts.monitor) {
        std::unique_lock<std::mutex> lock(opts.monitor->mutex, std::try_to_lock);
        if (lock.owns_lock()) {
            monitorFrame.seconds = seconds();
            if (position >= pcm.size())
                monitorFrame.note[8] = -1;
            opts.monitor->frame = monitorFrame;
        }
    }
    return n;
}
double SamplePlayer::duration() const {
    int mode = opts.liveKeyMode ? opts.liveKeyMode->load() : opts.keyMode;
    int shift = opts.liveKeyShift ? opts.liveKeyShift->load() : opts.keyShift;
    return pcm.size() / (rate * (mode == 1 ? std::exp2(shift / 12.0) : 1));
}
bool SamplePlayer::seek(double time, const std::function<bool()> &) {
    if (!std::isfinite(time) || time < 0 || time > 3600)
        throw std::runtime_error("Seek time outside 0..3600 seconds");
    double length = duration();
    position = length > 0 ? std::clamp(time / length, 0.0, 1.0) * pcm.size() : 0;
    framesDone = uint64_t(std::max(0.0, time) * opts.sampleRate);
    pitchShift.reset();
    return true;
}
double SamplePlayer::seconds() const {
    return pcm.empty() ? 0 : position / pcm.size() * duration();
}
void writeWav(Source &source, const std::filesystem::path &output, int rate,
              const std::function<bool()> &cancelled) {
    std::ofstream out(output, std::ios::binary | std::ios::trunc);
    if (!out)
        throw std::runtime_error("Cannot create WAV output");
    auto le = [&](uint32_t value, int count) {
        while (count--) {
            out.put(static_cast<char>(value & 255));
            value >>= 8;
        }
    };
    out.write("RIFF", 4);
    le(0, 4);
    out.write("WAVEfmt ", 8);
    le(16, 4);
    le(1, 2);
    le(2, 2);
    le(rate, 4);
    le(rate * 4, 4);
    le(4, 2);
    le(16, 2);
    out.write("data", 4);
    le(0, 4);
    uint64_t count = 0;
    std::array<int16_t, 8192> block;
    try {
        while (!cancelled || !cancelled()) {
            size_t n = source.render(block.data(), block.size() / 2);
            if (!n)
                break;
            if (count + n * 4 > 0xffffffffU - 36)
                throw std::runtime_error("WAV exceeds RIFF size limit");
            std::array<char,16384> bytes;
            for(size_t i=0;i<n*2;++i) {
                uint16_t value=static_cast<uint16_t>(block[i]);bytes[i*2]=char(value&255);bytes[i*2+1]=char(value>>8);
            }
            out.write(bytes.data(),std::streamsize(n*4));
            count += n * 4;
            if (!out)
                throw std::runtime_error("WAV write failed");
        }
    } catch (...) {
        out.close();
        std::error_code ec;
        std::filesystem::remove(output, ec);
        throw;
    }
    out.seekp(4);
    le(static_cast<uint32_t>(count + 36), 4);
    out.seekp(40);
    le(static_cast<uint32_t>(count), 4);
    out.close();
    if (!out)
        throw std::runtime_error("WAV finalization failed");
}
void appendWavLoop(const std::filesystem::path& output, int rate, uint64_t begin, uint64_t end)
{
    constexpr uint64_t HEADER_BYTES = 44;
    constexpr uint64_t LOOP_CHUNK_BYTES = 68;
    constexpr uint64_t RIFF_MAX = 0xffffffff;
    std::fstream file(output, std::ios::binary | std::ios::in | std::ios::out | std::ios::ate);
    const auto size = file.tellp();
    if ((!file) || (size < static_cast<std::streamoff>(HEADER_BYTES)))
    {
        throw std::runtime_error("Cannot open WAV for loop metadata");
    }
    const auto bytes = static_cast<uint64_t>(size);
    if ((rate <= 0) || (begin >= end) || (end > ((bytes - HEADER_BYTES) / 4))
        || (end > RIFF_MAX) || (bytes + LOOP_CHUNK_BYTES - 8 > RIFF_MAX))
    {
        throw std::runtime_error("Invalid WAV loop metadata");
    }
    const auto write = [&file](uint32_t value)
    {
        for (int index = 0; index < 4; ++index)
        {
            file.put(static_cast<char>((value >> (index * 8)) & 255));
        }
    };
    file.write("smpl", 4);
    write(60);
    write(0);
    write(0);
    write(static_cast<uint32_t>(1000000000ULL / rate));
    write(60);
    write(0);
    write(0);
    write(0);
    write(1);
    write(0);
    write(0);
    write(0);
    write(static_cast<uint32_t>(begin));
    write(static_cast<uint32_t>(end - 1));
    write(0);
    write(0);
    file.seekp(4);
    write(static_cast<uint32_t>(bytes + LOOP_CHUNK_BYTES - 8));
    file.close();
    if (!file)
    {
        throw std::runtime_error("WAV loop metadata write failed");
    }
}
} // namespace mdx
