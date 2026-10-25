// SPDX-License-Identifier: 0BSD
// Copyright (C) 2026 by I.C.KaZe
// Text protocol is test-only: tab-separated hex arguments, one hex result/line.
#include "text_logic.h"
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace
{
std::vector<std::string> split(const std::string &text, char delimiter)
{
    std::vector<std::string> fields;
    std::size_t start = 0;
    for (;;)
    {
        const auto at = text.find(delimiter, start);
        fields.push_back(text.substr(start, at - start));
        if (at == std::string::npos) { return fields; }
        start = at + 1;
    }
}
std::string unhex(const std::string &text)
{
    if (text.size() % 2) { throw std::invalid_argument("Odd hex argument"); }
    std::string bytes;
    for (std::size_t at = 0; at < text.size(); at += 2)
    {
        const auto part = text.substr(at, 2);
        std::size_t consumed = 0;
        const auto value = std::stoul(part, &consumed, 16);
        if (consumed != 2) { throw std::invalid_argument("Invalid hex"); }
        bytes += static_cast<char>(value);
    }
    return bytes;
}
std::string hex(const std::string &text)
{
    constexpr char digits[] = "0123456789abcdef";
    std::string result;
    for (unsigned char value : text)
    {
        result += digits[value >> 4];
        result += digits[value & 15];
    }
    return result;
}
std::string boolean(bool value) { if (value) { return "1"; } return "0"; }
std::string paired(const std::pair<std::string, bool> &value)
{
    return boolean(value.second) + ":" + value.first;
}
workbench::TrackSteps steps(const std::string &text)
{
    const auto fields = split(text, ',');
    if (fields.size() != 16) { throw std::invalid_argument("Expected 16 tracks"); }
    workbench::TrackSteps result{};
    for (int i = 0; i < 16; ++i) { result[i] = std::stoull(fields[i]); }
    return result;
}
std::vector<workbench::SourceEvent> events(const std::string &text)
{
    std::vector<workbench::SourceEvent> result;
    if (!text.empty())
    {
        for (const auto &row : split(text, ';'))
        {
            const auto fields = split(row, ',');
            result.push_back({std::stoi(fields.at(0)), std::stoi(fields.at(1)), std::stoull(fields.at(2))});
        }
    }
    return result;
}
int positionByteWidth(const std::string &text)
{
    int width = 0;
    const auto units = workbench::toUtf16(text);
    for (std::size_t index = 0; index < units.size(); ++index)
    {
        width += units[index] < 128 ? 1 : 2;
        if (units[index] >= 0xd800 && units[index] <= 0xdbff && index + 1 < units.size())
        {
            ++index;
        }
    }
    return width;
}
std::string evaluate(const std::string &command, const std::vector<std::string> &a)
{
    using namespace workbench;
    auto integer = [&](std::size_t i) { return std::stoi(a.at(i)); };
    auto number = [&](std::size_t i) { return std::stoull(a.at(i)); };
    if (command == "characterbatch")
    {
        SourcePositions positions(a.at(0), positionByteWidth);
        std::string output;
        for (const auto &entry : split(a.at(1), ';'))
        {
            const auto fields = split(entry, ',');
            output += std::to_string(positions.character(std::stoi(fields.at(0)), std::stoi(fields.at(1)))) + ";";
        }
        return output;
    }
    if (command == "location")
    {
        SourcePositions positions(a.at(0), positionByteWidth);
        const auto value = positions.location(static_cast<std::uint32_t>(number(1)));
        return std::to_string(value.first) + "," + std::to_string(value.second);
    }
    if (command == "character")
    {
        SourcePositions positions(a.at(0), positionByteWidth);
        return std::to_string(positions.character(integer(1), integer(2)));
    }
    if (command == "normalize") { return normalizeSourceText(a.at(0)); }
    if (command == "steps") { return formatLineSteps(steps(a.at(0))); }
    if (command == "notes") { return formatLineNoteLengths(steps(a.at(0))); }
    if (command == "compact") { return compactNoteLength(number(0)); }
    if (command == "remainder") { return noteRemainder(number(0)); }
    if (command == "token") { return std::to_string(mmlTokenEnd(toUtf16(a.at(0)), integer(1))); }
    if (command == "key")
    {
        const auto result = pianoKeyBounds(integer(0), integer(1));
        return std::to_string(result.left) + "," + std::to_string(result.right) + "," + boolean(result.black);
    }
    if (command == "slider") { return std::to_string(sliderClickValue(integer(0), integer(1), integer(2), integer(3), integer(4))); }
    if (command == "first") { return std::to_string(firstEnabledTrack(static_cast<std::uint32_t>(number(0)))); }
    if (command == "follow") { return std::to_string(editorFollowTrack(integer(0), static_cast<std::uint32_t>(number(1)))); }
    if (command == "restored")
    {
        std::optional<int> mode;
        if (a.at(0) != "nil") { mode = integer(0); }
        return std::to_string(restoredEditorFollow(mode, integer(1) != 0));
    }
    if (command == "click") { return boolean(isPdxEditorClick(integer(0), static_cast<std::uint16_t>(number(1)), number(2), number(3))); }
    if (command == "pcm") { return paired(pcmFileName(a.at(0))); }
    if (command == "reference") { return paired(pdxReferenceName(a.at(0))); }
    if (command == "project") { return pdxProjectName(a.at(0), a.at(1)); }
    if (command == "ensurepcm") { return paired(ensurePcmFileDirective(a.at(0), a.at(1))); }
    if (command == "overwrite") { return ensureOverwriteDirective(a.at(0)); }
    if (command == "append") { return appendVoiceDefinition(a.at(0), a.at(1), integer(2)); }
    if (command == "scope") { return voiceWithOffsetScope(a.at(0), integer(1)); }
    if (command == "tag")
    {
        const std::vector<std::uint8_t> input(a.at(0).begin(), a.at(0).end());
        const auto result = tagVgm(input, a.at(1));
        return {result.begin(), result.end()};
    }
    if (command == "gutter") { return buildGutterText(a.at(0), accumulateLineSteps(events(a.at(1)))); }
    if (command == "accumulate")
    {
        std::string result;
        for (const auto &row : accumulateLineSteps(events(a.at(0))))
        {
            result += std::to_string(row.first) + ":";
            for (auto value : row.second) { result += std::to_string(value) + ","; }
            result += ";";
        }
        return result;
    }
    if (command == "spans")
    {
        std::vector<EditorDiagnostic> diagnostics;
        if (!a.at(1).empty())
        {
            for (const auto &row : split(a.at(1), ';'))
            {
                const auto fields = split(row, ',');
                diagnostics.push_back({fields.at(0) == "1", std::stoi(fields.at(1)), std::stoi(fields.at(2))});
            }
        }
        const auto spans = buildErrorSpans(a.at(0), diagnostics, [](const std::string &s)
        {
            for (unsigned char c : s) { if (c > 127) { return 2; } }
            return 1;
        });
        std::string result;
        for (const auto &span : spans) { result += std::to_string(span.first) + "," + std::to_string(span.second) + ";"; }
        return result;
    }
    if (command == "gate")
    {
        EditorCheckGate gate;
        std::vector<EditorCheckTicket> tickets;
        std::string result;
        for (const auto &action : split(a.at(0), ';'))
        {
            const auto fields = split(action, ',');
            const auto &kind = fields.at(0);
            if (kind == "change") { gate.change(std::stoll(fields.at(1))); result += "C;"; }
            else if (kind == "cancel") { gate.cancel(); result += "X;"; }
            else if (kind == "take")
            {
                const auto ticket = gate.take(std::stoll(fields.at(1)), std::stoull(fields.at(2)));
                if (ticket)
                {
                    tickets.push_back(*ticket);
                    result += "T:" + std::to_string(ticket->generation) + "," + std::to_string(ticket->revision) + ";";
                }
                else { result += "N;"; }
            }
            else if (kind == "accept") { result += "A:" + boolean(gate.accept(tickets.at(std::stoul(fields.at(1))), std::stoull(fields.at(2)))) + ";"; }
            else { throw std::invalid_argument("Unknown gate action"); }
        }
        return result;
    }
    throw std::invalid_argument("Unknown compatibility command");
}
}

int main()
{
    std::string line;
    while (std::getline(std::cin, line))
    {
        const auto fields = split(line, '\t');
        std::vector<std::string> args;
        try
        {
            for (std::size_t i = 1; i < fields.size(); ++i) { args.push_back(unhex(fields[i])); }
            std::cout << hex(evaluate(fields.at(0), args)) << '\n';
        }
        catch (const std::exception &error)
        {
            std::cout << hex(std::string("ERROR:") + error.what()) << '\n';
        }
    }
    if (std::cin.bad() || !std::cout) { return 1; }
    return 0;
}
