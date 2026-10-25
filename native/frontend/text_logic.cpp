// SPDX-License-Identifier: 0BSD
// Copyright (C) 2026 by I.C.KaZe
// C++ port of the v2.9.11 Go text/display helpers. See MIGRATION.md.
#include "text_logic.h"
#include <algorithm>
#include <limits>
#include <set>
#include <stdexcept>

namespace workbench
{
namespace
{
bool starts(const std::string &s, const std::string &prefix)
{
    return s.compare(0, prefix.size(), prefix) == 0;
}
void replaceAll(std::string &s, const std::string &from, const std::string &to)
{
    std::size_t pos = 0;
    while ((pos = s.find(from, pos)) != std::string::npos)
    {
        s.replace(pos, from.size(), to);
        pos += to.size();
    }
}
std::string lower(std::string s)
{
    for (char &c : s)
    {
        if (c >= 'A' && c <= 'Z')
        {
            c += 'a' - 'A';
        }
    }
    return s;
}
std::vector<std::string> splitLines(const std::string &text)
{
    std::vector<std::string> result;
    std::size_t begin = 0;
    for (;;)
    {
        const auto end = text.find('\n', begin);
        result.push_back(text.substr(begin, end - begin));
        if (end == std::string::npos)
        {
            return result;
        }
        begin = end + 1;
    }
}
std::string join(const std::vector<std::string> &parts, const std::string &separator)
{
    std::string result;
    bool first = true;
    for (const auto &part : parts)
    {
        if (!first)
        {
            result += separator;
        }
        first = false;
        result += part;
    }
    return result;
}
bool unicodeSpace(std::uint32_t c)
{
    return (c >= 9 && c <= 13) || c == 0x20 || c == 0x85 || c == 0xa0 ||
           c == 0x1680 || (c >= 0x2000 && c <= 0x200a) ||
           c == 0x2028 || c == 0x2029 || c == 0x202f || c == 0x205f || c == 0x3000;
}
std::string trim(const std::string &s)
{
    const auto units = toUtf16(s);
    auto begin = units.begin();
    auto end = units.end();
    while (begin != end && unicodeSpace(*begin))
    {
        ++begin;
    }
    while (end != begin && unicodeSpace(*(end - 1)))
    {
        --end;
    }
    return fromUtf16(std::u16string(begin, end));
}
std::string trimLeftAscii(std::string text)
{
    const auto at = text.find_first_not_of(" \t");
    if (at == std::string::npos)
    {
        return {};
    }
    return text.substr(at);
}
std::string firstField(const std::string &text)
{
    auto units = toUtf16(trim(text));
    const auto end = std::find_if(units.begin(), units.end(), unicodeSpace);
    return fromUtf16(std::u16string(units.begin(), end));
}
void adjustBraces(const std::string &original, int &braces)
{
    if (braces > 0 || starts(original, "@"))
    {
        const auto line = original.substr(0, original.find_first_of(";*"));
        braces += static_cast<int>(std::count(line.begin(), line.end(), '{'));
        braces -= static_cast<int>(std::count(line.begin(), line.end(), '}'));
        braces = std::max(0, braces);
    }
}
const std::array<std::string, 192> &lengthParts()
{
    // Immutable, initialized once; no parsing or allocation of the table per paint.
    static const auto labels = []
    {
        std::vector<std::pair<int, std::string>> durations;
        for (int denominator = 2; denominator <= 192; ++denominator)
        {
            if (192 % denominator != 0)
            {
                continue;
            }
            const int ticks = 192 / denominator;
            durations.emplace_back(ticks, std::to_string(denominator));
            int length = ticks;
            for (int dots = 1; dots <= 5; ++dots)
            {
                const int divisor = 1 << dots;
                if (ticks % divisor != 0)
                {
                    break;
                }
                length += ticks / divisor;
                if (length < 192)
                {
                    durations.emplace_back(length, std::to_string(denominator) + std::string(dots, '.'));
                }
            }
        }
        std::array<std::string, 192> result{};
        std::array<int, 192> counts{};
        for (int total = 1; total < 192; ++total)
        {
            counts[total] = 193;
            for (const auto &note : durations)
            {
                if (note.first > total)
                {
                    continue;
                }
                const int previous = total - note.first;
                std::string candidate = note.second;
                if (previous != 0)
                {
                    candidate += "+" + result[previous];
                }
                const int count = 1 + counts[previous];
                if (count < counts[total] ||
                    (count == counts[total] && candidate.size() < result[total].size()))
                {
                    counts[total] = count;
                    result[total] = std::move(candidate);
                }
            }
        }
        return result;
    }();
    return labels;
}
bool high(char16_t c) { return c >= 0xd800 && c <= 0xdbff; }
bool low(char16_t c) { return c >= 0xdc00 && c <= 0xdfff; }
}

std::u16string toUtf16(const std::string &text)
{
    std::u16string result;
    for (std::size_t i = 0; i < text.size();)
    {
        const auto lead = static_cast<unsigned char>(text[i]);
        std::uint32_t value = 0xfffd;
        int count = 1;
        if (lead < 0x80)
        {
            value = lead;
        }
        else
        {
            int expected = 0;
            std::uint32_t minimum = 0;
            if (lead >= 0xc2 && lead <= 0xdf) { expected = 2; minimum = 0x80; value = lead & 0x1f; }
            else if (lead >= 0xe0 && lead <= 0xef) { expected = 3; minimum = 0x800; value = lead & 0xf; }
            else if (lead >= 0xf0 && lead <= 0xf4) { expected = 4; minimum = 0x10000; value = lead & 7; }
            bool valid = expected > 0 && i + expected <= text.size();
            for (int j = 1; valid && j < expected; ++j)
            {
                const auto next = static_cast<unsigned char>(text[i + j]);
                valid = (next & 0xc0) == 0x80;
                value = (value << 6) | (next & 0x3f);
            }
            valid = valid && value >= minimum && value <= 0x10ffff && !(value >= 0xd800 && value <= 0xdfff);
            if (valid) { count = expected; }
            else { value = 0xfffd; }
        }
        i += count;
        if (value <= 0xffff)
        {
            result.push_back(static_cast<char16_t>(value));
        }
        else
        {
            value -= 0x10000;
            result.push_back(static_cast<char16_t>(0xd800 | (value >> 10)));
            result.push_back(static_cast<char16_t>(0xdc00 | (value & 0x3ff)));
        }
    }
    return result;
}
std::string fromUtf16(const std::u16string &text)
{
    std::string result;
    for (std::size_t i = 0; i < text.size(); ++i)
    {
        std::uint32_t c = text[i];
        if (high(text[i]) && i + 1 < text.size() && low(text[i + 1]))
        {
            c = 0x10000 + ((text[i] - 0xd800) << 10) + (text[i + 1] - 0xdc00);
            ++i;
        }
        else if (high(text[i]) || low(text[i])) { c = 0xfffd; }
        if (c < 0x80) { result += static_cast<char>(c); }
        else if (c < 0x800)
        {
            result += static_cast<char>(0xc0 | (c >> 6));
            result += static_cast<char>(0x80 | (c & 0x3f));
        }
        else if (c < 0x10000)
        {
            result += static_cast<char>(0xe0 | (c >> 12));
            result += static_cast<char>(0x80 | ((c >> 6) & 0x3f));
            result += static_cast<char>(0x80 | (c & 0x3f));
        }
        else
        {
            result += static_cast<char>(0xf0 | (c >> 18));
            result += static_cast<char>(0x80 | ((c >> 12) & 0x3f));
            result += static_cast<char>(0x80 | ((c >> 6) & 0x3f));
            result += static_cast<char>(0x80 | (c & 0x3f));
        }
    }
    return result;
}
SourcePositions::SourcePositions(const std::string &text,
    std::function<int(const std::string &)> byteWidth)
    : units_(toUtf16(text)), byteWidth_(std::move(byteWidth))
{
    if (units_.size() > static_cast<std::size_t>(std::numeric_limits<int>::max() / 2))
    {
        throw std::length_error("Source document exceeds position range");
    }
    starts_.push_back(0);
    for (std::size_t index = 0; index < units_.size(); ++index)
    {
        if (units_[index] == u'\r' || units_[index] == u'\n')
        {
            if (units_[index] == u'\r' && index + 1 < units_.size() && units_[index + 1] == u'\n')
            {
                ++index;
            }
            starts_.push_back(static_cast<int>(index + 1));
        }
    }
}
const SourcePositions::Row &SourcePositions::row(std::size_t index)
{
    const auto found = rows_.find(index);
    if (found != rows_.end())
    {
        return found->second;
    }
    Row value;
    int column = 1;
    auto position = static_cast<std::size_t>(starts_.at(index));
    for (;;)
    {
        value.positions.push_back(static_cast<int>(position));
        value.columns.push_back(column);
        if (position == units_.size() || units_[position] == u'\r' || units_[position] == u'\n')
        {
            break;
        }
        auto next = position + 1;
        if (high(units_[position]) && next < units_.size())
        {
            ++next;
        }
        const auto width = units_[position] < 128 ? 1 :
            byteWidth_(fromUtf16(units_.substr(position, next - position)));
        if (width < 0 || width > std::numeric_limits<int>::max() - column)
        {
            throw std::overflow_error("Source byte column exceeds position range");
        }
        column += width;
        position = next;
    }
    return rows_.emplace(index, std::move(value)).first->second;
}
std::pair<int, int> SourcePositions::location(std::uint32_t requested)
{
    const auto offset = std::min<std::size_t>(requested, units_.size());
    const auto after = std::upper_bound(starts_.begin(), starts_.end(), static_cast<int>(offset));
    const auto index = static_cast<std::size_t>(after - starts_.begin() - 1);
    // A selection boundary between CR and LF already belongs to the next line
    // in the original Go prefix scan, although the normal line starts after LF.
    if (offset > 0 && offset < units_.size() && units_[offset - 1] == u'\r' && units_[offset] == u'\n')
    {
        return {static_cast<int>(index + 2), 1};
    }
    const auto start = static_cast<std::size_t>(starts_[index]);
    const auto width = byteWidth_(fromUtf16(units_.substr(start, offset - start)));
    return {static_cast<int>(index + 1), width + 1};
}
int SourcePositions::character(int line, int column)
{
    if (line < 1 || static_cast<std::size_t>(line) > starts_.size())
    {
        return -1;
    }
    const auto &value = row(static_cast<std::size_t>(line - 1));
    const auto found = std::lower_bound(value.columns.begin(), value.columns.end(), column);
    if (found == value.columns.end())
    {
        return -1;
    }
    const int position = value.positions[static_cast<std::size_t>(found - value.columns.begin())];
    if (static_cast<std::size_t>(position) == units_.size())
    {
        return -1;
    }
    return position;
}

std::string normalizeSourceText(std::string text)
{
    if (starts(text, "\xef\xbb\xbf")) { text.erase(0, 3); }
    const auto eof = text.find_last_of('\x1a');
    if (eof != std::string::npos && trim(text.substr(eof + 1)).empty()) { text.erase(eof, 1); }
    replaceAll(text, "\r\n", "\n");
    replaceAll(text, "\r", "\n");
    return text;
}
std::string formatLineSteps(const TrackSteps &values)
{
    std::vector<std::string> parts;
    for (auto steps : values) { if (steps != 0) { parts.push_back(std::to_string(steps)); } }
    if (parts.empty()) { return "0"; }
    return join(parts, "/");
}
std::string compactNoteLength(std::uint64_t steps)
{
    const auto bars = steps / 192;
    const auto remainder = steps % 192;
    if (bars == 0)
    {
        if (remainder == 0) { return "0"; }
        return lengthParts()[remainder];
    }
    std::string text = std::to_string(bars) + "b";
    if (remainder != 0) { text += "+" + lengthParts()[remainder]; }
    return text;
}
std::string noteRemainder(std::uint64_t steps)
{
    std::string text = std::to_string(steps / 192) + u8"小節";
    if (steps % 192 != 0)
    {
        auto part = lengthParts()[steps % 192];
        replaceAll(part, "+", " + ");
        text += " + " + part;
    }
    return text;
}
std::string formatLineNoteLengths(const TrackSteps &values)
{
    std::vector<std::string> parts;
    for (auto steps : values) { if (steps != 0) { parts.push_back(compactNoteLength(steps)); } }
    if (parts.empty()) { return "0"; }
    return join(parts, "/");
}
int mmlTokenEnd(const std::u16string &text, int begin)
{
    if (begin < 0 || static_cast<std::size_t>(begin) >= text.size()) { return begin; }
    int end = begin + 1;
    if (text[begin] == '$' || text[begin] == '#')
    {
        while (static_cast<std::size_t>(end) < text.size())
        {
            const auto c = text[end];
            if (!(c >= 'A' && c <= 'Z') && !(c >= 'a' && c <= 'z') && !(c >= '0' && c <= '9') && c != '-') { break; }
            ++end;
        }
        return end;
    }
    while (static_cast<std::size_t>(end) < text.size())
    {
        int cursor = end;
        while (static_cast<std::size_t>(cursor) < text.size() && (text[cursor] == ' ' || text[cursor] == '\t')) { ++cursor; }
        if (static_cast<std::size_t>(cursor) == text.size()) { break; }
        const auto c = text[cursor];
        if ((c >= '0' && c <= '9') || std::u16string(u"+-=\".%^~&").find(c) != std::u16string::npos || (text[begin] == 'n' && c == ',')) { end = cursor + 1; }
        else { break; }
    }
    return end;
}
bool pianoBlack(int note)
{
    const int pitch = (note + 3) % 12;
    return pitch == 1 || pitch == 3 || pitch == 6 || pitch == 8 || pitch == 10;
}
PianoKeyBounds pianoKeyBounds(int note, int width)
{
    int white = 0;
    for (int n = 0; n < note; ++n) { if (!pianoBlack(n)) { ++white; } }
    if (pianoBlack(note))
    {
        const int center = white * width / 56;
        const int half = std::max(1, (width + 167) / 168);
        return {std::max(0, center - half), center + half, true};
    }
    return {white * width / 56, (white + 1) * width / 56, false};
}
int sliderClickValue(int x, int left, int width, int minimum, int maximum)
{
    if (width <= 0 || maximum <= minimum) { return minimum; }
    const auto relative = std::clamp<std::int64_t>(static_cast<std::int64_t>(x) - left, 0, width);
    return static_cast<int>(minimum + (relative * (static_cast<std::int64_t>(maximum) - minimum) + width / 2) / width);
}
int firstEnabledTrack(std::uint32_t mask)
{
    for (int track = 0; track < 16; ++track) { if ((mask & (1u << track)) == 0) { return track; } }
    return -1;
}
int editorFollowTrack(int mode, std::uint32_t mask)
{
    if (mode == 1) { return firstEnabledTrack(mask); }
    if (mode >= 2 && mode < 18) { return mode - 2; }
    return -1;
}
int restoredEditorFollow(std::optional<int> mode, bool previous)
{
    if (mode) { if (*mode >= 0 && *mode < 18) { return *mode; } return 0; }
    if (previous) { return 1; }
    return 0;
}
bool isPdxEditorClick(int id, std::uint16_t code, std::uintptr_t source, std::uintptr_t button)
{
    return id == 4104 && code == 0 && button != 0 && source == button;
}
void EditorCheckGate::change(std::int64_t now) { ++generation_; editedAt_ = now; queued_ = false; }
void EditorCheckGate::cancel() { ++generation_; editedAt_.reset(); queued_ = true; }
EditorCheckGate::State EditorCheckGate::state() const
{
    return {generation_, static_cast<std::uint64_t>(editedAt_.value_or(0)), editedAt_.has_value(), queued_};
}
void EditorCheckGate::restore(const State &state)
{
    generation_ = state[0];
    editedAt_.reset();
    if (state[2]) { editedAt_ = static_cast<std::int64_t>(state[1]); }
    queued_ = state[3] != 0;
}
std::optional<EditorCheckTicket> EditorCheckGate::take(std::int64_t now, std::uint64_t revision, std::uint64_t delay)
{
    if (queued_ || !editedAt_ || now < *editedAt_ ||
        static_cast<std::uint64_t>(now) - static_cast<std::uint64_t>(*editedAt_) < delay) { return std::nullopt; }
    queued_ = true;
    return EditorCheckTicket{generation_, revision};
}
bool EditorCheckGate::accept(EditorCheckTicket ticket, std::uint64_t revision) const
{
    return queued_ && ticket.generation == generation_ && ticket.revision == revision;
}
std::map<int, TrackSteps> accumulateLineSteps(const std::vector<SourceEvent> &events)
{
    std::map<int, TrackSteps> rows;
    for (const auto &event : events)
    {
        if (event.track < 0 || event.track >= 16 || event.line < 1) { continue; }
        auto &value = rows[event.line][event.track];
        const auto limit = std::numeric_limits<std::uint64_t>::max();
        if (limit - value < event.steps) { value = limit; }
        else { value += event.steps; }
    }
    return rows;
}
std::vector<EditorErrorSpan> buildErrorSpans(const std::string &text,
    const std::vector<EditorDiagnostic> &diagnostics, const std::function<int(const std::string &)> &byteWidth)
{
    const auto units = toUtf16(text);
    std::vector<int> starts{0};
    for (int index = 0; static_cast<std::size_t>(index) < units.size(); ++index)
    {
        if (units[index] == '\r' || units[index] == '\n')
        {
            if (units[index] == '\r' && static_cast<std::size_t>(index + 1) < units.size() && units[index + 1] == '\n') { ++index; }
            starts.push_back(index + 1);
        }
    }
    std::set<EditorErrorSpan> seen;
    std::vector<EditorErrorSpan> spans;
    for (const auto &diagnostic : diagnostics)
    {
        if (!diagnostic.error || diagnostic.line < 1 || static_cast<std::size_t>(diagnostic.line) > starts.size() || diagnostic.column < 1) { continue; }
        const int start = starts[diagnostic.line - 1];
        int end = start;
        while (static_cast<std::size_t>(end) < units.size() && units[end] != '\r' && units[end] != '\n') { ++end; }
        int position = start;
        int column = 1;
        while (position < end)
        {
            int next = position + 1;
            if (high(units[position]) && next < end && low(units[next])) { ++next; }
            const int width = std::max(1, byteWidth(fromUtf16(units.substr(position, next - position))));
            if (column + width > diagnostic.column) { break; }
            column += width;
            position = next;
        }
        if (position == end)
        {
            if (end == start) { continue; }
            --position;
            if (low(units[position]) && position > start) { --position; }
        }
        int tokenEnd = mmlTokenEnd(units, position);
        if (tokenEnd == position + 1 && high(units[position]) && tokenEnd < end && low(units[tokenEnd])) { ++tokenEnd; }
        tokenEnd = std::min(tokenEnd, end);
        const EditorErrorSpan span{position, tokenEnd};
        if (span.second > span.first && seen.insert(span).second) { spans.push_back(span); }
    }
    return spans;
}
std::string formatGutterRow(int line, const TrackSteps &steps)
{
    return std::to_string(line) + "  " + formatLineSteps(steps);
}
std::string buildGutterText(const std::string &text, const std::map<int, TrackSteps> &steps)
{
    std::vector<std::string> rows;
    const auto count = std::count(text.begin(), text.end(), '\n') + 1;
    for (int line = 1; line <= count; ++line)
    {
        const auto found = steps.find(line);
        if (found == steps.end()) { rows.push_back(formatGutterRow(line, {})); }
        else { rows.push_back(formatGutterRow(line, found->second)); }
    }
    return join(rows, "\r\n");
}
std::pair<std::string, bool> pcmFileName(const std::string &text)
{
    auto normalized = text;
    replaceAll(normalized, "\r\n", "\n");
    std::string name;
    bool declared = false;
    bool listed = true;
    int braces = 0;
    for (const auto &line : splitLines(normalized))
    {
        if (braces == 0 && starts(line, "#"))
        {
            const auto at = line.find_first_of(" \t\r");
            const auto key = lower(line.substr(1, at - 1));
            std::string value;
            if (at != std::string::npos) { value = trim(line.substr(at + 1)); }
            if (key == "list") { listed = true; }
            else if (key == "nlist") { listed = false; }
            else if (key == "pcmfile" && listed)
            {
                declared = true;
                name.clear();
                char quote = 0;
                for (char c : value)
                {
                    if (quote != 0)
                    {
                        if (c == quote) { quote = 0; }
                        else { name += c; }
                        continue;
                    }
                    if (c == '\'' || c == '"') { quote = c; continue; }
                    if (c == ' ' || c == '\t' || c == '\r' || c == ';' || c == '*') { break; }
                    name += c;
                }
            }
        }
        adjustBraces(line, braces);
    }
    return {name, declared};
}
std::pair<std::string, bool> pdxReferenceName(const std::string &text)
{
    const auto active = pcmFileName(text);
    if (active.second) { return active; }
    auto normalized = text;
    replaceAll(normalized, "\r\n", "\n");
    std::pair<std::string, bool> result;
    int braces = 0;
    for (const auto &line : splitLines(normalized))
    {
        auto trimmed = trimLeftAscii(line);
        bool commented = trimmed != line;
        while (starts(trimmed, "//")) { commented = true; trimmed = trimLeftAscii(trimmed.substr(2)); }
        if (braces == 0 && commented)
        {
            const auto value = pcmFileName(trimmed);
            if (value.second) { result = value; }
        }
        adjustBraces(line, braces);
    }
    return result;
}
std::string pdxProjectName(const std::string &text, const std::string &fallback)
{
    auto name = pdxReferenceName(text).first;
    if (name.empty()) { name = fallback; }
    const auto slash = name.find_last_of('/');
    const auto dot = name.find_last_of('.');
    if (dot != std::string::npos && (slash == std::string::npos || dot > slash))
    {
        const auto ext = lower(name.substr(dot));
        if (ext == ".pdx" || ext == ".pdxedit" || ext == ".pdxed") { name.resize(dot); }
    }
    return name + ".pdxedit";
}
std::pair<std::string, bool> ensurePcmFileDirective(const std::string &text, const std::string &name)
{
    if (pdxReferenceName(text).second) { return {text, false}; }
    std::string newline = "\n";
    if (text.find("\r\n") != std::string::npos) { newline = "\r\n"; }
    auto normalized = text;
    replaceAll(normalized, "\r\n", "\n");
    auto lines = splitLines(normalized);
    auto cleanName = name;
    replaceAll(cleanName, "\"", "");
    std::size_t at = 0;
    for (std::size_t i = 0; i < lines.size(); ++i)
    {
        if (starts(lines[i], "#") && lower(firstField(lines[i])) == "#title") { at = i + 1; break; }
    }
    lines.insert(lines.begin() + at, "#pcmfile \"" + cleanName + ".pdx\"");
    return {join(lines, newline), true};
}
std::string ensureOverwriteDirective(const std::string &text)
{
    const auto lines = splitLines(text);
    bool overwrite = false;
    bool listed = true;
    bool block = false;
    int title = -1;
    int firstSignificant = -1;
    for (int i = 0; static_cast<std::size_t>(i) < lines.size(); ++i)
    {
        const auto &line = lines[i];
        std::string visible;
        for (std::size_t j = 0; j < line.size(); ++j)
        {
            if (block)
            {
                if (line.compare(j, 2, "*/") == 0) { block = false; ++j; }
                continue;
            }
            if (line.compare(j, 2, "/*") == 0) { block = true; ++j; continue; }
            if (line[j] == ';' || line.compare(j, 2, "//") == 0) { break; }
            visible += line[j];
        }
        const auto field = lower(firstField(visible));
        if (field.empty()) { continue; }
        if (firstSignificant < 0) { firstSignificant = i; }
        if (line.empty() || line[0] == ' ' || line[0] == '\t') { continue; }
        if (field == "#nlist") { listed = false; continue; }
        if (field == "#list") { listed = true; continue; }
        if (!listed) { continue; }
        if (field == "#overwrite") { overwrite = true; }
        if (field == "#title" && i == firstSignificant) { title = i; }
    }
    if (overwrite) { return text; }
    if (title < 0) { return "#overwrite\n" + text; }
    std::size_t at = 0;
    for (int i = 0; i <= title; ++i)
    {
        at += lines[i].size();
        if (static_cast<std::size_t>(i) < lines.size() - 1) { ++at; }
    }
    std::string prefix;
    if (static_cast<std::size_t>(title) == lines.size() - 1 && (text.empty() || text.back() != '\n')) { prefix = "\n"; }
    return text.substr(0, at) + prefix + "#overwrite\n" + text.substr(at);
}
std::string appendVoiceDefinition(const std::string &text, const std::string &voice, int anchorEnd)
{
    const auto normalized = normalizeSourceText(text);
    const auto cleanVoice = trim(normalizeSourceText(voice));
    const auto lines = splitLines(normalized);
    if (anchorEnd < 0 || static_cast<std::size_t>(anchorEnd) > lines.size()) { throw std::invalid_argument("invalid instrument insertion line: " + std::to_string(anchorEnd)); }
    if (anchorEnd == 0) { anchorEnd = static_cast<int>(lines.size()); }
    std::size_t offset = 0;
    for (int i = 0; i < anchorEnd && static_cast<std::size_t>(i) < lines.size(); ++i)
    {
        offset += lines[i].size();
        if (static_cast<std::size_t>(i) < lines.size() - 1) { ++offset; }
    }
    std::string insertion = "\n" + cleanVoice + "\n";
    if (offset == normalized.size() && (normalized.empty() || normalized.back() != '\n')) { insertion = "\n" + insertion; }
    return ensureOverwriteDirective(normalized.substr(0, offset) + insertion + normalized.substr(offset));
}
std::string voiceWithOffsetScope(const std::string &voice, int offset)
{
    if (offset == 0) { return voice; }
    return "#toneofs 0\n" + trim(normalizeSourceText(voice)) + "\n#toneofs " + std::to_string(offset);
}
std::vector<std::uint8_t> tagVgm(const std::vector<std::uint8_t> &data, const std::string &title)
{
    if (data.size() < 0x40 || !std::equal(data.begin(), data.begin() + 4, "Vgm ")) { throw std::invalid_argument("Invalid VGM header"); }
    auto out = data;
    std::vector<std::uint8_t> payload;
    for (const auto &field : std::array<std::string, 11>{title, title, "", "", "", "", "", "", "", "NOTE MDX Workbench", "Exported from NOTE MDX Workbench."})
    {
        const auto units = toUtf16(field);
        for (const auto unit : units) { payload.push_back(static_cast<std::uint8_t>(unit & 0xff)); payload.push_back(static_cast<std::uint8_t>(unit >> 8)); }
        payload.push_back(0); payload.push_back(0);
    }
    const auto offset = out.size();
    if (offset > UINT32_MAX || payload.size() > UINT32_MAX - 12 || offset > UINT32_MAX - payload.size() - 12) { throw std::length_error("VGM metadata exceeds 32-bit length"); }
    auto put32 = [&](std::size_t at, std::uint32_t value)
    {
        for (int i = 0; i < 4; ++i) { out[at + i] = static_cast<std::uint8_t>(value >> (i * 8)); }
    };
    out.insert(out.end(), {'G', 'd', '3', ' '});
    out.resize(out.size() + 8);
    put32(offset + 4, 0x100);
    put32(offset + 8, static_cast<std::uint32_t>(payload.size()));
    out.insert(out.end(), payload.begin(), payload.end());
    put32(0x14, static_cast<std::uint32_t>(offset - 0x14));
    put32(4, static_cast<std::uint32_t>(out.size() - 4));
    return out;
}
}
