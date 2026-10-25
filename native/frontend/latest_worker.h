// SPDX-License-Identifier: 0BSD
// Copyright (C) 2026 by I.C.KaZe
#pragma once
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <exception>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>

namespace workbench
{
// One running request, one replaceable pending request, one replaceable result.
// The processor owns all request data and never runs under the queue mutex.
template<class Request> class LatestWorker
{
  public:
    using Processor = std::function<std::string(const Request &)>;
    explicit LatestWorker(Processor processor) : processor_(std::move(processor))
    {
        thread_ = std::thread([this] { run(); });
    }
    LatestWorker(const LatestWorker &) = delete;
    LatestWorker &operator=(const LatestWorker &) = delete;
    ~LatestWorker() { stop(); }

    bool submit(Request request)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopping_)
        {
            return false;
        }
        ++sequence_;
        pending_ = std::move(request);
        result_.reset();
        failure_ = nullptr;
        changed_.notify_all();
        return true;
    }
    // A size query or undersized buffer never consumes the result. A producer
    // may replace it between calls; the caller retries with the returned size.
    std::size_t receive(char *destination, std::size_t capacity, bool wait)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        if (wait)
        {
            changed_.wait(lock, [this] { return stopping_ || result_ || failure_; });
        }
        if (stopping_)
        {
            return 0;
        }
        if (failure_)
        {
            auto error = std::exchange(failure_, nullptr);
            std::rethrow_exception(error);
        }
        if (!result_)
        {
            return 0;
        }
        const auto required = result_->size() + 1;
        if (destination && capacity >= required)
        {
            std::memcpy(destination, result_->c_str(), required);
            result_.reset();
        }
        return required;
    }
    void stop()
    {
        std::lock_guard<std::mutex> lifecycle(stopMutex_);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stopping_ = true;
            pending_.reset();
            result_.reset();
            failure_ = nullptr;
            changed_.notify_all();
        }
        if (thread_.joinable())
        {
            thread_.join();
        }
    }
  private:
    void run()
    {
        for (;;)
        {
            std::unique_lock<std::mutex> lock(mutex_);
            changed_.wait(lock, [this] { return stopping_ || pending_; });
            if (stopping_)
            {
                return;
            }
            auto request = std::move(*pending_);
            pending_.reset();
            const auto sequence = sequence_;
            lock.unlock();
            std::optional<std::string> result;
            std::exception_ptr error;
            try
            {
                result = processor_(request);
            }
            catch (...)
            {
                error = std::current_exception();
            }
            lock.lock();
            if (!stopping_ && sequence == sequence_)
            {
                result_ = std::move(result);
                failure_ = error;
                changed_.notify_all();
            }
        }
    }
    Processor processor_;
    std::mutex mutex_;
    std::mutex stopMutex_;
    std::condition_variable changed_;
    std::optional<Request> pending_;
    std::optional<std::string> result_;
    std::exception_ptr failure_;
    std::uint64_t sequence_ = 0;
    bool stopping_ = false;
    std::thread thread_;
};
}
