// SPDX-License-Identifier: 0BSD
// Copyright (C) 2026 by I.C.KaZe
#include "app.h"
namespace gui
{
bool confirmOverwrite(const std::vector<Path> &paths)
{
    std::wstring existing;
    for (const auto &p : paths)
    {
        if (gui::exists(p))
        {
            existing += L"\n" + fs::absolute(p).wstring();
        }
    }
    if (existing.empty())
    {
        return true;
    }
    return dialog(translate(L"既存のファイルを上書きしますか？") + L"\n" + existing, L"上書き確認",
                  MB_YESNO) == IDYES;
}
void cancelExport()
{
    if (!app.outputBusy)
    {
        return;
    }
    app.core.WB_ExportControl(1);
    bool cancel = dialog(L"出力を中断しますか？", L"出力中断の確認", MB_YESNO) == IDYES;
    app.core.WB_ExportControl(
        [&]()
        {
            if (cancel)
            {
                return 2;
            }
            return 0;
        }());
}
void convertMdx(bool watching)
{
    if (!compileFull())
    {
        stop();
        return;
    }
    Path directory = executableDirectory();
    if (checked(app.modeFile))
    {
        directory = app.sourcePath.parent_path();
    }
    auto name = safeName(text(app.editorName));
    if (checked(app.modeFile))
    {
        name = safeName(app.sourcePath.filename().wstring());
    }
    Path output = directory / (name + L".mdx");
    if (!confirmOverwrite({output}))
    {
        return;
    }
    atomicWrite(output, read(app.fullPath));
    for (const auto &d : app.full["diagnostics"].items)
    {
        logLine(wide(d.text()));
    }
    logLine(output.wstring());
    logLine(wide(app.full["report"].text()));
    // Watch conversion also uses the same full-path completion notification.
    dialog(fs::absolute(output).wstring(), L"出力完了", MB_OK);
    if (watching)
    {
        send(app.editor, EM_SETSEL, 0, 0);
        play();
    }
}
void startExport(int format)
{
    if (app.outputBusy)
    {
        cancelExport();
        return;
    }
    bool wav = format >= 6, tracks = format == 7;
    if (wav && app.snapshot.output == 2)
    {
        return;
    }
    if (!compileFull(true))
    {
        return;
    }
    if (app.outputThread.joinable())
    {
        app.outputThread.join();
    }
    auto name = safeName(text(app.editorName));
    Path directory = executableDirectory();
    if (checked(app.modeFile) && !wav)
    {
        directory = app.sourcePath.parent_path();
        name = safeName(app.sourcePath.filename().wstring());
    }
    if (wav)
    {
        directory /= name;
        fs::create_directories(directory);
    }
    std::vector<int> channels;
    if (tracks)
    {
        for (std::size_t i = 0; i < 16; ++i)
        {
            if (app.full["tracks"][i]["total"].u64())
            {
                channels.push_back(int(i));
            }
        }
    }
    else
    {
        channels.push_back(-1);
    }
    if (channels.empty())
    {
        throw std::runtime_error("No tracks to export");
    }
    std::vector<Path> outputs;
    for (int ch : channels)
    {
        std::wstring suffix = L".mrz";
        if (wav)
        {
            suffix = L"_Mix.wav";
            if (ch >= 0)
            {
                suffix = L"_" + std::wstring(1, names[ch]) + L".wav";
            }
        }
        else if (format == 2)
        {
            suffix = L".vgm";
        }
        else if (format == 3)
        {
            suffix = L".vgz";
        }
        else if (format == 4)
        {
            suffix = L".mrl";
        }
        outputs.push_back(directory / (name + suffix));
    }
    if (!confirmOverwrite(outputs))
    {
        return;
    }
    auto files = std::make_shared<TemporaryFiles>();
    auto frozen = files->make(fs::current_path(), L".mdx");
    write(frozen, read(app.fullPath));
    std::vector<Path> stages;
    for (std::size_t i = 0; i < outputs.size(); ++i)
    {
        stages.push_back(files->make(fs::current_path()));
    }
    auto raw = read(frozen);
    auto titleEnd = raw.find("\r\n\x1a");
    std::wstring title;
    if (titleEnd != std::string::npos)
    {
        title = decode(raw.substr(0, titleEnd));
    }
    Path bank = trim(text(app.pdxEdit)), source = app.sourcePath;
    int loops = std::max(1, app.loops), fade = app.fade, master = app.master, pcm = app.pcm;
    NativeState state = app.snapshot;
    app.core.WB_ExportStart();
    if (wav)
    {
        app.core.WB_WavStart(int(channels.size()));
    }
    app.outputBusy = true;
    app.logOutput = !wav;
    app.progress = 0;
    app.completed.clear();
    app.outputError.clear();
    app.activeButton = app.convertBtn;
    if (wav)
    {
        app.activeButton = app.exportMixBtn;
        if (tracks)
        {
            app.activeButton = app.exportTracksBtn;
        }
    }
    app.outputThread = std::thread(
        [files, frozen, stages, outputs, channels, title, bank, source, format, wav, loops, fade, master, pcm,
         state]
        {
            std::vector<Path> completed;
            std::wstring error;
            try
            {
                if (wav)
                {
                    std::atomic<std::size_t> next{0};
                    std::mutex failureMutex;
                    std::vector<std::string> failures;
                    std::vector<std::thread> workers;
                    auto render = [&]
                    {
                        for (;;)
                        {
                            auto i = next.fetch_add(1);
                            if (i >= channels.size())
                            {
                                return;
                            }
                            try
                            {
                                if (!app.core.WB_ExportCheckpoint())
                                {
                                    return;
                                }
                                app.core.require(app.core.WB_SaveWavFrozen(
                                    frozen.c_str(), bank.c_str(), source.c_str(), stages[i].c_str(),
                                    channels[i] + 1, loops, fade, master, pcm, state.fmPercent,
                                    state.measured, state.dac, state.output, int(i)));
                            }
                            catch (const std::exception &e)
                            {
                                std::lock_guard<std::mutex> lock(failureMutex);
                                failures.push_back(e.what());
                            }
                        }
                    };
                    // Every launched thread is joined even if a later construction fails.
                    try
                    {
                        for (unsigned i = 0;
                             i < std::min<unsigned>(4, std::max(1u, std::thread::hardware_concurrency()));
                             ++i)
                        {
                            workers.emplace_back(render);
                        }
                    }
                    catch (...)
                    {
                        for (auto &worker : workers)
                        {
                            worker.join();
                        }
                        throw;
                    }
                    for (auto &worker : workers)
                    {
                        worker.join();
                    }
                    if (!failures.empty())
                    {
                        throw std::runtime_error(failures.front());
                    }
                    for (std::size_t i = 0; i < stages.size(); ++i)
                    {
                        if (!app.core.WB_ExportCheckpoint())
                        {
                            break;
                        }
                        app.core.require(app.core.WB_PublishOutput(stages[i].c_str(), outputs[i].c_str()));
                        completed.push_back(fs::absolute(outputs[i]));
                    }
                }
                else
                {
                    auto save = app.core.WB_SaveVgm;
                    if (format >= 4)
                    {
                        save = app.core.WB_SaveMrl;
                        // The register-log writer exclusively creates its output.
                        // This path is owned by this job; only remove our empty reservation.
                        gui::remove(stages[0]);
                    }
                    app.core.require(save(frozen.c_str(), bank.c_str(), source.c_str(), stages[0].c_str()));
                    unsigned nativeFormat = 0;
                    if (format == 2)
                    {
                        nativeFormat = 1;
                    }
                    else if (format == 3)
                    {
                        nativeFormat = 2;
                    }
                    else if (format == 5)
                    {
                        nativeFormat = 3;
                    }
                    auto utf8Title = utf8(title);
                    app.core.require(app.core.WB_FinalizeOutput(stages[0].c_str(), outputs[0].c_str(),
                                                                nativeFormat, utf8Title.data(),
                                                                utf8Title.size()));
                    completed.push_back(fs::absolute(outputs[0]));
                }
            }
            catch (const std::exception &e)
            {
                error = wide(e.what());
            }
            files->paths.clear(); // Remove owned files explicitly before notifying the UI.
            for (const auto &p : stages)
            {
                gui::remove(p);
            }
            gui::remove(frozen);
            {
                std::lock_guard<std::mutex> lock(app.resultsMutex);
                app.completed = std::move(completed);
                app.outputError = std::move(error);
            }
            PostMessageW(app.hwnd, outputMessage, 0, 0);
        });
    updateProgress();
}
void finishExport()
{
    if (app.outputThread.joinable())
    {
        app.outputThread.join();
    }
    std::vector<Path> complete;
    std::wstring error;
    {
        std::lock_guard<std::mutex> lock(app.resultsMutex);
        complete = std::move(app.completed);
        error = app.outputError;
    }
    app.outputBusy = false;
    app.activeButton = nullptr;
    if (!error.empty())
    {
        logLine(error);
    }
    updateProgress();
    std::wstring body;
    for (const auto &p : complete)
    {
        if (!body.empty())
        {
            body += L"\n";
        }
        body += p.wstring();
        logLine(p.wstring());
    }
    if (!body.empty() && !app.closing)
    {
        dialog(body, L"出力完了", MB_OK);
    }
}
void updateProgress()
{
    if (app.outputBusy)
    {
        auto progress = app.core.WB_WavProgress;
        if (app.logOutput)
        {
            progress = app.core.WB_VgmProgress;
        }
        app.progress = std::clamp(progress() / 10, 0, 100);
    }
    for (auto h : {app.convertBtn, app.exportMixBtn, app.exportTracksBtn})
    {
        bool enabled = h == app.convertBtn || app.snapshot.output != 2;
        if (app.outputBusy)
        {
            enabled = h == app.activeButton;
        }
        if (IsWindowEnabled(h) != enabled)
        {
            EnableWindow(h, enabled);
        }
        InvalidateRect(h, nullptr, FALSE);
    }
}
} // namespace gui
