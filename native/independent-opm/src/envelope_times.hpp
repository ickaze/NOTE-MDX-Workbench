// SPDX-License-Identifier: 0BSD
// Copyright (C) 2026 by I.C.KaZe
#pragma once
#include <array>
#include <limits>

namespace independent_opm::specification {
// Yamaha YM2151 OPM Application Manual, Fig.2.11 (printed p.19).
// Full traversal times in milliseconds, at 3.6 MHz, indexed by scaled rate.
// These are published rounded performance figures, NOT decoded internal EG ROM.
constexpr double infinite = std::numeric_limits<double>::infinity();
constexpr std::array<double,64> attack_ms = {
    infinite,infinite,infinite,infinite,
    7723.38,6378.04,5355.70,4354.31,
    3986.77,3187.43,2657.85,2278.14,
    1993.37,1594.71,1328.92,1139.08,
    996.67,797.35,664.46,569.54,
    498.33,398.68,332.23,284.77,
    249.17,199.34,166.12,142.38,
    124.59,99.67,83.06,71.19,
    62.29,49.83,41.53,35.60,
    31.15,24.92,20.76,17.80,
    15.57,12.46,10.38,8.90,
    7.79,6.23,5.19,4.45,
    3.89,3.11,2.60,2.22,
    2.13,1.71,1.42,1.22,
    1.12,.90,.77,.64,
    .51,.51,.51,0
};
// Keep the existing decay interpolation until ambiguous cells in the scan
// can be independently checked. Explicit high-rate anchors are legible.
constexpr std::array<double,12> fast_decay_ms = {
    26.91,21.53,17.94,15.38,13.45,10.76,8.97,7.48,6.73,6.73,6.73,6.73
};
}
