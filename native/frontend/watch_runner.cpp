// SPDX-License-Identifier: 0BSD
// Copyright (C) 2026 by I.C.KaZe
#include "source_watch.h"
#include <iostream>
#include <string>
int main()
{
    workbench::SourceWatchState state{};
    std::string source, pdx;
    while (std::cin >> source >> pdx)
    {
        if (source == "reset")
        {
            state.fill(0);
            continue;
        }
        const auto parse = [](const std::string &text) -> std::optional<std::uint64_t>
        {
            if (text == "missing") { return std::nullopt; }
            return std::stoull(text);
        };
        const bool changed = workbench::observeSourceWatch(state, parse(source), parse(pdx));
        std::cout << changed;
        for (const auto word : state) { std::cout << ' ' << word; }
        std::cout << '\n';
    }
}
