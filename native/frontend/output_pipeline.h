// SPDX-License-Identifier: 0BSD
// Copyright (C) 2026 by I.C.KaZe
#pragma once
#include <filesystem>
#include <functional>
#include <string>

namespace workbench
{
enum class OutputFormat : unsigned { Copy, Vgm, Vgz, Mrz };
struct OutputControl
{
    // Checkpoints may block for pause, or throw to cancel. No caller pointers
    // are retained after finalizeOutput/publishOutput return.
    std::function<void()> checkpoint;
    std::function<void(int)> progress;
};
// All transformation happens in a current-directory temporary file. The final
// rename is the publication point. Cancellation before it preserves the target.
void finalizeOutput(const std::filesystem::path &source,
    const std::filesystem::path &target, OutputFormat format,
    const std::string &title, const OutputControl &control);
// Moves an already staged file. Cross-volume moves use a target-directory
// staging copy followed by a same-volume rename, never a copy over the target.
void publishOutput(const std::filesystem::path &source,
    const std::filesystem::path &target, const OutputControl &control);
}
