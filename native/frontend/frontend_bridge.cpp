// SPDX-License-Identifier: 0BSD
// Copyright (C) 2026 by I.C.KaZe
#include "text_logic.h"
#include "source_watch.h"
#include <cstring>
#include <algorithm>
#include <limits>
#include <stdexcept>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#define WB_FRONTEND_EXPORT extern "C" __declspec(dllexport)
#else
#define WB_FRONTEND_EXPORT extern "C"
#endif

namespace
{
std::string boolean(bool value)
{
    if (value) { return "1"; }
    return "0";
}
std::string paired(const std::pair<std::string, bool> &value)
{
    return boolean(value.second) + ":" + value.first;
}
int positionByteWidth(const std::string &text)
{
#ifdef _WIN32
    const auto units = workbench::toUtf16(text);
    const std::wstring wide(units.begin(), units.end());
    return WideCharToMultiByte(932, 0, wide.data(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
#else
    // Deterministic test callback; Windows uses the actual system CP932 table.
    int width = 0;
    for (const auto unit : workbench::toUtf16(text))
    {
        if (unit >= 0xdc00 && unit <= 0xdfff) { continue; }
        width += unit < 128 ? 1 : 2;
    }
    return width;
#endif
}
std::string evaluate(std::uint32_t operation, const std::string &left,
    const std::string &right, const std::vector<std::uint64_t> &args)
{
    using namespace workbench;
    const auto integer = [&](std::size_t i) { return static_cast<int>(static_cast<std::int64_t>(args.at(i))); };
    TrackSteps steps{};
    if (operation == 4 || operation == 5)
    {
        if (args.size() != 16) { throw std::invalid_argument("Expected 16 track values"); }
        std::copy(args.begin(), args.end(), steps.begin());
    }
    switch (operation)
    {
    case 1: return normalizeSourceText(left);
    case 2: return compactNoteLength(args.at(0));
    case 3: return noteRemainder(args.at(0));
    case 4: return formatLineSteps(steps);
    case 5: return formatLineNoteLengths(steps);
    case 6: return std::to_string(mmlTokenEnd(toUtf16(left), integer(0)));
    case 7: return boolean(pianoBlack(integer(0)));
    case 8:
    {
        const auto key = pianoKeyBounds(integer(0), integer(1));
        return std::to_string(key.left) + "," + std::to_string(key.right) + "," + boolean(key.black);
    }
    case 9: return std::to_string(sliderClickValue(integer(0), integer(1), integer(2), integer(3), integer(4)));
    case 10: return std::to_string(firstEnabledTrack(static_cast<std::uint32_t>(args.at(0))));
    case 11: return std::to_string(editorFollowTrack(integer(0), static_cast<std::uint32_t>(args.at(1))));
    case 12:
    {
        std::optional<int> mode;
        if (args.at(0)) { mode = integer(1); }
        return std::to_string(restoredEditorFollow(mode, args.at(2) != 0));
    }
    case 13: return boolean(isPdxEditorClick(integer(0), static_cast<std::uint16_t>(args.at(1)), args.at(2), args.at(3)));
    case 14: return paired(pcmFileName(left));
    case 15: return paired(pdxReferenceName(left));
    case 16: return pdxProjectName(left, right);
    case 17: return paired(ensurePcmFileDirective(left, right));
    case 18: return ensureOverwriteDirective(left);
    case 19: return appendVoiceDefinition(left, right, integer(0));
    case 20: return voiceWithOffsetScope(left, integer(0));
    case 22:
    {
        SourcePositions positions(left, positionByteWidth);
        const auto value = positions.location(static_cast<std::uint32_t>(args.at(0)));
        return std::to_string(value.first) + "," + std::to_string(value.second);
    }
    case 23:
    {
        SourcePositions positions(left, positionByteWidth);
        return std::to_string(positions.character(integer(0), integer(1)));
    }
    case 21:
    {
        const auto bytes = tagVgm(std::vector<std::uint8_t>(left.begin(), left.end()), right);
        return std::string(bytes.begin(), bytes.end());
    }
    default: throw std::invalid_argument("Unknown frontend operation");
    }
}
}

// Four caller-owned words form a value snapshot, never a retained native handle.
// The caller serializes operations on a particular gate. Nanoseconds preserve
// the original 500 ms boundary without rounding an edit time to milliseconds.
WB_FRONTEND_EXPORT int WB_EditorGate(std::uint64_t *state, std::uint32_t operation,
    std::int64_t now, std::uint64_t revision, std::uint64_t generation,
    std::uint64_t ticketRevision, std::uint64_t *ticket)
{
    if (!state || !ticket || operation > 4) { return -1; }
    workbench::EditorCheckGate gate;
    workbench::EditorCheckGate::State snapshot;
    std::copy_n(state, snapshot.size(), snapshot.begin());
    gate.restore(snapshot);
    int result = 0;
    switch (operation)
    {
    case 0: gate.change(now); break;
    case 1: gate.cancel(); break;
    case 2:
        if (const auto value = gate.take(now, revision, 500000000))
        {
            ticket[0] = value->generation;
            ticket[1] = value->revision;
            result = 1;
        }
        break;
    case 3: result = gate.accept({generation, ticketRevision}, revision); break;
    case 4:
        // Change(time.Time{}) increments the generation but cannot be queued.
        gate.change(now);
        snapshot = gate.state(); snapshot[2] = 0; gate.restore(snapshot);
        break;
    }
    snapshot = gate.state();
    std::copy(snapshot.begin(), snapshot.end(), state);
    return result;
}

#ifdef _WIN32
WB_FRONTEND_EXPORT int WB_PollSourceWatch(std::uint64_t *words,
    const wchar_t *source, const wchar_t *pdx)
{
    if (!words || !source || !pdx)
    {
        return -1;
    }
    try
    {
        workbench::SourceWatchState state{};
        std::copy_n(words, state.size(), state.begin());
        const bool changed = workbench::pollSourceWatch(state, source, pdx);
        std::copy(state.begin(), state.end(), words);
        return changed ? 1 : 0;
    }
    catch (...)
    {
        return -1;
    }
}
#endif

// UTF-8 and binary-safe lengths; args are fixed-width 64-bit values, never Go
// objects. The callee retains no caller pointers. Query with capacity == 0.
// Positive return: required capacity including sentinel NUL; negative: same for
// an error string. The result may contain embedded NULs (VGM and source text).
WB_FRONTEND_EXPORT std::int64_t WB_FrontendText(std::uint32_t operation,
    const char *left, std::size_t leftSize, const char *right, std::size_t rightSize,
    const std::uint64_t *args, std::size_t argCount, char *out, std::size_t capacity)
{
    std::string result;
    bool failed = false;
    try
    {
        if ((leftSize && !left) || (rightSize && !right) || (argCount && !args) ||
            argCount > 16)
        {
            throw std::invalid_argument("Invalid frontend arguments");
        }
        std::string l;
        std::string r;
        std::vector<std::uint64_t> numbers;
        if (leftSize) { l.assign(left, leftSize); }
        if (rightSize) { r.assign(right, rightSize); }
        if (argCount) { numbers.assign(args, args + argCount); }
        result = evaluate(operation, l, r, numbers);
    }
    catch (const std::exception &error)
    {
        failed = true;
        result = error.what();
    }
    catch (...)
    {
        failed = true;
        result = "Frontend operation failed";
    }
    const auto required = result.size() + 1;
    if (out && capacity >= required)
    {
        std::memcpy(out, result.data(), result.size());
        out[result.size()] = 0;
    }
    const auto value = static_cast<std::int64_t>(required);
    if (failed) { return -value; }
    return value;
}
