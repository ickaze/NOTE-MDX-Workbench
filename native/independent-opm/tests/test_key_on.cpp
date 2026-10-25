// SPDX-License-Identifier: 0BSD
// Copyright (C) 2026 by I.C.KaZe
#include "ym2151.hpp"
#include "voice_regression.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <stdexcept>
using namespace independent_opm;
void require(bool b,const char* m) { if(!b) throw std::runtime_error(m); }
void expect_keys(const Ym2151& c,unsigned ch,unsigned mask) {
    require(c.inspect(ch,0).key==bool(mask&1),"M1 key");
    require(c.inspect(ch,2).key==bool(mask&2),"C1 key");
    require(c.inspect(ch,1).key==bool(mask&4),"M2 key");
    require(c.inspect(ch,3).key==bool(mask&8),"C2 key");
}
int main() { try {
    for(unsigned ch=0;ch<8;++ch) for(unsigned mask=0;mask<16;++mask) {
        Ym2151 c(4000000);
        c.write_register(8,static_cast<unsigned char>(0x78|ch));
        c.write_register(8,static_cast<unsigned char>((mask<<3)|ch));
        expect_keys(c,ch,mask);
        for(unsigned other=0;other<8;++other) if(other!=ch) expect_keys(c,other,0);
        c.write_register(0x10,255);c.write_register(0x11,3);
        c.write_register(0x14,129);c.advance(64);
        expect_keys(c,ch,mask); // CSM pulse must preserve the manual key mask.
        c.write_register(0x14,0);c.write_register(8,static_cast<unsigned char>(ch));
        expect_keys(c,ch,0);
    }
    for(unsigned v=0;v<3;++v) for(bool timing:{false,true}) {
        Ym2151 c(4000000);c.set_measured_output_timing(timing);
        regression::load([&](unsigned a,unsigned d){c.write_register(a,d);},v);
        expect_keys(c,0,3);
        double peak=0,energy=0;
        for(unsigned n=0;n<125000;++n) {
            c.advance(64);auto s=c.last_sample();
            require(std::isfinite(s.left)&&std::isfinite(s.right),"nonfinite audio");
            peak=std::max(peak,std::abs(s.left));energy+=s.left*s.left;
        }
        const double rms=std::sqrt(energy/125000);
        std::printf("@%d ALG%d timing=%d peak=%.9g rms=%.9g\n",regression::ids[v],regression::algorithms[v],timing,peak,rms);
        require(peak>0.01 && rms>0.001,"user voice silent or missing C1");
    }
    std::puts("PASS: 128 key masks, channel isolation, key-off, CSM, 3 corrected voices");
    return 0;
} catch(const std::exception& e) { std::fprintf(stderr,"FAIL: %s\n",e.what());return 1; } }
