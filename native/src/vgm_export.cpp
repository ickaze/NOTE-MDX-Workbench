// SPDX-License-Identifier: 0BSD
// Copyright (C) 2026 by I.C.KaZe
#include "vgm_export.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace mdx {
namespace {
constexpr uint64_t sourceRate = 4000000 / 64;
constexpr uint64_t vgmRate = 44100;
void put32(Bytes &b, size_t at, uint32_t value) {
    for (int i = 0; i < 4; ++i) b[at+i] = uint8_t(value >> (8*i));
}
void wait(Bytes &b, uint64_t count) {
    while (count) {
        if (count <= 16) { b.push_back(uint8_t(0x70 + count - 1)); break; }
        auto n = uint16_t(std::min<uint64_t>(count, 65535));
        b.insert(b.end(), {0x61, uint8_t(n), uint8_t(n >> 8)});
        count -= n;
    }
}
void pwm(Bytes &b, int reg, int value) {
    b.insert(b.end(), {0xb2, uint8_t((reg << 4) | ((value >> 8)&15)), uint8_t(value)});
}
}
Bytes makeVgm(Song song, std::shared_ptr<Pdx> bank, const std::function<void(int)> &progress) {
    auto report = [&](int value) { if(progress) progress(value); };
    report(0);
    Options opts;
    opts.skipOfflineFmMonitor = true;
    opts.offlineFM = true; // Sequence/register capture and PCM only: no FM DLL needed.
    opts.loops = 0;
    opts.maxSeconds = 0;
    opts.fadeSeconds = 0;
    opts.gain = 1;
    Player player(song, bank, opts);
    auto timeline = player.analyze([&] { report(0); return false; }, false);
    if (timeline.truncated) throw std::runtime_error("VGM timeline exceeds the supported 3600 seconds.");
    const bool loop = timeline.loopEnd > timeline.loopStart;
    const uint64_t samples = uint64_t(std::llround((loop ? timeline.loopEnd : timeline.duration) * vgmRate));
    const uint64_t loopSample = uint64_t(std::llround(timeline.loopStart * vgmRate));
    struct Event { uint64_t sample; uint8_t address, value; };
    std::vector<Event> events;
    // Constructor register setup precedes renderNativeBlock's tracing.
    player.registerTrace = [&](uint64_t t, int a, int v) {
        events.push_back({(t*vgmRate + sourceRate/2)/sourceRate, uint8_t(a), uint8_t(v)});
    };
    player.syncRegisters();
    player.registerTrace = {};
    std::vector<Stereo> pcm;
    pcm.reserve(size_t(samples));
    uint64_t native = 0, output = 0;
    Stereo integral;
    uint64_t weight = 0;
    const uint64_t targetNative = (samples*sourceRate + vgmRate-1)/vgmRate;
    while (native < targetNative) {
        report(50 + int(native * 750 / std::max<uint64_t>(1,targetNative)));
        auto block = player.renderNativeBlock(size_t(std::min<uint64_t>(8192, targetNative-native)));
        if (block.frames.empty()) break;
        for (auto &event : block.events) {
            uint64_t t = native + event.clock_offset/64;
            events.push_back({(t*vgmRate + sourceRate/2)/sourceRate, uint8_t(event.address), uint8_t(event.value)});
        }
        for (auto &frame : block.frames) {
            uint64_t start = native*vgmRate, end = (native+1)*vgmRate;
            while (start < end && output < samples) {
                uint64_t next = std::min(end, (output+1)*sourceRate);
                auto span = next-start;
                // Keep the PCM8 mix and pan independent on the two output sides.
                integral.left += frame.pcm.left * span;
                integral.right += frame.pcm.right * span;
                weight += span;
                start = next;
                if (start == (output+1)*sourceRate) {
                    pcm.push_back({integral.left / weight, integral.right / weight});
                    integral = {}; weight = 0; ++output;
                }
            }
            ++native;
        }
    }
    pcm.resize(size_t(samples));
    double peak = 0;
    for (const auto &value : pcm) {
        peak = std::max({peak, std::abs(value.left), std::abs(value.right)});
    }
    const bool hasPcm = peak > 0;
    // One shared L/R PCM attenuation preserves stereo balance and dynamics and prevents
    // overload when several PCM8 channels exceed the signed 12-bit range.
    constexpr double maximum = 2047 * 16.0;
    const double pcmScale = peak > maximum ? maximum / peak : 1.0;
    Bytes result(0x100, 0);
    result[0]='V'; result[1]='g'; result[2]='m'; result[3]=' ';
    put32(result, 8, 0x171);
    put32(result, 0x30, 4000000);
    put32(result, 0x34, 0x100-0x34);
    put32(result, 0x18, uint32_t(samples));
    // Match IndependentMDX's existing direct signed-DAC export profile.
    // Time is carried by VGM waits (44.1 kHz), not an invented PWM clock.
    // This is a direct-DAC profile, not general 32X PWM/FIFO emulation.
    if (hasPcm) {
        put32(result, 0x70, 23011361);
        pwm(result, 1, 0);
        pwm(result, 4, 0);
    }
    size_t event = 0;
    uint64_t cursor = 0;
    auto advance = [&](uint64_t target) { wait(result, target-cursor); cursor=target; };
    uint64_t iterations=0;
    for (uint64_t sample = 0; sample < samples; ++sample) {
        if ((iterations++ & 1023) == 0) report(800 + int(sample * 150 / std::max<uint64_t>(1,samples)));
        if (!hasPcm) {
            uint64_t next = event < events.size() ? std::min(samples,events[event].sample) : samples;
            if(loop && sample <= loopSample) next=std::min(next,loopSample);
            if(next>sample) sample=next;
            if(sample>=samples) break;
        }
        if (loop && sample == loopSample) {
            advance(sample);
            put32(result, 0x1c, uint32_t(result.size()-0x1c));
            put32(result, 0x20, uint32_t(samples-loopSample));
        }
        while (event < events.size() && events[event].sample <= sample) {
            advance(sample);
            result.insert(result.end(), {0x54, events[event].address, events[event].value});
            ++event;
        }
        if (hasPcm) {
            advance(sample);
            auto quantize = [&](double value) {
                return std::clamp(int(std::lround(value * pcmScale / 16.0)), -2048, 2047);
            };
            pwm(result, 2, quantize(pcm[size_t(sample)].left) & 4095);
            pwm(result, 3, quantize(pcm[size_t(sample)].right) & 4095);
        }
        if (result.size() > 0xfffffff0ULL) throw std::runtime_error("VGM exceeds the 32-bit file limit.");
    }
    advance(samples);
    if (hasPcm) {
        pwm(result, 4, 0);
    }
    result.push_back(0x66);
    put32(result, 4, uint32_t(result.size()-4));
    report(950);
    return result;
}
}
