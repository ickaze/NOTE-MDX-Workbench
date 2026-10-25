// SPDX-License-Identifier: 0BSD
// Copyright (C) 2026 by I.C.KaZe
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace workbench
{
using TrackSteps = std::array<std::uint64_t, 16>;

// Text is UTF-8, editor positions are UTF-16, compiler columns are CP932 bytes.
// These units must never be interchanged at the UI / compiler boundary.
std::u16string toUtf16(const std::string &text);
std::string fromUtf16(const std::u16string &text);
std::string normalizeSourceText(std::string text);
std::string formatLineSteps(const TrackSteps &values);
std::string compactNoteLength(std::uint64_t steps);
std::string noteRemainder(std::uint64_t steps);
std::string formatLineNoteLengths(const TrackSteps &values);
int mmlTokenEnd(const std::u16string &text, int begin);
bool pianoBlack(int note);
struct PianoKeyBounds
{
    int left;
    int right;
    bool black;
};
PianoKeyBounds pianoKeyBounds(int note, int width);
int sliderClickValue(int x, int left, int width, int minimum, int maximum);
int firstEnabledTrack(std::uint32_t mask);
int editorFollowTrack(int mode, std::uint32_t mask);
int restoredEditorFollow(std::optional<int> mode, bool previous);
bool isPdxEditorClick(int id, std::uint16_t code, std::uintptr_t source, std::uintptr_t button);

// Reuse one document index for all compiler events. Positions are UTF-16,
// columns are one-based encoded byte offsets. The callback supplies CP932 width.
class SourcePositions
{
  public:
    SourcePositions(const std::string &text, std::function<int(const std::string &)> byteWidth);
    std::pair<int, int> location(std::uint32_t offset);
    int character(int line, int column);
  private:
    struct Row
    {
        std::vector<int> positions;
        std::vector<int> columns;
    };
    const Row &row(std::size_t index);
    std::u16string units_;
    std::vector<int> starts_;
    std::map<std::size_t, Row> rows_;
    std::function<int(const std::string &)> byteWidth_;
};

struct EditorCheckTicket
{
    std::uint64_t generation = 0;
    std::uint64_t revision = 0;
};
class EditorCheckGate
{
  public:
    using State = std::array<std::uint64_t, 4>;
    State state() const;
    void restore(const State &state);
    // Monotonic milliseconds; nullopt corresponds to Go's time.Time{}.
    void change(std::int64_t now);
    void cancel();
    std::optional<EditorCheckTicket> take(std::int64_t now, std::uint64_t revision, std::uint64_t delay = 500);
    bool accept(EditorCheckTicket ticket, std::uint64_t revision) const;
  private:
    std::uint64_t generation_ = 0;
    std::optional<std::int64_t> editedAt_;
    bool queued_ = false;
};
struct SourceEvent
{
    int track = 0;
    int line = 0;
    std::uint64_t steps = 0;
};
struct EditorDiagnostic
{
    bool error = false;
    int line = 0;
    int column = 0;
};
using EditorErrorSpan = std::pair<std::int32_t, std::int32_t>;
std::map<int, TrackSteps> accumulateLineSteps(const std::vector<SourceEvent> &events);
std::vector<EditorErrorSpan> buildErrorSpans(
    const std::string &text, const std::vector<EditorDiagnostic> &diagnostics,
    const std::function<int(const std::string &)> &byteWidth);
std::string formatGutterRow(int line, const TrackSteps &steps);
std::string buildGutterText(const std::string &text, const std::map<int, TrackSteps> &steps);

std::pair<std::string, bool> pcmFileName(const std::string &text);
std::pair<std::string, bool> pdxReferenceName(const std::string &text);
std::string pdxProjectName(const std::string &text, const std::string &fallback);
std::pair<std::string, bool> ensurePcmFileDirective(const std::string &text, const std::string &name);
std::string ensureOverwriteDirective(const std::string &text);
std::string appendVoiceDefinition(const std::string &text, const std::string &voice, int anchorEnd);
std::string voiceWithOffsetScope(const std::string &voice, int offset);
std::vector<std::uint8_t> tagVgm(const std::vector<std::uint8_t> &data, const std::string &title);
}
