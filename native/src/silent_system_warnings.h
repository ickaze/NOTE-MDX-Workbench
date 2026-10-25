// SPDX-License-Identifier: 0BSD
// Copyright (C) 2026 by I.C.KaZe
#pragma once
#include <windows.h>
#include <vector>

namespace {
BOOL WINAPI silentMessageBeep(UINT) { return TRUE; }
BOOL WINAPI silentBeep(DWORD, DWORD) { return TRUE; }
struct WarningImport { HMODULE module; void **slot; void *original; void *replacement; };
std::vector<WarningImport> warningImports;

// Patch only this process's warning API imports, not audio output APIs or Windows
// sound preferences. This also covers boundary warnings inside native RichEdit.
void silenceSystemWarnings() {
  if (!warningImports.empty()) return;
  auto messageBeep = reinterpret_cast<void *>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "MessageBeep"));
  auto beep = reinterpret_cast<void *>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "Beep"));
  const wchar_t *names[] = {L"msftedit.dll", L"riched20.dll", L"comctl32.dll", L"comdlg32.dll", L"user32.dll", L"workbench_core.dll", nullptr};
  for (auto name : names) {
    auto module = GetModuleHandleW(name);
    if (!module) continue;
    auto base = reinterpret_cast<BYTE *>(module);
    auto dos = reinterpret_cast<IMAGE_DOS_HEADER *>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) continue;
    auto nt = reinterpret_cast<IMAGE_NT_HEADERS *>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) continue;
    auto directory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!directory.VirtualAddress) continue;
    auto imports = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR *>(base + directory.VirtualAddress);
    for (; imports->Name; ++imports) {
      if (!imports->FirstThunk) continue;
      auto thunk = reinterpret_cast<IMAGE_THUNK_DATA *>(base + imports->FirstThunk);
      for (; thunk->u1.Function; ++thunk) {
        auto slot = reinterpret_cast<void **>(&thunk->u1.Function);
        void *replacement = *slot == messageBeep ? reinterpret_cast<void *>(&silentMessageBeep) :
                            *slot == beep ? reinterpret_cast<void *>(&silentBeep) : nullptr;
        if (!replacement) continue;
        DWORD previous = 0;
        if (!VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &previous)) continue;
        auto original = InterlockedExchangePointer(slot, replacement);
        DWORD ignored = 0;
        VirtualProtect(slot, sizeof(*slot), previous, &ignored);
        warningImports.push_back({module, slot, original, replacement});
      }
    }
  }
}
void restoreSystemWarningImports() {
  for (auto &entry : warningImports) {
    HMODULE current = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                          reinterpret_cast<LPCWSTR>(entry.slot), &current) || current != entry.module) continue;
    DWORD previous = 0;
    if (!VirtualProtect(entry.slot, sizeof(*entry.slot), PAGE_READWRITE, &previous)) continue;
    InterlockedCompareExchangePointer(entry.slot, entry.original, entry.replacement);
    DWORD ignored = 0;
    VirtualProtect(entry.slot, sizeof(*entry.slot), previous, &ignored);
  }
  warningImports.clear();
}
} // namespace
