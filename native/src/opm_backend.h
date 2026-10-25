#pragma once
#include "independent_opm.h"
#include <array>
#include <memory>
#include <stdexcept>
#include <string>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "x68_bridge.h"
#include <filesystem>
#include <windows.h>
#endif
namespace mdx {
// Windows calls the supplied C ABI DLL. Non-Windows tests link the identical C API.
struct OpmApi {
#ifdef _WIN32
    HMODULE module = nullptr;
#endif
#define OPM_PROC(name) decltype(&iopm_##name) name = nullptr
    OPM_PROC(abi_version);
    OPM_PROC(version);
    OPM_PROC(create);
    OPM_PROC(destroy);
    OPM_PROC(state_size);
    OPM_PROC(save_state);
    OPM_PROC(load_state);
    OPM_PROC(write_register);
    OPM_PROC(render_f32);
    OPM_PROC(render_channels_f32);
    OPM_PROC(render_events_f32);
    OPM_PROC(render_events_channels_f32);
    OPM_PROC(reset);
    OPM_PROC(set_measured_output_timing);
#undef OPM_PROC
    OpmApi() {
#ifdef _WIN32
        wchar_t path[32768];
        GetModuleFileNameW(nullptr, path, 32768);
        auto file = std::filesystem::path(path).parent_path() / L"independent_opm.dll";
        module = LoadLibraryExW(file.c_str(), nullptr,
                                LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!module)
            throw std::runtime_error("Cannot load independent_opm.dll beside the executable (x64 required)");
#define OPM_LOAD(name)                                                                                       \
    name = reinterpret_cast<decltype(name)>(GetProcAddress(module, "iopm_" #name));                          \
    if (!name) {                                                                                             \
        FreeLibrary(module);                                                                                 \
        module = nullptr;                                                                                    \
        throw std::runtime_error("Missing DLL export: iopm_" #name);                                         \
    }
#else
#define OPM_LOAD(name) name = &iopm_##name
#endif
        OPM_LOAD(abi_version);
        OPM_LOAD(version);
        OPM_LOAD(create);
        OPM_LOAD(destroy);
        OPM_LOAD(state_size);
        OPM_LOAD(save_state);
        OPM_LOAD(load_state);
        OPM_LOAD(write_register);
        OPM_LOAD(render_f32);
        OPM_LOAD(render_channels_f32);
        OPM_LOAD(render_events_f32);
        OPM_LOAD(render_events_channels_f32);
        OPM_LOAD(reset);
        OPM_LOAD(set_measured_output_timing);
#undef OPM_LOAD
        if (abi_version() != IOPM_ABI_VERSION) {
#ifdef _WIN32
            FreeLibrary(module);
            module = nullptr;
#endif
            throw std::runtime_error("Unsupported Independent OPM DLL ABI");
        }
    }
    ~OpmApi() {
#ifdef _WIN32
        if (module)
            FreeLibrary(module);
#endif
    }
    static OpmApi &get() {
        static OpmApi api;
        return api;
    }
    static void check(int result) {
        if (result != IOPM_OK)
            throw std::runtime_error("Independent OPM DLL call failed: " + std::to_string(result));
    }
};
class OpmBackend {
    OpmApi *api = nullptr;
#ifdef _WIN32
    std::unique_ptr<X68Bridge> x68;
#endif
    iopm_handle handle = nullptr;
    std::array<float, 2> samples{};
    std::array<float, 16> channels{};
    std::array<unsigned, 8> keys{};
    std::array<uint8_t,256> registers{};
    uint8_t amd=0, pmd=0;
    bool monitorEnabled=true, measuredTiming=false, audible=false;
    void writeDll(unsigned a,unsigned v) {
        OpmApi::check(api->write_register(handle,a,v));
    }
  public:
    struct Frame { double left, right; };
    struct State { bool key; };
    explicit OpmBackend(bool capture=false, bool sound=true, int backend=0) {
        audible=sound && backend==0;
#ifdef _WIN32
        if(sound && backend==1) x68=std::make_unique<X68Bridge>();
#endif
        if(capture || audible) {
            api=&OpmApi::get();
            OpmApi::check(api->create(4000000,62500,&handle));
        }
        monitorEnabled=capture;
    }
    ~OpmBackend() { if(handle) api->destroy(handle); }
    OpmBackend(const OpmBackend &)=delete;
    OpmBackend &operator=(const OpmBackend &)=delete;
    void write_register(uint8_t a,uint8_t v) {
        registers[a]=v;
        if(a==8) keys[v&7]=(v>>3)&15;
        if(a==0x19) { if(v&128) pmd=v; else amd=v; }
#ifdef _WIN32
        if(x68) x68->write(a,v);
#endif
        if(handle) writeDll(a,v);
    }
    void set_measured_output_timing(bool on) {
        measuredTiming=on;
        if(handle) OpmApi::check(api->set_measured_output_timing(handle,on));
    }
    void set_monitor_enabled(bool enabled) {
        if(enabled && !monitorEnabled && !audible && handle) {
            OpmApi::check(api->reset(handle));
            OpmApi::check(api->set_measured_output_timing(handle,measuredTiming));
            for(unsigned a=0;a<256;++a) if(a!=8 && a!=0x19)
                writeDll(a,registers[a]);
            writeDll(0x19,amd&127);
            writeDll(0x19,pmd|128);
            for(unsigned ch=0;ch<8;++ch)
                writeDll(8,ch|(keys[ch]<<3));
        }
        monitorEnabled=enabled;
        if(!enabled) channels.fill(0);
    }
    void advance(unsigned) {
        if(handle && (audible || monitorEnabled)) {
            std::array<float,2> mix{};
            if(monitorEnabled)
                OpmApi::check(api->render_channels_f32(handle,mix.data(),channels.data(),1));
            else OpmApi::check(api->render_f32(handle,mix.data(),1));
            if(audible) samples=mix;
        }
#ifdef _WIN32
        if(x68) samples=x68->advance();
#endif
    }
    Frame last_sample() const { return {samples[0],samples[1]}; }
    Frame channel_sample(unsigned ch) const {
        if(ch>=8 || !monitorEnabled) return {};
        // The DLL returns pre-pan signals; retain the player's pan/mute display.
        return {(registers[0x20+ch]&64) ? channels[ch*2] : 0,
                (registers[0x20+ch]&128) ? channels[ch*2+1] : 0};
    }
    State inspect(unsigned ch,unsigned op) const {
        constexpr unsigned bit[]={0,2,1,3};
        return {bool(keys[ch] & (1u<<bit[op]))};
    }
};
} // namespace mdx
