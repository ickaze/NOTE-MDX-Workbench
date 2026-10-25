// SPDX-License-Identifier: 0BSD
// Copyright (C) 2026 by I.C.KaZe
#define WB_CORE_IMPLEMENTATION
#include "core_api.h"
#include "pdx_editor_embed.h"
#include "../pdxeditor/src/App.h"
#include <condition_variable>
#include <thread>
#include <objbase.h>

namespace
{
std::mutex              editorMutex;
std::condition_variable editorChanged;
std::thread             editorThread;
HWND                    editorWindow = nullptr;
bool                    editorActive = false;
bool                    editorReady = false;
}

extern "C" __declspec(dllexport) int WB_OpenPdx(HWND, const wchar_t *project, const wchar_t *language)
{
    std::unique_lock<std::mutex> lock(editorMutex);
    if (editorActive)
    {
        return 0;
    }
    if (editorThread.joinable())
    {
        editorThread.join();
    }
    std::wstring projectPath;
    if (project)
    {
        projectPath = project;
    }
    std::wstring languageCode = L"ja";
    if (language)
    {
        languageCode = language;
    }
    editorActive = true;
    editorReady = false;
    try
    {
        editorThread = std::thread([projectPath, languageCode]
        {
            const HRESULT com = OleInitialize(nullptr);
            try
            {
                App editor;
                // Avoid cross-thread owner notifications while the host waits or joins.
                editor.Run(GetModuleHandleW(nullptr), projectPath, languageCode, nullptr, [](HWND window)
                {
                    std::lock_guard<std::mutex> guard(editorMutex);
                    editorWindow = window;
                    editorReady = true;
                    editorChanged.notify_all();
                });
            }
            catch (...)
            {
                // Startup failure is reported to the host through editorReady/window.
            }
            if (SUCCEEDED(com))
            {
                OleUninitialize();
            }
            {
                std::lock_guard<std::mutex> guard(editorMutex);
                editorWindow = nullptr;
                editorActive = false;
                editorReady = true;
            }
            editorChanged.notify_all();
        });
    }
    catch (...)
    {
        editorActive = false;
        return 0;
    }
    editorChanged.wait(lock, [] { return editorReady; });
    return editorWindow != nullptr;
}

extern "C" __declspec(dllexport) int WB_PdxIsOpen()
{
    std::lock_guard<std::mutex> lock(editorMutex);
    return editorActive;
}

extern "C" __declspec(dllexport) int WB_ClosePdx()
{
    HWND window;
    {
        std::lock_guard<std::mutex> lock(editorMutex);
        window = editorWindow;
    }
    if (window)
    {
        SendMessageW(window, WM_CLOSE, 0, 0);
    }
    {
        std::lock_guard<std::mutex> lock(editorMutex);
        // Failed project save: keep the editor and its host open.
        if (editorWindow)
        {
            return 0;
        }
    }
    if (editorThread.joinable())
    {
        editorThread.join();
    }
    return 1;
}
