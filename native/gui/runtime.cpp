// SPDX-License-Identifier: 0BSD
// Copyright (C) 2026 by I.C.KaZe
#include "app.h"
#include "source_watch.h"
namespace gui
{
void initCore()
{
    app.core.load(executableDirectory());
    app.core.require(app.core.WB_Init(app.hwnd));
    app.ready = true;
    wchar_t temp[32768]{};
    GetTempPathW(32768, temp);
    auto marker = temporary(Path(temp));
    gui::remove(marker);
    fs::create_directory(marker);
    app.directory = marker;
    app.fullPath = marker / L"preview.mdx";
    app.selectionPath = marker / L"selection.mdx";
    for (unsigned kind = 0; kind < 2; ++kind)
    {
        app.core.require(app.core.WB_AnalysisStart(kind));
        app.receivers[kind] = std::thread(
            [kind]
            {
                std::vector<char> buffer(1024 * 1024);
                for (;;)
                {
                    try
                    {
                        auto count = app.core.WB_AnalysisTake(kind, buffer.data(), buffer.size(), 1);
                        if (!count)
                        {
                            return;
                        }
                        if (count < 0)
                        {
                            logLine(wide(app.core.error()));
                            continue;
                        }
                        if (count > 64 * 1024 * 1024)
                        {
                            throw std::runtime_error("Analysis result exceeds buffer limit");
                        }
                        if (count > int64_t(buffer.size()))
                        {
                            buffer.resize(std::size_t(count));
                            continue;
                        }
                        auto result = Json::parse(std::string(buffer.data(), std::size_t(count) - 1));
                        {
                            std::lock_guard<std::mutex> lock(app.resultsMutex);
                            app.results[kind] = std::move(result);
                        }
                        PostMessageW(
                            app.hwnd,
                            [&]()
                            {
                                if (kind)
                                {
                                    return selectionMessage;
                                }
                                return analysisMessage;
                            }(),
                            0, 0);
                    }
                    catch (const std::exception &e)
                    {
                        logError(e);
                    }
                }
            });
    }
    int mask = 0;
    for (int i = 0; i < 16; ++i)
    {
        if (!checked(app.trackChecks[i]))
        {
            mask |= 1 << i;
        }
    }
    app.core.command(8, mask);
    app.core.command(9, app.master);
    app.core.command(10, app.pcm);
    app.core.command(25, app.roll || app.waves);
    app.core.command(26, app.english);
    SetWindowSubclass(app.editor, editorProc, 1, 0);
    SetWindowSubclass(app.lines, gutterProc, 2, 0);
    app.core.require(app.core.WB_State(&app.snapshot, sizeof(app.snapshot)));
    syncControls();
    invalidate();
}
void shutdownCore()
{
    if (!app.ready)
    {
        app.core.unload();
        return;
    }
    app.core.WB_ExportControl(2);
    if (app.outputThread.joinable())
    {
        app.outputThread.join();
    }
    for (unsigned kind = 0; kind < 2; ++kind)
    {
        app.core.WB_AnalysisStop(kind);
        if (app.receivers[kind].joinable())
        {
            app.receivers[kind].join();
        }
    }
    app.core.WB_Shutdown();
    app.ready = false;
    app.core.unload();
    if (!app.directory.empty())
    {
        std::error_code ec;
        fs::remove_all(app.directory, ec);
        app.directory.clear();
    }
}
void invalidate()
{
    ++app.revision;
    app.gate.change(int64_t(GetTickCount64()));
    app.cursorsValid = false;
    app.errors.clear();
    app.lineSteps.clear();
    app.selectionRevision = UINT64_MAX;
    ++app.selectionGeneration;
    app.decorationDirty = true;
    setText(app.selectionLabel, L"");
    updateStatistics();
    paintDecoration();
    InvalidateRect(app.lines, nullptr, FALSE);
}
void queueCheck()
{
    auto ticket = app.gate.take(int64_t(GetTickCount64()), app.revision);
    if (!ticket || !app.ready)
    {
        return;
    }
    Path path;
    auto content = utf8(text(app.editor));
    bool file = checked(app.modeFile);
    if (file)
    {
        path = trim(text(app.srcEdit));
        if (!path.empty())
        {
            path = fs::absolute(path);
        }
    }
    else
    {
        auto reference = trim(text(app.srcEdit));
        auto directory = executableDirectory();
        if (!reference.empty())
        {
            directory = fs::absolute(Path(reference)).parent_path();
        }
        path = directory / (safeName(text(app.editorName)) + L".mus");
    }
    app.core.require(app.core.WB_AnalysisSubmit(0u, ticket->generation, ticket->revision, 0u, 0u,
                                                path.c_str(), content.data(), content.size(), int(file)));
}
void applyDocument(const Path &path, const std::wstring &content, uint64_t revision, const Json &result,
                   const Json &decoration)
{
    app.checkedPath = path;
    app.checkedText = content;
    app.checkedRevision = revision;
    app.checkedDocument = result;
    app.lineSteps.clear();
    for (const auto &row : decoration["steps"].fields)
    {
        workbench::TrackSteps values{};
        for (std::size_t i = 0; i < 16; ++i)
        {
            values[i] = row.second[i].u64();
        }
        app.lineSteps[std::stoi(row.first)] = values;
    }
    app.errors.clear();
    for (const auto &span : decoration["spans"].items)
    {
        app.errors.emplace_back(span["Begin"].integer(), span["End"].integer());
    }
    auto gutter = wide(decoration["gutter"].text());
    setText(app.lines, gutter);
    app.gutterRows.clear();
    std::size_t at = 0;
    for (;;)
    {
        auto end = gutter.find(L"\r\n", at);
        app.gutterRows.push_back(gutter.substr(at,
                                               [&]()
                                               {
                                                   if (end == std::wstring::npos)
                                                   {
                                                       return end;
                                                   }
                                                   return end - at;
                                               }()));
        if (end == std::wstring::npos)
        {
            break;
        }
        at = end + 2;
    }
    app.decorationDirty = true;
    updateStatistics();
    app.selectionRevision = UINT64_MAX;
    refreshSelection();
    paintDecoration();
    InvalidateRect(app.lines, nullptr, FALSE);
}
void receiveAnalysis(unsigned kind)
{
    std::optional<Json> next;
    {
        std::lock_guard<std::mutex> lock(app.resultsMutex);
        next = std::move(app.results[kind]);
        app.results[kind].reset();
    }
    if (!next || !app.ready)
    {
        return;
    }
    const auto &response = *next;
    if (!kind)
    {
        if (!app.gate.accept({response["generation"].u64(), response["revision"].u64()}, app.revision))
        {
            return;
        }
        if (!response["error"].text().empty())
        {
            logLine(wide(response["error"].text()));
            return;
        }
        auto content = wide(response["text"].text());
        if (response["readFile"].boolean() && text(app.editor) != content)
        {
            app.restoring = true;
            setText(app.editor, content);
            app.restoring = false;
        }
        applyDocument(wide(response["path"].text()), content, app.revision, response["result"], response);
        std::vector<uint8_t> entries;
        for (const auto &v : response["result"]["voices"].items)
        {
            if (v["id"].integer(-1) >= 0 && v["id"].integer() < 256 && v["bytes"].items.size() == 27)
            {
                entries.push_back(uint8_t(v["id"].integer()));
                for (const auto &b : v["bytes"].items)
                {
                    entries.push_back(uint8_t(b.integer()));
                }
            }
        }
        app.core.WB_ApplyVoiceCatalog(entries.data(), entries.size() / 28);
        if (!response["result"]["ok"].boolean())
        {
            for (const auto &d : response["result"]["diagnostics"].items)
            {
                logLine(wide(d.text()));
            }
        }
    }
    else
    {
        DWORD begin = 0, end = 0;
        send(app.editor, EM_GETSEL, WPARAM(&begin), LPARAM(&end));
        if (response["generation"].u64() != app.selectionGeneration ||
            response["revision"].u64() != app.revision || app.checkedRevision != app.revision ||
            begin != response["begin"].u64() || end != response["end"].u64())
        {
            return;
        }
        app.selection = response["result"];
        if (!response["error"].text().empty() || !app.selection["ok"].boolean())
        {
            setText(app.selectionLabel, translate(L"選択範囲: 解析エラー"));
        }
        else
        {
            updateStatistics();
        }
    }
}
bool compileFull(bool showLog)
{
    if (!app.ready)
    {
        return false;
    }
    app.gate.cancel();
    auto document = source();
    if (app.compiledText == document.second && app.sourcePath == document.first &&
        app.compiledRevision == app.revision && app.full["ok"].boolean())
    {
        return true;
    }
    if (app.sourceText != document.second || app.sourcePath != document.first)
    {
        ++app.revision;
        app.cursorsValid = false;
    }
    if (checked(app.modeFile) && text(app.editor) != document.second)
    {
        app.restoring = true;
        setText(app.editor, document.second);
        app.restoring = false;
    }
    auto result = app.core.compile(document.first, document.second, app.fullPath);
    if (!result.has("decoration"))
    {
        throw std::runtime_error("Missing native editor decoration");
    }
    app.full = result;
    app.sourcePath = document.first;
    app.sourceText = document.second;
    app.compiledText = document.second;
    app.compiledRevision = app.revision;
    applyDocument(document.first, document.second, app.revision, result, result["decoration"]);
    if (showLog || !result["ok"].boolean())
    {
        for (const auto &d : result["diagnostics"].items)
        {
            logLine(wide(d.text()));
        }
    }
    app.selectionRevision = UINT64_MAX;
    return result["ok"].boolean();
}
void updateStatistics()
{
    for (std::size_t i = 0; i < 16; ++i)
    {
        std::wstring label = std::wstring(1, names[i]) + L": —";
        if (app.checkedRevision == app.revision && app.checkedDocument["ok"].boolean())
        {
            const auto &t = app.checkedDocument["tracks"][i];
            label=std::wstring(1,names[i])+L" "+std::to_wstring(t["total"].u64())+L"/"+([&]() -> std::wstring { if (t["loop"].kind==Json::Null) { return L"—"; } return std::to_wstring(t["loop"].u64()); }());
            if (t["overflow"].boolean())
            {
                label += L"+";
            }
        }
        setText(app.stepLabels[i], label);
    }
    if (app.selectionBegin != app.selectionEnd && app.selection["ok"].boolean())
    {
        std::wstring label = L"範囲: ";
        uint64_t longest = 0;
        bool first = true;
        for (std::size_t i = 0; i < 16; ++i)
        {
            auto n = app.selection["tracks"][i]["total"].u64();
            if (!n)
            {
                continue;
            }
            if (!first)
            {
                label += L" / ";
            }
            first = false;
            label += std::wstring(1, names[i]) + L":" + std::to_wstring(n);
            longest = std::max(longest, n);
        }
        label +=
            L"\r\n最長: " + std::to_wstring(longest) + L" (" + wide(workbench::noteRemainder(longest)) + L")";
        setText(app.selectionLabel, translate(label));
    }
}
void refreshSelection(bool force)
{
    if (!app.ready)
    {
        return;
    }
    DWORD begin = 0, end = 0;
    send(app.editor, EM_GETSEL, WPARAM(&begin), LPARAM(&end));
    if (!force && begin == app.selectionBegin && end == app.selectionEnd &&
        app.selectionRevision == app.checkedRevision)
    {
        return;
    }
    ++app.selectionGeneration;
    app.selectionBegin = begin;
    app.selectionEnd = end;
    app.selectionRevision = app.checkedRevision;
    app.selection = Json{};
    if (begin == end)
    {
        setText(app.selectionLabel, L"");
        return;
    }
    if (!app.checkedDocument["ok"].boolean() || app.checkedRevision != app.revision)
    {
        setText(app.selectionLabel, translate(L"解析待ち"));
        return;
    }
    if (!force)
    {
        auto content = utf8(app.checkedText);
        app.core.require(app.core.WB_AnalysisSubmit(1u, app.selectionGeneration, app.checkedRevision, begin,
                                                    end, app.checkedPath.c_str(), content.data(),
                                                    content.size(), 0));
        setText(app.selectionLabel, translate(L"解析待ち"));
        return;
    }
    workbench::SourcePositions positions(utf8(app.sourceText),
                                         [](const std::string &v)
                                         {
                                             auto w = wide(v);
                                             return WideCharToMultiByte(932, 0, w.data(), int(w.size()),
                                                                        nullptr, 0, nullptr, nullptr);
                                         });
    auto a = positions.location(begin), b = positions.location(end);
    app.selection = app.core.compile(app.sourcePath, app.sourceText, app.selectionPath, a.first, a.second,
                                     b.first, b.second);
    if (!app.selection["ok"].boolean())
    {
        setText(app.selectionLabel, translate(L"選択範囲: 解析エラー"));
    }
    else
    {
        updateStatistics();
    }
}
void play()
{
    if (!compileFull())
    {
        stop();
        return;
    }
    refreshSelection(true);
    bool selected = app.selectionBegin != app.selectionEnd;
    const auto &result = [&]() -> const Json &
    {
        if (selected)
        {
            return app.selection;
        }
        return app.full;
    }();
    if (!result["ok"].boolean())
    {
        throw std::runtime_error("Cannot compile the selected MML");
    }
    uint64_t total = 0;
    for (const auto &t : result["tracks"].items)
    {
        total |= t["total"].u64();
    }
    if (!total)
    {
        throw std::runtime_error("No notes or rests to play");
    }
    app.cursors.clear();
    for (const auto &event : result["events"].items)
    {
        app.cursors[event["offset"].u64()] = event;
    }
    app.playingText = text(app.editor);
    app.cursorsValid = app.playingText == app.sourceText;
    app.history.clear();
    app.score = Json{};
    app.wall = 0;
    app.scoreRevision = UINT64_MAX;
    app.core.command(
        1, int(selected),
        [&]() -> const Path &
        {
            if (selected)
            {
                return app.selectionPath;
            }
            return app.fullPath;
        }(),
        trim(text(app.pdxEdit)), app.sourcePath);
}
void stop()
{
    if (!app.ready)
    {
        return;
    }
    app.core.command(3);
    app.cursorsValid = false;
    app.decorationDirty = true;
    paintDecoration();
}
void tick()
{
    if (!app.ready)
    {
        return;
    }
    app.core.require(app.core.WB_State(&app.snapshot, sizeof(app.snapshot)));
    pollVoice();
    char status[8192]{};
    int n = app.core.WB_Status(status, sizeof(status));
    static std::string previous;
    if (n > 1 && n <= int(sizeof(status)) && previous != status)
    {
        previous = status;
        logLine(wide(previous));
    }
    syncControls();
    queueCheck();
    refreshSelection();
    EnableWindow(app.pdxButton, !app.core.WB_PdxIsOpen());
    updateProgress();
    captureHistory();
    paintDecoration();
    if (app.scoreRevision != app.snapshot.scoreRevision)
    {
        std::vector<char> data(1024 * 1024);
        int size = app.core.WB_Score(data.data(), data.size());
        if (size > int(data.size()) && size <= 64 * 1024 * 1024)
        {
            data.resize(size);
            size = app.core.WB_Score(data.data(), data.size());
        }
        if (size > 0 && size <= int(data.size()))
        {
            app.score = Json::parse(std::string(data.data(), size - 1));
        }
        else
        {
            app.score = Json{};
        }
        app.scoreRevision = app.snapshot.scoreRevision;
    }
    auto total = app.snapshot.total, pos = app.snapshot.position;
    if (!app.seeking)
    {
        LPARAM position = 0;
        if (total)
        {
            position = LPARAM(pos * 1000 / total);
        }
        send(app.seek, TBM_SETPOS, TRUE, position);
    }
    else
    {
        pos = total * uint64_t(send(app.seek, TBM_GETPOS)) / 1000;
    }
    auto seconds = pos / 48000;
    wchar_t time[64]{};
    swprintf(time, 64, L"%02llu:%02llu:%02llu", static_cast<unsigned long long>(seconds / 3600),
             static_cast<unsigned long long>((seconds / 60) % 60),
             static_cast<unsigned long long>(seconds % 60));
    setText(app.timeLabel, time);
    syncGutter();
    InvalidateRect(app.mmdspPanel, nullptr, FALSE);
}
} // namespace gui
