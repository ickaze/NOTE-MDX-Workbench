#pragma once
// NoteMDX revision 5 console report format, shared with the Workbench host.
#include "compiler.hpp"
#include <algorithm>
#include <iomanip>
#include <sstream>
namespace notemdx
{
inline std::string format_diagnostic(const Diagnostic &diagnostic)
{
    std::ostringstream out;
    out << diagnostic.file << '(' << diagnostic.line << ',' << diagnostic.column << "): ";
    if (diagnostic.severity == Severity::error)
    {
        out << "error";
    }
    else
    {
        out << "warning";
    }
    if (diagnostic.channel)
    {
        out << " [" << diagnostic.channel << ']';
    }
    out << ": " << diagnostic.message;
    return out.str();
}
inline std::string format_statistics(const Statistics &stats, const std::string &utf8Title)
{
    std::ostringstream out;
    auto               list = [](const std::vector<int> &values, bool ranges)
    {
        if (values.empty())
        {
            return std::string("none");
        }
        std::string text;
        for (size_t i = 0; i < values.size(); ++i)
        {
            if (!text.empty())
            {
                text += ", ";
            }
            text += std::to_string(values[i]);
            size_t end = i;
            while (ranges && (end + 1 < values.size()) && (values[end + 1] == values[end] + 1))
            {
                ++end;
            }
            if (end > i)
            {
                text += "-" + std::to_string(values[end]);
            }
            i = end;
        }
        return text;
    };
    auto steps = [](const StepCount &count)
    {
        if (count.overflow)
        {
            return std::string("overflow");
        }
        return std::to_string(count.value);
    };
    auto row = [&](const std::string &label, const std::string &value)
    { out << std::left << std::setw(11) << label << " : " << value << '\n'; };
    std::string title = utf8Title;
    if (title.empty())
    {
        title = "(untitled)";
    }
    row("Title", title);
    row("FM voices", list(stats.fm_used, false));
    for (const auto &bank : stats.pcm_banks)
    {
        row("PCM bank" + std::to_string(bank.bank), list(bank.used, true));
    }
    size_t width = 6;
    for (const auto &track : stats.tracks)
    {
        width = std::max(width, steps(track.total_steps).size());
        if (track.loop_steps)
        {
            width = std::max(width, steps(*track.loop_steps).size());
        }
    }
    for (int first : {0, 8})
    {
        std::string label = "OPM track";
        if (first != 0)
        {
            label = "PCM track";
        }
        out << std::left << std::setw(11) << label << " :";
        for (int i = first; i < first + 8; ++i)
        {
            out << ' ' << std::right << std::setw(int(width)) << "ABCDEFGHPQRSTUVW"[i];
        }
        out << '\n';
        for (bool loop : {false, true})
        {
            std::string label = "Total steps";
            if (loop)
            {
                label = "Loop steps";
            }
            out << std::left << std::setw(11) << label << " :";
            for (int i = first; i < first + 8; ++i)
            {
                const auto &track = stats.tracks[i];
                std::string value = steps(track.total_steps);
                if (loop)
                {
                    value = "-";
                    if (track.loop_steps)
                    {
                        value = steps(*track.loop_steps);
                    }
                }
                out << ' ' << std::right << std::setw(int(width)) << value;
            }
            out << '\n';
        }
    }
    return out.str();
}
} // namespace notemdx
