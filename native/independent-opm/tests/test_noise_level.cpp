// SPDX-License-Identifier: 0BSD
// Copyright (C) 2026 by I.C.KaZe
#include "ym2151.hpp"
#include <cmath>
#include <cstdio>
#include <stdexcept>
using namespace independent_opm;
double rms(bool noise,unsigned alg,unsigned tl,unsigned nfrq) {
    Ym2151 c(4000000);
    c.write_register(0x27,0xc0|alg);c.write_register(0x2f,0x48);
    c.write_register(0x5f,1);c.write_register(0x7f,tl);
    c.write_register(0x9f,31);c.write_register(0xbf,0);
    c.write_register(0xdf,0);c.write_register(0xff,15);
    c.write_register(0x0f,noise?(128|nfrq):0);c.write_register(8,0x47);
    c.advance(64000);
    double energy=0;
    for(unsigned i=0;i<62500;++i){c.advance(64);auto s=c.last_sample();energy+=s.left*s.left;}
    return std::sqrt(energy/62500);
}
int main(){try{
    const double sine_peak=rms(false,7,0,0)*std::sqrt(2.0);
    for(unsigned alg=0;alg<8;++alg) for(unsigned freq:{0u,16u,31u}) {
        const double ratio=rms(true,alg,0,freq)/sine_peak;
        // Hardware 43NOISE: approx .24967 including capture filtering.
        if(!(ratio>.24 && ratio<.26))throw std::runtime_error("noise normalization outside measured band");
    }
    // Hardware normalized TL32=.749084, TL64=.498167. Allow the retained
    // approximate endpoint curve, but reject exponential TL attenuation.
    const double base=rms(true,7,0,0);
    if(std::abs(rms(true,7,32,0)/base-.749084)>.003 ||
       std::abs(rms(true,7,64,0)/base-.498167)>.003)
        throw std::runtime_error("noise TL differs from hardware envelope");
    std::puts("PASS: hardware noise normalization band / 8 algorithms / 3 frequencies / TL curve");
    return 0;
}catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}}
