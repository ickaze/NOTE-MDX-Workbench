// SPDX-License-Identifier: 0BSD
// Copyright (C) 2026 by I.C.KaZe
#include "ym2151.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>
using namespace independent_opm;
void check(bool b,const char* s) { if(!b) throw std::runtime_error(s); }
void tone(Ym2151& c,unsigned pan=192) {
    c.write_register(0x20,static_cast<std::uint8_t>(pan|7));
    c.write_register(0x28,0x4a); // manual p.6: A=440 Hz at reference clock
    c.write_register(0x40,1); c.write_register(0x80,0xdf);
    c.write_register(0xe0,15); c.write_register(8,8); // M1 only
}
void collect(void* p,Stereo s) { static_cast<std::vector<Stereo>*>(p)->push_back(s); }
int main() {
 try {
    Ym2151 c;
    c.advance(640); check(c.last_sample().left==0 && c.status()==0,"reset silence");
    c.write_address(0x1b); check(c.write_data(0xc0),"bus first write");
    check(!c.write_data(0),"busy rejection"); check(c.control_outputs()==3,"rejected side effect");
    c.advance(67); check(c.status()&128,"busy 67"); c.advance(1); check(!(c.status()&128),"busy 68");
    c.reset(); c.write_register(0x10,255); c.write_register(0x11,3); c.write_register(0x14,5);
    c.advance(63); check(!(c.status()&1),"timer premature"); c.advance(1); check(c.status()&1,"timer A period");
    c.write_register(0x14,0x15); check(!(c.status()&1),"timer clear");
    c.advance(64); check(c.irq(),"timer reload");
    c.reset(); c.write_register(0x12,255); c.write_register(0x14,10); c.advance(1023);
    check(!(c.status()&2),"timer B premature"); c.advance(1); check(c.status()&2,"timer B period");
    c.reset(); tone(c,64); check(std::abs(c.inspect(0,0).frequency_hz-440)<1e-9,"A440");
    std::vector<Stereo> frames; c.advance(3579545,collect,&frames);
    unsigned crossings=0; double energy=0;
    for(std::size_t i=1;i<frames.size();++i) {
        if(frames[i-1].left<=0 && frames[i].left>0) ++crossings;
        energy+=frames[i].left*frames[i].left; check(frames[i].right==0,"left pan isolation");
    }
    check(crossings>=439 && crossings<=441 && energy>100,"generated pitch/energy");
    c.write_register(0x40,0); check(std::abs(c.inspect(0,0).frequency_hz-220)<1e-9,"MUL zero half");
    c.write_register(0x40,1); c.write_register(0x30,128);
    check(std::abs(c.inspect(0,0).frequency_hz-440*std::exp2(1.0/24))<1e-8,"KF half semitone");
    c.write_register(8,0); c.advance(3579545); check(c.inspect(0,0).stage==Envelope::off,"release ends");
    Ym2151 a,b; tone(a); tone(b);
    std::vector<Stereo> x,y; a.advance(123456,collect,&x);
    for(int i=0;i<123456;++i) b.advance(1,collect,&y);
    check(x.size()==y.size(),"chunk count");
    for(std::size_t i=0;i<x.size();++i) check(x[i].left==y[i].left,"chunk determinism");
    auto saved=a; a.advance(9000); saved.advance(9000); check(a.last_sample().left==saved.last_sample().left,"state copy");
    for(unsigned alg=0;alg<8;++alg) {
        Ym2151 chip; tone(chip); chip.write_register(0x20,static_cast<std::uint8_t>(192|alg));
        for(unsigned s=0;s<4;++s) { chip.write_register(static_cast<std::uint8_t>(0x40+s*8),1); chip.write_register(static_cast<std::uint8_t>(0x80+s*8),0xdf); }
        chip.write_register(8,0x78); chip.advance(10000);
        check(std::isfinite(chip.last_sample().left),"algorithm finite");
    }
    Ym2151 csm; tone(csm); csm.write_register(0x10,255); csm.write_register(0x11,3);
    csm.write_register(0x14,129); csm.advance(64);
    check(csm.inspect(0,0).key && !csm.inspect(0,1).key,"CSM preserves manual key");
    Ym2151 noise; noise.write_register(0x27,0xc7); noise.write_register(0x9f,0xdf);
    noise.write_register(0x0f,0x9f); noise.write_register(8,0x47); noise.advance(640);
    check(std::abs(noise.last_sample().left)>.01,"noise slot produces audio");
    Ym2151 lfo; tone(lfo); lfo.write_register(0xa0,128); lfo.write_register(0x38,3);
    lfo.write_register(0x19,127); lfo.advance(1000); auto with_am=lfo.last_sample();
    auto lfo_copy=lfo; lfo_copy.write_register(0x19,255);
    lfo.advance(640); lfo_copy.advance(640);
    check(lfo.last_sample().left==lfo_copy.last_sample().left,"PMD preserves AMD when PMS zero");
    check(std::isfinite(with_am.left),"LFO finite");
    Resampler r(c.native_rate(),48000); for(int i=0;i<100;++i) r.push({.25,-.5});
    auto f=r.output(.3); check(std::abs(f.left-.25)<1e-12 && std::abs(f.right+.5)<1e-12,"resampler DC gain");
    std::cout<<"PASS: reset, BUSY, CT, timer A/B, A440 waveform, pan, MUL, KF, release, chunking, state copy, algorithms, resampler\n";
    return 0;
 } catch(const std::exception& e) { std::cerr<<"FAIL: "<<e.what()<<"\n"; return 1; }
}
