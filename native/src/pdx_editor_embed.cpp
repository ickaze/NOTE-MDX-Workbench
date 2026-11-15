// SPDX-License-Identifier: 0BSD
// Copyright (C) 2026 by I.C.KaZe
#define WB_CORE_IMPLEMENTATION
#include "core_api.h"
#include "pdx_editor_embed.h"
#include "../pdxeditor/src/App.h"
#include <condition_variable>
#include <thread>
#include <objbase.h>

std::shared_ptr<NdsifVoice> WB_SharedPreview(const std::vector<float>&,uint32_t);

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
                auto& audio=NdsifAudio::Instance();
                audio.SetHost([](int com,std::wstring& error) {
                    BridgeRequest request{30,com,nullptr,nullptr,nullptr,nullptr};
                    if (WB_Command(&request)) return true;
                    char text[4096]{}; WB_Error(text,sizeof(text)); error=Utf8ToW(text); return false;
                }, [] {
                    BridgeRequest request{31,0,nullptr,nullptr,nullptr,nullptr}; WB_Command(&request);
                }, [](const std::vector<float>& samples,uint32_t rate) {
                    return WB_SharedPreview(samples,rate);
                });
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
