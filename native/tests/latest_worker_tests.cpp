// SPDX-License-Identifier: 0BSD
// Copyright (C) 2026 by I.C.KaZe
#include "latest_worker.h"
#include <array>
#include <chrono>
#include <future>
#include <iostream>
#include <stdexcept>
#include <vector>

using namespace std::chrono_literals;
void require(bool condition, const char *message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}
template<class Request> std::string receive(workbench::LatestWorker<Request> &worker)
{
    auto size = worker.receive(nullptr, 0, true);
    std::vector<char> buffer(size);
    require(size != 0, "unexpected stopped worker");
    require(worker.receive(buffer.data(), buffer.size(), true) == size, "result changed unexpectedly");
    return std::string(buffer.data(), size - 1);
}
int main()
{
    try
    {
        // A blocked first request makes replacement order deterministic, with
        // no reliance on compiler duration or sleep-based scheduling guesses.
        std::promise<void> entered;
        std::promise<void> release;
        auto released = release.get_future().share();
        std::vector<int> processed;
        workbench::LatestWorker<int> latest([&](const int &value)
        {
            processed.push_back(value);
            if (value == 1)
            {
                entered.set_value();
                released.wait();
            }
            return std::to_string(value);
        });
        latest.submit(1);
        entered.get_future().wait();
        for (int value = 2; value <= 1000; ++value)
        {
            require(latest.submit(value), "live submission rejected");
        }
        release.set_value();
        require(receive(latest) == "1000", "obsolete result delivered");
        latest.stop();
        require(processed == std::vector<int>({1, 1000}), "pending queue did not coalesce");
        require(!latest.submit(1001), "stopped worker accepted request");

        workbench::LatestWorker<int> failures([](const int &value)
        {
            if (value == 0)
            {
                throw std::runtime_error("expected processor error");
            }
            return std::string("a\0b", 3);
        });
        failures.submit(0);
        bool caught = false;
        try
        {
            failures.receive(nullptr, 0, true);
        }
        catch (const std::runtime_error &)
        {
            caught = true;
        }
        require(caught, "processor exception lost");
        failures.submit(1);
        require(failures.receive(nullptr, 0, true) == 4, "wrong result size");
        std::array<char, 12> buffer;
        buffer.fill('Z');
        require(failures.receive(buffer.data(), 3, false) == 4, "short buffer wrong size");
        for (const auto value : buffer)
        {
            require(value == 'Z', "short buffer modified");
        }
        require(failures.receive(buffer.data(), buffer.size(), false) == 4, "result not retained");
        require(std::string(buffer.data(), 3) == std::string("a\0b", 3) && buffer[3] == 0,
            "embedded NUL or sentinel changed");
        require(buffer[4] == 'Z' && failures.receive(nullptr, 0, false) == 0,
            "guard overwritten or result delivered twice");

        std::promise<void> inputEntered;
        std::promise<void> inputRelease;
        auto inputReady = inputRelease.get_future().share();
        workbench::LatestWorker<std::string> ownership([&](const std::string &input)
        {
            inputEntered.set_value();
            inputReady.wait();
            return input;
        });
        std::string input(1024 * 1024, 'A');
        input[100] = 0;
        const auto expected = input;
        ownership.submit(input);
        inputEntered.get_future().wait();
        input.assign(input.size(), 'Z');
        inputRelease.set_value();
        require(receive(ownership) == expected, "worker retained mutable caller storage");

        std::promise<void> busy;
        std::promise<void> unblock;
        auto unblockFuture = unblock.get_future().share();
        int stopCalls = 0;
        workbench::LatestWorker<int> stopping([&](const int &)
        {
            ++stopCalls;
            busy.set_value();
            unblockFuture.wait();
            return std::string("discarded");
        });
        stopping.submit(1);
        busy.get_future().wait();
        stopping.submit(2);
        auto stopped = std::async(std::launch::async, [&] { stopping.stop(); });
        // Stop cannot complete until the already running processor has finished.
        bool rejected = false;
        const auto deadline = std::chrono::steady_clock::now() + 2s;
        while (std::chrono::steady_clock::now() < deadline)
        {
            if (!stopping.submit(3))
            {
                rejected = true;
                break;
            }
            std::this_thread::yield();
        }
        const bool premature = stopped.wait_for(0ms) == std::future_status::ready;
        unblock.set_value();
        stopped.get();
        require(rejected && !premature, "stop did not join processor");
        require(stopCalls == 1, "pending processor ran after stop");
        require(stopping.receive(nullptr, 0, false) == 0, "result survived stop");

        // Repeated construction/shutdown also checks that blocked result
        // receivers wake, including when stop wins before receive starts.
        for (int iteration = 0; iteration < 100; ++iteration)
        {
            workbench::LatestWorker<int> empty([](const int &) { return std::string(); });
            auto reader = std::async(std::launch::async, [&] { return empty.receive(nullptr, 0, true); });
            empty.stop();
            require(reader.get() == 0, "blocked receiver did not wake");
        }
        std::cout << "latest worker: 1000 coalesced submissions, stale rejection, binary result, "
            "owned input, short buffers, exceptions, pending cancellation, join and 100 shutdown cycles passed\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
