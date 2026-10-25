#pragma once
#ifdef _WIN32
#include <windows.h>
#include <filesystem>
#include <atomic>
#include <string>
#include <stdexcept>
#include <array>
#include <algorithm>
#include "x68_protocol.h"
namespace mdx {
class X68Bridge {
    HANDLE mapping = nullptr, request = nullptr, reply = nullptr, process = nullptr, job = nullptr;
    X68Shared *shared = nullptr;
    uint32_t frame = 0, count = 0;
    bool pending = false;
    std::array<X68Event, x68Events> events{};
    std::array<int16_t, x68Frames * 2> ready{};
    void wait() {
        HANDLE handles[]{reply, process};
        if (WaitForMultipleObjects(2, handles, FALSE, 5000) != WAIT_OBJECT_0)
            throw std::runtime_error("X68Sound helper exited or did not respond within 5 seconds");
        if (shared->status)
            throw std::runtime_error(std::string("X68Sound: ") + shared->error);
    }
    void close() noexcept {
        if (process && shared && request && (!pending || WaitForSingleObject(reply, 500) == WAIT_OBJECT_0)) {
            shared->command = 2;
            SetEvent(request);
            WaitForSingleObject(process, 500);
        }
        if (job)
            CloseHandle(job); // Kill only this helper if DLL teardown stalled.
        if (process)
            CloseHandle(process);
        if (shared)
            UnmapViewOfFile(shared);
        for (HANDLE h : {mapping, request, reply})
            if (h)
                CloseHandle(h);
    }

  public:
    X68Bridge() {
        try {
            static std::atomic<unsigned> serial{0};
            std::wstring name = L"Local\\IndependentMDX-X68-" + std::to_wstring(GetCurrentProcessId()) +
                                L"-" + std::to_wstring(++serial);
            mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(X68Shared),
                                         name.c_str());
            if (!mapping || GetLastError() == ERROR_ALREADY_EXISTS)
                throw std::runtime_error("Cannot create X68Sound shared buffer");
            shared = static_cast<X68Shared *>(
                MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(X68Shared)));
            request = CreateEventW(nullptr, FALSE, FALSE, (name + L"-request").c_str());
            reply = CreateEventW(nullptr, FALSE, FALSE, (name + L"-reply").c_str());
            job = CreateJobObjectW(nullptr, nullptr);
            if (!shared || !request || !reply || !job)
                throw std::runtime_error("Cannot create X68Sound IPC");
            *shared = {};
            shared->magic = x68Magic;
            JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
            limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
            if (!SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits)))
                throw std::runtime_error("Cannot configure X68Sound helper job");
            wchar_t path[32768];
            GetModuleFileNameW(nullptr, path, 32768);
            auto exe = std::filesystem::path(path).parent_path() / L"IndependentMDX-X68Bridge32.exe";
            std::wstring command = L"\"" + exe.wstring() + L"\" \"" + name + L"\"";
            STARTUPINFOW si{};
            si.cb = sizeof(si);
            PROCESS_INFORMATION pi{};
            if (!CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, FALSE,
                                CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr, nullptr, &si, &pi))
                throw std::runtime_error(
                    "Cannot start IndependentMDX-X68Bridge32.exe beside the application");
            process = pi.hProcess;
            if (!AssignProcessToJobObject(job, process)) {
                TerminateProcess(process, 1);
                CloseHandle(pi.hThread);
                throw std::runtime_error("Cannot attach X68Sound helper job");
            }
            ResumeThread(pi.hThread);
            CloseHandle(pi.hThread);
            wait();
        } catch (...) {
            close();
            throw;
        }
    }
    ~X68Bridge() { close(); }
    X68Bridge(const X68Bridge &) = delete;
    void write(uint8_t a, uint8_t v) {
        if (count >= x68Events)
            throw std::runtime_error("X68Sound register block overflow");
        events[count++] = {frame, a, v};
    }
    std::array<float, 2> advance() {
        std::array<float, 2> out{ready[frame * 2] / 32768.0f, ready[frame * 2 + 1] / 32768.0f};
        if (++frame == x68Frames) {
            if (pending) {
                wait();
                std::copy_n(shared->audio, ready.size(), ready.data());
            }
            std::copy_n(events.data(), count, shared->events);
            shared->count = count;
            shared->command = 1;
            SetEvent(request);
            pending = true;
            frame = 0;
            count = 0;
        }
        return out;
    }
};
} // namespace mdx
#endif
