#pragma once
#include <windows.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cwchar>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <vector>
#include <thread>

#include "NdsifPreview.h"
#include <functional>

class NdsifAudio {
public:
    static NdsifAudio& Instance();
    ~NdsifAudio();
    using OpenHandler = std::function<bool(int,std::wstring&)>;
    void SetHost(OpenHandler open, std::function<void()> close,
                 std::function<std::shared_ptr<NdsifVoice>(const std::vector<float>&,uint32_t)> play) {
        hostOpen_=std::move(open); hostClose_=std::move(close); hostPlay_=std::move(play);
    }
    bool Open(int com, std::wstring& error);
    bool OpenLocal(int com, std::wstring& error);
    void CloseLocal();
    void AttachShared(int com);
    void ReportSharedError(const std::wstring& error) { std::lock_guard<std::mutex> guard(mutex_); ready_=false; error_=error; }
    int Port() const { return com_; }
    bool Shared() const { return shared_; }
    void Close();
    bool Selected() const { return selected_; }
    std::shared_ptr<NdsifVoice> Play(const std::vector<float>& samples, uint32_t rate);
    std::wstring TakeError();
    static std::vector<int> Ports();
private:
    OpenHandler hostOpen_;
    std::function<void()> hostClose_;
    std::function<std::shared_ptr<NdsifVoice>(const std::vector<float>&,uint32_t)> hostPlay_;
    std::atomic<int> com_{0};
    std::atomic<bool> shared_{false};
    void Run();
    struct Transport;
    std::unique_ptr<Transport> transport_;
    std::thread worker_;
    std::atomic<bool> quit_{false}, selected_{false}, ready_{false};
    std::mutex mutex_;
    std::vector<std::shared_ptr<NdsifVoice>> voices_;
    uint64_t generated_ = 0;
    std::wstring error_;
};
