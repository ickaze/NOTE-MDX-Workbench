// SPDX-License-Identifier: 0BSD
// Copyright (C) 2026 by I.C.KaZe
#pragma once
// Shared declaration of the existing Windows C ABI. Both the DLL definitions
// and GUI loader compile against this file, so argument units/counts are checked.
#include <windows.h>
#include <cstddef>
#include <cstdint>
struct BridgeState
{
    std::uint32_t size, playing, paused;
    std::int32_t output, midi;
    std::uint32_t reserved;
    std::uint64_t position, total, offsets[16];
    std::uint8_t registers[256];
    std::int32_t notes[16];
    float waveform[16][256];
    std::uint32_t cursor, muteMask, masterPercent, pcmPercent;
    std::int32_t fmPercent, measured, dac, fmDelay, pcmDelay, key, keyMode, speed, polyphonic, aftertouch;
    std::uint64_t scoreRevision;
};
struct BridgeRequest
{
    std::int32_t command, value;
    const wchar_t *path, *bank, *source;
    BridgeState *state;
};
static_assert(sizeof(BridgeState) == 16936, "Bridge state ABI changed");
static_assert(sizeof(BridgeRequest) == 40, "Bridge request ABI changed");
#ifdef WB_CORE_IMPLEMENTATION
#define WB_CORE_API extern "C" __declspec(dllexport)
#else
#define WB_CORE_API extern "C"
#endif
WB_CORE_API int WB_Init(HWND owner);
WB_CORE_API int WB_Command(const BridgeRequest *request);
WB_CORE_API int WB_State(BridgeState *state, std::size_t size);
WB_CORE_API int WB_Error(char *text, std::size_t capacity);
WB_CORE_API int WB_Status(char *text, std::size_t capacity);
WB_CORE_API void WB_Shutdown();
WB_CORE_API int WB_Compile(const wchar_t *, const wchar_t *, const wchar_t *, int, int, int, int, char *,
                           std::size_t);
WB_CORE_API int WB_CompileDecorated(const wchar_t *, const wchar_t *, const wchar_t *, int, int, int, int,
                                    char *, std::size_t);
WB_CORE_API int WB_EditorCheck(const wchar_t *, const wchar_t *, const wchar_t *, int, int, int, int, char *,
                               std::size_t);
WB_CORE_API int WB_SelectionStats(const wchar_t *, const wchar_t *, const wchar_t *, int, int, int, int,
                                  char *, std::size_t);
WB_CORE_API void WB_ApplyVoiceCatalog(const std::uint8_t *, std::size_t);
WB_CORE_API int WB_AnalysisStart(unsigned kind);
WB_CORE_API void WB_AnalysisStop(unsigned kind);
WB_CORE_API int WB_AnalysisSubmit(unsigned, std::uint64_t, std::uint64_t, std::uint32_t, std::uint32_t,
                                  const wchar_t *, const char *, std::size_t, int);
WB_CORE_API std::int64_t WB_AnalysisTake(unsigned, char *, std::size_t, int);
WB_CORE_API void WB_ExportStart();
WB_CORE_API int WB_ExportCheckpoint();
WB_CORE_API int WB_ExportCancelled();
WB_CORE_API void WB_ExportControl(int action);
WB_CORE_API int WB_VgmProgress();
WB_CORE_API void WB_WavStart(int jobs);
WB_CORE_API int WB_WavProgress();
WB_CORE_API int WB_PublishOutput(const wchar_t *source, const wchar_t *target);
WB_CORE_API int WB_FinalizeOutput(const wchar_t *source, const wchar_t *target, unsigned format,
                                  const char *utf8Title, std::size_t titleBytes);
WB_CORE_API int WB_SaveVgm(const wchar_t *, const wchar_t *, const wchar_t *, const wchar_t *);
WB_CORE_API int WB_SaveMrl(const wchar_t *, const wchar_t *, const wchar_t *, const wchar_t *);
WB_CORE_API int WB_SaveWav(const wchar_t *, const wchar_t *, const wchar_t *, const wchar_t *, int, int, int,
                           int, int);
WB_CORE_API int WB_SaveWavFrozen(const wchar_t *, const wchar_t *, const wchar_t *, const wchar_t *, int, int,
                                 int, int, int, int, int, int, int, int);
WB_CORE_API int WB_Score(char *, std::size_t);
WB_CORE_API int WB_VoiceEdit(char *, std::size_t);
WB_CORE_API int WB_EditorDecoration(HWND, const std::int32_t *, std::size_t);
WB_CORE_API int WB_EditorBackground(HWND, const std::int32_t *, std::size_t);
WB_CORE_API int WB_OpenPdx(HWND, const wchar_t *, const wchar_t *);
WB_CORE_API int WB_PdxIsOpen();
WB_CORE_API int WB_ClosePdx();
#undef WB_CORE_API
#define WB_CORE_FUNCTIONS(X)                                                                                 \
    X(WB_Init)                                                                                               \
    X(WB_Command) X(WB_State) X(WB_Error) X(WB_Status) X(WB_Shutdown) X(WB_Compile) X(WB_CompileDecorated)   \
        X(WB_EditorCheck) X(WB_SelectionStats) X(WB_ApplyVoiceCatalog) X(WB_AnalysisStart)                   \
            X(WB_AnalysisStop) X(WB_AnalysisSubmit) X(WB_AnalysisTake) X(WB_ExportStart)                     \
                X(WB_ExportCheckpoint) X(WB_ExportCancelled) X(WB_ExportControl) X(WB_VgmProgress)           \
                    X(WB_WavStart) X(WB_WavProgress) X(WB_PublishOutput) X(WB_FinalizeOutput) X(WB_SaveVgm)  \
                        X(WB_SaveMrl) X(WB_SaveWav) X(WB_SaveWavFrozen) X(WB_Score) X(WB_VoiceEdit)          \
                            X(WB_EditorDecoration) X(WB_EditorBackground) X(WB_OpenPdx) X(WB_PdxIsOpen)      \
                                X(WB_ClosePdx)
