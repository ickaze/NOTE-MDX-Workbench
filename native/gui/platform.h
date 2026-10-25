// SPDX-License-Identifier: 0BSD
// Copyright (C) 2026 by I.C.KaZe
#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "core_api.h"
#include <cstring>
#include "json.h"
#include "text_logic.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <commctrl.h>
#include <commdlg.h>
#include <deque>
#include <filesystem>
#include <memory>
#include <mmsystem.h>
#include <mutex>
#include <richedit.h>
#include <set>
#include <thread>
#include <windowsx.h>
namespace gui
{
using workbench::Json;
namespace fs = std::filesystem;
using Path = fs::path;
inline std::wstring wide(const std::string &s)
{
    if (s.empty())
    {
        return {};
    }
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), int(s.size()), nullptr, 0);
    if (!n)
    {
        throw std::runtime_error("Invalid UTF-8 text");
    }
    std::wstring v(n, 0);
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), int(s.size()), v.data(), n);
    return v;
}
inline std::string utf8(const std::wstring &s)
{
    if (s.empty())
    {
        return {};
    }
    int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0, nullptr, nullptr);
    std::string v(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, s.data(), int(s.size()), v.data(), n, nullptr, nullptr);
    return v;
}
inline std::wstring decode(const std::string &s)
{
    if (s.empty())
    {
        return {};
    }
    auto data = s.data();
    int len = int(s.size());
    if (len >= 3 && s.compare(0, 3, "\xef\xbb\xbf") == 0)
    {
        data += 3;
        len -= 3;
    }
    UINT cp = CP_UTF8;
    int n = MultiByteToWideChar(cp, MB_ERR_INVALID_CHARS, data, len, nullptr, 0);
    if (!n)
    {
        cp = 932;
        n = MultiByteToWideChar(cp, 0, data, len, nullptr, 0);
    }
    std::wstring v(n, 0);
    MultiByteToWideChar(
        cp,
        [&]() -> DWORD
        {
            if (cp == CP_UTF8)
            {
                return MB_ERR_INVALID_CHARS;
            }
            return 0;
        }(),
        data, len, v.data(), n);
    return wide(workbench::normalizeSourceText(utf8(v)));
}
inline Path extended(const Path &p)
{
    auto v = fs::absolute(p).wstring();
    if (v.rfind(L"\\\\?\\", 0) == 0)
    {
        return v;
    }
    if (v.rfind(L"\\\\", 0) == 0)
    {
        return L"\\\\?\\UNC\\" + v.substr(2);
    }
    return L"\\\\?\\" + v;
}
struct Handle
{
    HANDLE value = INVALID_HANDLE_VALUE;
    explicit Handle(HANDLE v) : value(v)
    {
    }
    ~Handle()
    {
        if (value != INVALID_HANDLE_VALUE)
        {
            CloseHandle(value);
        }
    }
    Handle(const Handle &) = delete;
};
inline std::runtime_error ioError(const char *operation, const Path &path)
{
    return std::runtime_error(std::string(operation) + ": " + utf8(path.wstring()) + " (Windows " +
                              std::to_string(GetLastError()) + ")");
}
inline std::string read(const Path &path)
{
    Handle f(CreateFileW(extended(path).c_str(), GENERIC_READ,
                         FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                         FILE_ATTRIBUTE_NORMAL, nullptr));
    if (f.value == INVALID_HANDLE_VALUE)
    {
        throw ioError("Read", path);
    }
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(f.value, &size) || size.QuadPart > 128 * 1024 * 1024 || size.QuadPart < 0)
    {
        throw ioError("Read size", path);
    }
    std::string out(std::size_t(size.QuadPart), 0);
    std::size_t at = 0;
    while (at < out.size())
    {
        DWORD n = 0;
        if (!ReadFile(f.value, out.data() + at, DWORD(std::min<std::size_t>(65536, out.size() - at)), &n,
                      nullptr) ||
            !n)
        {
            throw ioError("Read", path);
        }
        at += n;
    }
    return out;
}
inline void write(const Path &path, const std::string &data, DWORD creation = CREATE_ALWAYS)
{
    Handle f(CreateFileW(extended(path).c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, creation,
                         FILE_ATTRIBUTE_NORMAL, nullptr));
    if (f.value == INVALID_HANDLE_VALUE)
    {
        throw ioError("Write", path);
    }
    std::size_t at = 0;
    while (at < data.size())
    {
        DWORD n = 0;
        if (!WriteFile(f.value, data.data() + at, DWORD(std::min<std::size_t>(65536, data.size() - at)), &n,
                       nullptr) ||
            !n)
        {
            throw ioError("Write", path);
        }
        at += n;
    }
    if (!FlushFileBuffers(f.value))
    {
        throw ioError("Flush", path);
    }
}
inline bool exists(const Path &p)
{
    return GetFileAttributesW(extended(p).c_str()) != INVALID_FILE_ATTRIBUTES;
}
inline void remove(const Path &p)
{
    if (!p.empty())
    {
        DeleteFileW(extended(p).c_str());
    }
}
inline Path temporary(const Path &dir, const wchar_t *suffix = L".tmp")
{
    static std::atomic<unsigned> serial{0};
    for (unsigned i = 0; i < 1000; ++i)
    {
        Path p = dir / (L"workbench-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
                        std::to_wstring(GetTickCount64()) + L"-" + std::to_wstring(serial++) + suffix);
        Handle f(CreateFileW(extended(p).c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                             FILE_ATTRIBUTE_NORMAL, nullptr));
        if (f.value != INVALID_HANDLE_VALUE)
        {
            return p;
        }
        if (GetLastError() != ERROR_FILE_EXISTS)
        {
            throw ioError("Create temporary file", p);
        }
    }
    throw std::runtime_error("Cannot allocate temporary filename");
}
struct TemporaryFiles
{
    std::vector<Path> paths;
    ~TemporaryFiles()
    {
        for (const auto &p : paths)
        {
            gui::remove(p);
        }
    }
    Path make(const Path &dir, const wchar_t *suffix = L".tmp")
    {
        paths.push_back(temporary(dir, suffix));
        return paths.back();
    }
};
inline void atomicWrite(const Path &path, const std::string &data)
{
    TemporaryFiles files;
    auto stage = files.make(fs::absolute(path).parent_path());
    write(stage, data);
    if (!MoveFileExW(extended(stage).c_str(), extended(path).c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    {
        throw ioError("Replace", path);
    }
}
inline Path executableDirectory()
{
    std::wstring s(32768, 0);
    DWORD n = GetModuleFileNameW(nullptr, s.data(), DWORD(s.size()));
    if (!n || n == s.size())
    {
        throw std::runtime_error("Cannot locate executable");
    }
    s.resize(n);
    return Path(s).parent_path();
}
inline std::wstring trim(std::wstring v)
{
    auto a = v.find_first_not_of(L" \t\r\n");
    if (a == std::wstring::npos)
    {
        return {};
    }
    return v.substr(a, v.find_last_not_of(L" \t\r\n") - a + 1);
}
inline std::wstring safeName(std::wstring s)
{
    s = trim(s);
    if (s.empty())
    {
        s = L"editor";
    }
    s = Path(s).filename().stem().wstring();
    for (auto &c : s)
    {
        if (std::wstring(L"<>:\"/\\|?*").find(c) != std::wstring::npos)
        {
            c = L'_';
        }
    }
    if (s.empty())
    {
        return L"editor";
    }
    return s;
}
inline std::string encodeSource(const std::wstring &text, const std::string &original)
{
    bool bom = original.compare(0, 3, "\xef\xbb\xbf") == 0;
    bool utf = original.empty() || MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, original.data(),
                                                       int(original.size()), nullptr, 0) != 0;
    std::string normalized = workbench::normalizeSourceText(utf8(text));
    if (original.find("\r\n") != std::string::npos)
    {
        std::string out;
        for (char c : normalized)
        {
            if (c == '\n')
            {
                out += '\r';
            }
            out += c;
        }
        normalized = std::move(out);
    }
    if (utf)
    {
        if (bom)
        {
            return "\xef\xbb\xbf" + normalized;
        }
        return normalized;
    }
    auto units = wide(normalized);
    BOOL used = FALSE;
    int n = WideCharToMultiByte(932, WC_NO_BEST_FIT_CHARS, units.data(), int(units.size()), nullptr, 0,
                                nullptr, &used);
    if (!n || used)
    {
        throw std::runtime_error("MML cannot be encoded as CP932");
    }
    std::string out(n, 0);
    WideCharToMultiByte(932, WC_NO_BEST_FIT_CHARS, units.data(), int(units.size()), out.data(), n, nullptr,
                        &used);
    if (used)
    {
        throw std::runtime_error("MML cannot be encoded as CP932");
    }
    return out;
}
using NativeState = BridgeState;
using NativeRequest = BridgeRequest;
class Core
{
    HMODULE dll = nullptr;

  public:
    template <class Function> Function resolve(const char *name) const
    {
        auto address = GetProcAddress(dll, name);
        if (!address)
        {
            throw std::runtime_error(std::string("Missing native API: ") + name);
        }
        return reinterpret_cast<Function>(address);
    }

  public:
#define WB_DECLARE_MEMBER(name) decltype(&::name) name = nullptr;
    WB_CORE_FUNCTIONS(WB_DECLARE_MEMBER)
#undef WB_DECLARE_MEMBER
    void load(const Path &dir)
    {
        dll = LoadLibraryExW((dir / L"workbench_core.dll").c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
        if (!dll)
        {
            throw ioError("Load", dir / L"workbench_core.dll");
        }
#define WB_LOAD_MEMBER(name) name = resolve<decltype(name)>(#name);
        WB_CORE_FUNCTIONS(WB_LOAD_MEMBER)
#undef WB_LOAD_MEMBER
    }
    std::string error() const
    {
        char text[8192]{};
        WB_Error(text, sizeof(text));
        return text;
    }
    void require(int ok) const
    {
        if (!ok)
        {
            throw std::runtime_error(error());
        }
    }
    void command(int n, int value = 0, const Path &path = {}, const Path &bank = {},
                 const Path &source = {}) const
    {
        NativeRequest request{n, value, path.c_str(), bank.c_str(), source.c_str(), nullptr};
        require(WB_Command(&request));
    }
    Json compile(const Path &path, const std::wstring &text, const Path &output, int bl = 0, int bc = 0,
                 int el = 0, int ec = 0) const
    {
        auto proc = WB_CompileDecorated;
        if (bl)
        {
            proc = WB_Compile;
        }
        std::vector<char> buffer(1024 * 1024);
        auto invoke = [&]
        {
            return proc(path.c_str(), text.c_str(), output.c_str(), bl, bc, el, ec, buffer.data(),
                        buffer.size());
        };
        int n = invoke();
        if (n > int(buffer.size()) && n <= 64 * 1024 * 1024)
        {
            buffer.resize(n);
            n = invoke();
        }
        require(n);
        if (n < 1 || n > int(buffer.size()))
        {
            throw std::runtime_error("Compiler result exceeds buffer limit");
        }
        return Json::parse(std::string(buffer.data(), n - 1));
    }
    void unload()
    {
        if (dll)
        {
            FreeLibrary(dll);
            dll = nullptr;
        }
    }
};
} // namespace gui
