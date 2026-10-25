// SPDX-License-Identifier: 0BSD
// Copyright (C) 2026 by I.C.KaZe
#pragma once
#include <array>
#include <cstdint>
#include <optional>

namespace workbench
{
// Caller-owned snapshot: source known/time, PDX known/time. Failed observations
// retain their previous baseline. The UI serializes access to this snapshot.
using SourceWatchState = std::array<std::uint64_t, 4>;
bool observeSourceWatch(SourceWatchState &state,
    std::optional<std::uint64_t> source, std::optional<std::uint64_t> pdx);
#ifdef _WIN32
bool pollSourceWatch(SourceWatchState &state, const wchar_t *source, const wchar_t *pdx);
#endif
}
