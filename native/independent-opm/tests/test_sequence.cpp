// SPDX-License-Identifier: 0BSD
// Copyright (C) 2026 by I.C.KaZe
#include "ym2151.hpp"
#include <iostream>
#include <stdexcept>
int main() {
    using namespace independent_opm::detail;
    for (unsigned s=0;s<(1u<<17);++s) {
        unsigned reference=s;
        for(unsigned k=0;k<16;++k)reference=sequence_step(reference);
        if(reference!=sequence_advance16(s))throw std::runtime_error("16-step mismatch");
    }
    std::cout<<"PASS: 131072 states, direct expression equals 16 sequential updates\n";
}
