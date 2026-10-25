// SPDX-License-Identifier: 0BSD
// Copyright (C) 2026 by I.C.KaZe
// Test-only entry point. No code generation or expected values live here.
#include "output_pipeline.h"
#include <iostream>
#include <stdexcept>

int main(int argc, char **argv)
{
    if (argc < 5) { return 2; }
    unsigned calls = 0;
    int progress = 0;
    int cancelAt = -1;
    if (argc > 5)
    {
        cancelAt = std::stoi(argv[5]);
    }
    try
    {
        const workbench::OutputControl control{
            [&]
            {
                if (static_cast<int>(calls++) == cancelAt) { throw std::runtime_error("Export cancelled"); }
            },
            [&](int value)
            {
                if (value < progress || value > 1000) { throw std::runtime_error("Progress reversed"); }
                progress = value;
            }
        };
        if (std::string(argv[3]) == "publish")
        {
            workbench::publishOutput(std::filesystem::u8path(argv[1]), std::filesystem::u8path(argv[2]), control);
        }
        else
        {
            workbench::finalizeOutput(std::filesystem::u8path(argv[1]), std::filesystem::u8path(argv[2]),
                static_cast<workbench::OutputFormat>(std::stoul(argv[3])), argv[4], control);
        }
        std::cout << calls << " " << progress << "\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << "\n";
        return 1;
    }
}
