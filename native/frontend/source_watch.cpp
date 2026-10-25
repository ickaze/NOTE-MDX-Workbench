// SPDX-License-Identifier: 0BSD
// Copyright (C) 2026 by I.C.KaZe
#include "source_watch.h"
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <string>
#include <vector>
#endif

namespace workbench
{
bool observeSourceWatch(SourceWatchState &state,
    std::optional<std::uint64_t> source, std::optional<std::uint64_t> pdx)
{
    if (!source)
    {
        return false;
    }
    bool changed = state[0] != 0 && state[1] != *source;
    state[0] = 1;
    state[1] = *source;
    if (pdx)
    {
        changed = changed || (state[2] != 0 && *pdx > state[3]);
        state[2] = 1;
        state[3] = *pdx;
    }
    return changed;
}
#ifdef _WIN32
namespace
{
std::optional<std::wstring> watchPath(const wchar_t *path)
{
    if (!path || !*path)
    {
        return std::nullopt;
    }
    const std::wstring name(path);
    if (name.rfind(L"\\\\?\\", 0) == 0 || name.rfind(L"\\\\.\\", 0) == 0)
    {
        return name;
    }
    std::vector<wchar_t> buffer(256);
    for (;;)
    {
        const auto count = GetFullPathNameW(path, static_cast<DWORD>(buffer.size()), buffer.data(), nullptr);
        if (count == 0)
        {
            return std::nullopt;
        }
        if (count < buffer.size())
        {
            const std::wstring full(buffer.data(), count);
            if (full.rfind(L"\\\\", 0) == 0)
            {
                return L"\\\\?\\UNC\\" + full.substr(2);
            }
            return L"\\\\?\\" + full;
        }
        buffer.resize(static_cast<std::size_t>(count) + 1);
    }
}
std::optional<std::uint64_t> modificationTime(const wchar_t *path)
{
    const auto name = watchPath(path);
    if (!name)
    {
        return std::nullopt;
    }
    // Follow symbolic links as os.Stat does. Metadata access only; do not read
    // musical data or prevent the writer from replacing the observed file.
    const auto handle = CreateFileW(name->c_str(), FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (handle == INVALID_HANDLE_VALUE)
    {
        return std::nullopt;
    }
    FILETIME modified{};
    const bool valid = GetFileTime(handle, nullptr, nullptr, &modified) != 0;
    CloseHandle(handle);
    if (!valid)
    {
        return std::nullopt;
    }
    return (static_cast<std::uint64_t>(modified.dwHighDateTime) << 32) | modified.dwLowDateTime;
}
}
bool pollSourceWatch(SourceWatchState &state, const wchar_t *source, const wchar_t *pdx)
{
    const auto sourceTime = modificationTime(source);
    if (!sourceTime)
    {
        return false;
    }
    return observeSourceWatch(state, sourceTime, modificationTime(pdx));
}
#endif
}
