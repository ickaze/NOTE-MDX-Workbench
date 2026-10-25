// x86-only DLL host. Calls the public C API; no X68Sound implementation is embedded.
#define NOMINMAX
#include <windows.h>
#include <filesystem>
#include <string>
#include <stdexcept>
#include <cstring>
#include <array>
#include "x68_render.h"
template <class T> T symbol(HMODULE dll, const char *name) {
    auto p = reinterpret_cast<T>(GetProcAddress(dll, name));
    if (!p)
        throw std::runtime_error(std::string("Missing DLL export: ") + name);
    return p;
}
int wmain(int argc, wchar_t **argv) {
    static_assert(sizeof(void *) == 4, "Build the X68Sound helper for Win32 / x86");
    if (argc != 2)
        return 1;
    std::wstring name = argv[1];
    HANDLE map = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, name.c_str());
    auto s = map ? static_cast<mdx::X68Shared *>(
                       MapViewOfFile(map, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(mdx::X68Shared)))
                 : nullptr;
    HANDLE request = OpenEventW(SYNCHRONIZE, FALSE, (name + L"-request").c_str());
    HANDLE reply = OpenEventW(EVENT_MODIFY_STATE, FALSE, (name + L"-reply").c_str());
    if (!s || !request || !reply)
        return 2;
    HMODULE dll = nullptr;
    void(__cdecl * freeSound)() = nullptr;
    try {
        if (s->magic != mdx::x68Magic)
            throw std::runtime_error("Bridge protocol mismatch");
        wchar_t path[32768];
        GetModuleFileNameW(nullptr, path, 32768);
        auto file = std::filesystem::path(path).parent_path() / L"X68Sound.dll";
        dll = LoadLibraryExW(file.c_str(), nullptr,
                             LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!dll)
            throw std::runtime_error("Cannot load X68Sound.dll (32bit required) beside executable");
        auto start = symbol<int(__cdecl *)(int, int, int, int)>(dll, "X68Sound_StartPcm");
        auto get = symbol<int(__cdecl *)(void *, int)>(dll, "X68Sound_GetPcm");
        auto reg = symbol<void(__cdecl *)(unsigned char)>(dll, "X68Sound_OpmReg");
        auto poke = symbol<void(__cdecl *)(unsigned char)>(dll, "X68Sound_OpmPoke");
        freeSound = symbol<void(__cdecl *)()>(dll, "X68Sound_Free");
        auto clock = reinterpret_cast<int(__cdecl *)(int)>(GetProcAddress(dll, "X68Sound_OpmClock"));
        // 44.1 kHz is supported by the original DLL. Preserve the 62.5 kHz
        // sequencer clock with a rational 44100/62500 sample accumulator.
        if (start(44100, 1, 0, 5) != 0)
            throw std::runtime_error("X68Sound_StartPcm failed");
        if (clock && clock(4000000) != 0)
            throw std::runtime_error("X68Sound_OpmClock failed");
        auto wait = reinterpret_cast<int(__cdecl *)(int)>(GetProcAddress(dll, "X68Sound_OpmWait"));
        if (wait)
            wait(1); // Zero expands to 65,535 queue polls per internal sample in classic DLLs.
                     // One gives 160 polls, enough for an OPM register burst without that busy loop.
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
        s->status = 0;
        SetEvent(reply);
        mdx::X68BlockRenderer renderer;
        while (WaitForSingleObject(request, INFINITE) == WAIT_OBJECT_0) {
            if (s->command == 2)
                break;
            if (s->command != 1 || s->count > mdx::x68Events)
                throw std::runtime_error("Invalid bridge command");
            renderer.render(
                *s,
                [&](uint8_t a, uint8_t v) {
                    reg(a);
                    poke(v);
                },
                [&](int16_t *out, unsigned count) {
                    if (get(out, int(count)) < 0)
                        throw std::runtime_error("X68Sound_GetPcm failed");
                });
            SetEvent(reply);
        }
    } catch (const std::exception &e) {
        s->status = 1;
        std::strncpy(s->error, e.what(), sizeof(s->error) - 1);
        SetEvent(reply);
    }
    if (freeSound)
        freeSound();
    if (dll)
        FreeLibrary(dll);
    UnmapViewOfFile(s);
    CloseHandle(map);
    CloseHandle(request);
    CloseHandle(reply);
    return 0;
}
