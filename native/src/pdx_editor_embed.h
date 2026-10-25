// SPDX-License-Identifier: 0BSD
// Copyright (C) 2026 by I.C.KaZe
#pragma once
#include <windows.h>
extern "C" __declspec(dllexport) int WB_OpenPdx(HWND owner, const wchar_t *project, const wchar_t *language);
extern "C" __declspec(dllexport) int WB_PdxIsOpen();
extern "C" __declspec(dllexport) int WB_ClosePdx();
