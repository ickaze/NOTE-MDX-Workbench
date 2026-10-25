#pragma once
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <mmsystem.h>
#include "player.h"
#include <array>
#include <functional>
#include <stdexcept>
namespace mdx {
// Six independently owned blocks: the device never reads a buffer while
// the renderer is modifying it. Reset returns every block before cleanup.
inline void playWindows(Source &source, int rate, const std::function<bool()> &stop,
                        const std::function<bool()> &pause,
                        const std::function<void(double)> &progress = {}) {
    struct Device {
        HWAVEOUT handle = nullptr;
        HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        std::array<WAVEHDR, 6> headers{};
        std::array<std::array<int16_t, 2048>, 6> data{};
        ~Device() {
            if (handle) {
                waveOutReset(handle);
                for (auto &h : headers)
                    if (h.dwFlags & WHDR_PREPARED)
                        waveOutUnprepareHeader(handle, &h, sizeof(h));
                waveOutClose(handle);
            }
            if (event)
                CloseHandle(event);
        }
    } device;
    if (!device.event)
        throw std::runtime_error("Cannot create audio event");
    WAVEFORMATEX format{};
    format.wFormatTag = WAVE_FORMAT_PCM;
    format.nChannels = 2;
    format.nSamplesPerSec = rate;
    format.wBitsPerSample = 16;
    format.nBlockAlign = 4;
    format.nAvgBytesPerSec = rate * 4;
    if (waveOutOpen(&device.handle, WAVE_MAPPER, &format, reinterpret_cast<DWORD_PTR>(device.event), 0,
                    CALLBACK_EVENT) != MMSYSERR_NOERROR)
        throw std::runtime_error("Windows audio device could not be opened");
    for (size_t i = 0; i < device.headers.size(); ++i) {
        auto &h = device.headers[i];
        h.lpData = reinterpret_cast<LPSTR>(device.data[i].data());
        h.dwBufferLength = sizeof(device.data[i]);
        if (waveOutPrepareHeader(device.handle, &h, sizeof(h)) != MMSYSERR_NOERROR)
            throw std::runtime_error("Audio buffer preparation failed");
    }
    bool ended = false, wasPaused = false;
    while (!stop()) {
        if (source.servicePending()) {
            waveOutReset(device.handle);
            source.service();
            if (!pause())
                waveOutRestart(device.handle);
            ended = false;
        }
        bool isPaused = pause();
        if (isPaused != wasPaused) {
            if (isPaused)
                waveOutPause(device.handle);
            else
                waveOutRestart(device.handle);
            wasPaused = isPaused;
        }
        bool pending = false;
        for (size_t i = 0; i < device.headers.size(); ++i) {
            auto &h = device.headers[i];
            if ((h.dwFlags & WHDR_INQUEUE) != 0) {
                pending = true;
                continue;
            }
            if (ended || isPaused)
                continue;
            size_t frames = source.render(device.data[i].data(), device.data[i].size() / 2);
            if (!frames) {
                ended = true;
                continue;
            }
            h.dwBufferLength = static_cast<DWORD>(frames * 4);
            if (waveOutWrite(device.handle, &h, sizeof(h)) != MMSYSERR_NOERROR)
                throw std::runtime_error("Audio buffer submission failed");
            pending = true;
        }
        if (progress) {
            MMTIME position{};
            position.wType = TIME_SAMPLES;
            waveOutGetPosition(device.handle, &position, sizeof(position));
            double seconds = position.wType == TIME_SAMPLES ? static_cast<double>(position.u.sample) / rate
                                                            : source.seconds();
            progress(seconds);
        }
        if (ended && !pending)
            break;
        WaitForSingleObject(device.event, 20);
    }
}
} // namespace mdx
#endif
