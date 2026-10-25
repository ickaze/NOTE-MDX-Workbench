// SPDX-License-Identifier: 0BSD
// Copyright (C) 2026 by I.C.KaZe
#include "vgm_export.h"
#include "notemdx/compiler.hpp"
#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
using mdx::Bytes;
void check(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
}
uint32_t le32(const Bytes &data, size_t at) {
    return uint32_t(data.at(at)) | uint32_t(data.at(at+1)) << 8 |
           uint32_t(data.at(at+2)) << 16 | uint32_t(data.at(at+3)) << 24;
}
void be16(Bytes &data, size_t at, size_t value) {
    data.at(at) = uint8_t(value >> 8);
    data.at(at+1) = uint8_t(value);
}
void be32(Bytes &data, size_t at, size_t value) {
    for (int i = 3; i >= 0; --i) { data.at(at+i) = uint8_t(value); value >>= 8; }
}
struct Decoded {
    std::vector<std::array<int,2>> pcm;
    std::vector<std::array<uint32_t,3>> fm;
    bool pwm = false;
    uint64_t waits = 0;
};
Decoded decode(const Bytes &data) {
    Decoded result;
    check(le32(data,4)+4 == data.size(), "VGM EOF");
    result.pwm = le32(data,0x70) != 0;
    check(!result.pwm || le32(data,0x70) == 23011361, "IndependentMDX PWM clock");
    int left = 0, right = 0;
    size_t pcmWrites = 0;
    bool initialized = false, ended = false;
    const auto start = 0x34 + le32(data,0x34);
    const auto loop = le32(data,0x1c) ? 0x1c + le32(data,0x1c) : 0;
    bool sawLoop = !loop;
    for (size_t p = start; p < data.size();) {
        if (p == loop) { sawLoop = true; check(le32(data,0x20) == le32(data,0x18)-result.waits, "loop samples"); }
        const auto command = data.at(p++);
        uint32_t count = 0;
        if (command == 0x54) {
            result.fm.push_back({uint32_t(result.waits),data.at(p),data.at(p+1)}); p += 2;
        } else if (command == 0xb2 && result.pwm) {
            unsigned reg = data.at(p) >> 4, raw = (data.at(p) & 15) * 256 + data.at(p+1); p += 2;
            // IndependentMDX's existing strict direct-DAC acceptance rules.
            check(reg >= 1 && reg <= 4 && ((reg != 1 && reg != 4) || raw == 0), "IndependentMDX PWM mode");
            int value = raw >= 2048 ? int(raw)-4096 : int(raw);
            if (reg == 4) { left = right = 0; initialized = true; }
            if (reg == 2) { check(initialized, "PCM initialized at digital zero"); left = value; ++pcmWrites; }
            if (reg == 3) { right = value; ++pcmWrites; }
        } else if (command == 0x61) {
            count = data.at(p) | uint32_t(data.at(p+1)) << 8; p += 2;
        } else if (command >= 0x70 && command <= 0x7f) {
            count = 1 + (command & 15);
        } else if (command == 0x66) {
            check(left == 0 && right == 0, "PCM ends at digital zero"); ended = true; break;
        } else {
            throw std::runtime_error("Unexpected VGM command");
        }
        if (count) {
            result.pcm.insert(result.pcm.end(),count,{left,right});
            result.waits += count;
        }
    }
    check(ended && sawLoop && result.waits == le32(data,0x18), "VGM duration/loop/end");
    check(!result.pwm || pcmWrites == 2*result.waits, "two signed stereo writes per PCM frame");
    return result;
}
std::shared_ptr<mdx::Pdx> bank() {
    Bytes data(768 + 512*2);
    be32(data,0,768); be32(data,4,1024);
    for (int i = 0; i < 512; ++i) {
        // Both signs, with sustained sections and a small alternating waveform.
        int sample = (i < 256 ? 12000 : -12000) + (i & 1 ? 1000 : -1000);
        be16(data,768+i*2,uint16_t(sample));
    }
    return std::make_shared<mdx::Pdx>(std::move(data));
}
mdx::Song song(unsigned mask, int pan = 3, bool loop = false, bool split = false) {
    // Minimal independent MDX fixture: PCM8 enable on FM A, P16 on P..W.
    std::vector<Bytes> tracks(16,Bytes{241,0});
    tracks[0] = {232,241,0};
    for (unsigned channel = 0; channel < 8; ++channel) {
        if (mask & (1u << channel)) {
            const auto volume = uint8_t(split && channel % 2 ? 4 : 8);
            const auto routing = uint8_t(split ? (channel % 2 ? 2 : 1) : pan);
            tracks[8+channel] = {237,5,251,volume,252,routing,128,15};
            if (loop) tracks[8+channel].insert(tracks[8+channel].end(),{241,255,251});
            else tracks[8+channel].insert(tracks[8+channel].end(),{241,0});
        }
    }
    Bytes data{'T',13,10,26,'T',0};
    const size_t base = data.size(); data.resize(base+34);
    for (size_t i = 0; i < tracks.size(); ++i) {
        be16(data,base+2+i*2,data.size()-base);
        data.insert(data.end(),tracks[i].begin(),tracks[i].end());
    }
    be16(data,base,data.size()-base);
    return mdx::Song(std::move(data));
}
void write(const std::filesystem::path &path,const Bytes &bytes) {
    std::ofstream out(path,std::ios::binary); out.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());
    check(bool(out),"write VGM fixture");
}
}
int main(int argc, char **argv) {
    try {
        if (argc == 4) {
            // CP932 MUS input: the same native compiler and export function as the GUI.
            const auto raw = mdx::readFile(argv[1]);
            auto compiled = notemdx::compile({"input.mus",std::string(raw.begin(),raw.end())});
            for (const auto &diagnostic : compiled.diagnostics) std::cerr << diagnostic.line << ": " << diagnostic.message << '\n';
            check(compiled.ok(),"MUS compilation");
            auto data = mdx::makeVgm(mdx::Song(compiled.mdx),std::make_shared<mdx::Pdx>(mdx::readFile(argv[2])));
            decode(data); write(argv[3],data);
            return 0;
        }
        auto pdx = bank();
        auto one = decode(mdx::makeVgm(song(1),pdx));
        auto two = decode(mdx::makeVgm(song(3),pdx));
        auto eight = decode(mdx::makeVgm(song(255),pdx));
        auto left = decode(mdx::makeVgm(song(1,1),pdx));
        auto right = decode(mdx::makeVgm(song(1,2),pdx));
        check(one.pwm && two.pwm && eight.pwm, "active PCM channel detection");
        check(one.pcm.size() == two.pcm.size() && one.pcm.size() == eight.pcm.size(), "mixed duration");
        auto asymmetric = decode(mdx::makeVgm(song(255,3,false,true),pdx));
        int peak = 0, asymmetricPeak = 0;
        bool positive = false, negative = false, different = false;
        for (size_t i = 0; i < one.pcm.size(); ++i) {
            for (unsigned side = 0; side < 2; ++side) {
                check(std::abs(two.pcm[i][side]-2*one.pcm[i][side]) <= 1,
                      "two PCM channels summed before quantization");
                peak = std::max(peak,std::abs(eight.pcm[i][side]));
                positive |= eight.pcm[i][side] > 0;
                negative |= eight.pcm[i][side] < 0;
            }
            check(left.pcm[i][0] == one.pcm[i][0] && left.pcm[i][1] == 0,
                  "left pan preserves amplitude and isolates right");
            check(right.pcm[i][1] == one.pcm[i][1] && right.pcm[i][0] == 0,
                  "right pan preserves amplitude and isolates left");
            check(one.pcm[i][0] == one.pcm[i][1], "center pan stays centered");
            const auto pair = asymmetric.pcm[i];
            check(std::abs(8*pair[1]-3*pair[0]) <= 6,
                  "shared overload gain preserves asymmetric 3/8 stereo balance");
            asymmetricPeak = std::max({asymmetricPeak,std::abs(pair[0]),std::abs(pair[1])});
            different |= pair[0] != pair[1];
        }
        check(peak == 2047 && positive && negative, "eight-channel overload attenuation without sign wrap");
        check(asymmetricPeak == 2047 && different, "asymmetric stereo has no independent normalization");
        check(!decode(mdx::makeVgm(song(255,0),pdx)).pwm, "disabled pan is silent");
        auto silent = decode(mdx::makeVgm(song(0),pdx));
        check(!silent.pwm, "silent PCM does not declare an extra chip");
        auto looped = mdx::makeVgm(song(3,3,true),pdx);
        decode(looped);
        check(le32(looped,0x1c) != 0, "PCM loop retained");
        if (argc == 2) {
            std::filesystem::create_directories(argv[1]);
            write(std::filesystem::path(argv[1])/"stereo-eight.vgm",mdx::makeVgm(song(255),pdx));
            write(std::filesystem::path(argv[1])/"stereo-loop.vgm",looped);
            write(std::filesystem::path(argv[1])/"stereo-asymmetric.vgm",mdx::makeVgm(song(255,3,false,true),pdx));
            write(std::filesystem::path(argv[1])/"stereo-left-only.vgm",mdx::makeVgm(song(1,1),pdx));
            write(std::filesystem::path(argv[1])/"stereo-right-only.vgm",mdx::makeVgm(song(1,2),pdx));
        }
        std::cout << "PASS signed PCM8 stereo: 1/2/8 channels, shared overload gain, both signs, pan isolation, asymmetric balance, clock, mode, waits, loop and silence\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}
