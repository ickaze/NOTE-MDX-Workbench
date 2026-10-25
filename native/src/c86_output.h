// Original IndependentMDX integration: 0BSD; see ../../licenses/IndependentMDX.txt.
// Copyright (c) 2026 IndependentMDX contributors
// C86CTL ABI declarations retain their separate BSD license and copyrights;
// see the declaration notice below and ../../THIRD_PARTY_NOTICES.txt.

#pragma once
#include "c86_gain.h"
#include <windows.h>
#include <unknwn.h>
#include <filesystem>
#include <stdexcept>
#include <mutex>
#include <chrono>
#include <thread>
#include <atomic>
#include <deque>
#include <array>
#include <cmath>
#include <algorithm>
#include <condition_variable>
#include <mmsystem.h>
namespace mdx {
// ABI declarations from honet/c86ctl development/c86ctl/src/c86ctl.h.
// Copyright (c) 2009-2012 honet.
// Copyright (c) 2014, KIICHIRO KOTAJIMA.
// BSD-3-Clause; full conditions and disclaimer: ../../THIRD_PARTY_NOTICES.txt.
namespace c86abi {
const GUID baseId = {0x5c457918, 0xe66d, 0x4ac1, {0x8c, 0xb5, 0xb9, 0x1c, 0x47, 0x04, 0xdf, 0x79}};
const GUID chip3Id = {0x761db10b, 0x2432, 0x4747, {0xac, 0x75, 0x0e, 0xa6, 0xd9, 0x33, 0x67, 0x97}};
const GUID chipId = {0xf959c007, 0x6b4d, 0x46f3, {0xbb, 0x60, 0x9b, 0x08, 0x97, 0xc7, 0xe6, 0x42}};
const GUID gimicId = {0x47141a01, 0x15f5, 0x4bf5, {0x95, 0x54, 0xca, 0x7a, 0xac, 0xd5, 0x4b, 0xb8}};
struct Base : IUnknown {
    virtual int STDMETHODCALLTYPE initialize() = 0;
    virtual int STDMETHODCALLTYPE deinitialize() = 0;
    virtual int STDMETHODCALLTYPE getNumberOfChip() = 0;
    virtual HRESULT STDMETHODCALLTYPE getChipInterface(int, REFIID, void **) = 0;
};
struct Chip : IUnknown {
    virtual int STDMETHODCALLTYPE reset() = 0;
    virtual void STDMETHODCALLTYPE out(UINT, UCHAR) = 0;
    virtual UCHAR STDMETHODCALLTYPE in(UINT) = 0;
};
struct Chip3 : Chip {
    virtual int STDMETHODCALLTYPE getChipStatus(UINT, UCHAR *) = 0;
    virtual void STDMETHODCALLTYPE directOut(UINT, UCHAR) = 0;
    virtual int STDMETHODCALLTYPE getChipType(int *) = 0;
};
struct Devinfo {
    char name[16];
    char rev;
    char serial[15];
};
struct Gimic : IUnknown {
    virtual int STDMETHODCALLTYPE getFWVer(UINT *, UINT *, UINT *, UINT *) = 0;
    virtual int STDMETHODCALLTYPE getMBInfo(Devinfo *) = 0;
    virtual int STDMETHODCALLTYPE getModuleInfo(Devinfo *) = 0;
    virtual int STDMETHODCALLTYPE setSSGVolume(UCHAR) = 0;
    virtual int STDMETHODCALLTYPE getSSGVolume(UCHAR *) = 0;
    virtual int STDMETHODCALLTYPE setPLLClock(UINT) = 0;
    virtual int STDMETHODCALLTYPE getPLLClock(UINT *) = 0;
    virtual int STDMETHODCALLTYPE getModuleType(int *) = 0;
};
} // namespace c86abi
class C86Output {
    HMODULE dll = nullptr;
    c86abi::Base *base = nullptr;
    c86abi::Chip *chip = nullptr;
    bool initialized = false;
    const std::atomic<uint32_t> *channelOverride = nullptr;
    const std::atomic<uint32_t> *channelMask = nullptr;
    std::mutex mutex;
    std::chrono::steady_clock::time_point origin;
    uint64_t clockOrigin = 0;
    bool timed = false;
    int delayMs = 0;
    C86Gain levels;
    void send(int a, int v) {
        if (chip)
            levels.write(a, v, [&](int address, int value) { chip->out(UINT(address), UCHAR(value)); });
    }
    struct Event {
        std::chrono::steady_clock::time_point at;
        int address, value;
    };
    std::deque<Event> events;
    std::condition_variable wake;
    std::thread sender;
    bool quitting = false, timerResolution = false;
    void sendLoop() {
        std::unique_lock<std::mutex> lock(mutex);
        while (!quitting) {
            if (events.empty()) {
                wake.wait(lock);
                continue;
            }
            auto at = events.front().at;
            if (std::chrono::steady_clock::now() < at) {
                wake.wait_until(lock, at);
                continue;
            }
            auto e = events.front();
            events.pop_front();
            int channel = e.address == 8 ? (e.value & 7) : (e.address >= 0x20 ? (e.address & 7) : -1);
            bool overridden = channel >= 0 && channelOverride && (channelOverride->load() & (1u << channel));
            bool masked = channel >= 0 && channelMask && (channelMask->load() & (1u << channel)) &&
                          (e.address == 8 || e.address >= 0x28);
            if (chip && !overridden && !masked)
                send(e.address, e.value);
        }
    }

  public:
    ~C86Output() { close(); }
    void setDelay(int ms) {
        std::lock_guard<std::mutex> lock(mutex);
        delayMs = std::clamp(ms, 0, 2000);
    }
    void setGain(double master, double fm) {
        std::lock_guard<std::mutex> lock(mutex);
        levels.setGain(master, fm, [&](int address, int value) {
            if (chip) chip->out(UINT(address), UCHAR(value));
        });
    }

    void close() noexcept {
        {
            std::lock_guard<std::mutex> lock(mutex);
            quitting = true;
            events.clear();
            wake.notify_all();
        }
        if (sender.joinable())
            sender.join();
        std::lock_guard<std::mutex> lock(mutex);
        if (timerResolution) {
            timeEndPeriod(1);
            timerResolution = false;
        }
        if (chip) {
            chip->reset();
            chip->Release();
            chip = nullptr;
        }
        if (base) {
            if (initialized)
                base->deinitialize();
            base->Release();
            base = nullptr;
        }
        if (dll) {
            FreeLibrary(dll);
            dll = nullptr;
        }
        initialized = false;
        timed = false;
    }
    void open(const std::filesystem::path &path) {
        close();
        try {
            dll = LoadLibraryExW(path.c_str(), nullptr,
                                 LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
            if (!dll)
                throw std::runtime_error("Cannot load x64 c86ctl.dll beside the executable");
            auto create =
                reinterpret_cast<HRESULT(WINAPI *)(REFIID, void **)>(GetProcAddress(dll, "CreateInstance"));
            if (!create || FAILED(create(c86abi::baseId, reinterpret_cast<void **>(&base))) || !base)
                throw std::runtime_error("C86CTL CreateInstance failed");
            if (base->initialize() != 0)
                throw std::runtime_error("C86CTL initialize failed");
            initialized = true;
            for (int i = 0; i < base->getNumberOfChip(); ++i) {
                c86abi::Chip3 *c = nullptr;
                int type = 0;
                if (SUCCEEDED(base->getChipInterface(i, c86abi::chip3Id, reinterpret_cast<void **>(&c))) &&
                    c) {
                    if (c->getChipType(&type) == 0 && (type & 65535) == 2)
                        chip = c;
                    else
                        c->Release();
                }
                c86abi::Gimic *g = nullptr;
                if (SUCCEEDED(base->getChipInterface(i, c86abi::gimicId, reinterpret_cast<void **>(&g))) &&
                    g) {
                    if (!chip && g->getModuleType(&type) == 0 && (type & 65535) == 2)
                        base->getChipInterface(i, c86abi::chipId, reinterpret_cast<void **>(&chip));
                    if (chip && g->setPLLClock(4000000) != 0) {
                        g->Release();
                        throw std::runtime_error("C86CTL cannot set OPM clock to 4 MHz");
                    }
                    g->Release();
                }
                if (chip)
                    break;
            }
            if (!chip)
                throw std::runtime_error("C86CTL: no YM2151 / OPM device found");
            reset();
            quitting = false;
            timerResolution = timeBeginPeriod(1) == TIMERR_NOERROR;
            sender = std::thread([this] { sendLoop(); });
        } catch (...) {
            close();
            throw;
        }
    }
    void setChannelOverride(const std::atomic<uint32_t> *value, const std::atomic<uint32_t> *mask = nullptr) {
        std::lock_guard<std::mutex> lock(mutex);
        channelOverride = value;
        channelMask = mask;
    }
    bool active() const { return chip != nullptr; }
    void reset() {
        std::lock_guard<std::mutex> lock(mutex);
        timed = false;
        levels.reset();
        events.clear();
        wake.notify_all();
        if (chip && chip->reset() != 0)
            throw std::runtime_error("C86CTL reset failed");
    }
    void write(int a, int v) {
        std::lock_guard<std::mutex> lock(mutex);
        if (chip)
            send(a, v);
    }
    void scheduled(uint64_t sample, int a, int v, const std::atomic<bool> &stop) {
        if (stop.load())
            return;
        std::lock_guard<std::mutex> lock(mutex);
        if (!chip || quitting)
            return;
        if (!timed) {
            origin = std::chrono::steady_clock::now();
            clockOrigin = sample;
            timed = true;
        }
        if (events.size() >= 262144)
            throw std::runtime_error("C86CTL output queue overflow");
        events.push_back({origin + std::chrono::milliseconds(delayMs) +
                              std::chrono::microseconds((sample - clockOrigin) * 16),
                          a, v});
        wake.notify_one();
    }
};
} // namespace mdx
