// SPDX-License-Identifier: 0BSD
// Copyright (C) 2026 by I.C.KaZe
#include "ym2151.hpp"
#include "periodic_recording_vectors.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>
using namespace independent_opm;
void check(bool yes,const char* why) { if(!yes) throw std::runtime_error(why); }
void save(void* p,Stereo s) { static_cast<std::vector<double>*>(p)->push_back(s.left); }
int main() { try {
    for(const auto& t:recording_vectors::steps) {
        detail::PeriodicModulator clock;clock.order=t.origin;
        for(unsigned i=0;i<t.length;++i) {
            clock.wait_samples=detail::PeriodicModulator::interval(t.freq)-1;
            check(clock.next_increment(t.freq)==recording_vectors::increments[t.offset+i],"recorded step order");
        }
    }
    for(const auto& p:recording_vectors::am) {
        Ym2151 c;c.write_register(0x1b,1);c.write_register(0x19,static_cast<std::uint8_t>(p.depth));
        c.write_register(1,2);c.advance(64);
        check(c.inspect_lfo().am_after_depth==p.units,"recorded square-AM level");
    }
    // Software policy checks, not claims about exact hardware bus timing.
    Ym2151 a(4000000);a.write_register(0x18,0xf0);a.advance(7*64);
    check(a.inspect_lfo().phase==0,"highest octave boundary");a.advance(64);
    check(a.inspect_lfo().phase==1,"8 sample update");
    a.advance(3*64);a.write_register(0x18,0xf0);a.advance(7*64);
    check(a.inspect_lfo().phase==1,"same write preserves position");a.advance(64);
    check(a.inspect_lfo().phase==2,"restart policy");
    a.write_register(1,2);a.advance(6400);check(a.inspect_lfo().phase==0,"hold policy");
    a.write_register(1,0);a.write_register(0x1b,2);a.advance(8*64);
    check(a.inspect_lfo().phase==2,"triangle double advance");
    auto b=a;std::vector<double>x,y;a.advance(76543,save,&x);
    for(unsigned n=0;n<76543;++n)b.advance(1,save,&y);
    check(x==y,"copy and chunk invariance");
    Ym2151 tone;tone.write_register(0x20,0x47);tone.write_register(0x28,0x4a);
    tone.write_register(0x40,1);tone.write_register(0x80,0xdf);tone.write_register(8,8);
    tone.write_register(0x1b,1);tone.write_register(1,2);tone.write_register(0x19,255);
    tone.write_register(0x38,0x70);std::vector<double>audio;tone.advance(3579545,save,&audio);
    double first=0,last=0;unsigned crossings=0;
    for(unsigned n=1;n<audio.size();++n)if(audio[n-1]<=0 && audio[n]>0) {
        const double at=n-1-audio[n-1]/(audio[n]-audio[n-1]);
        if(!crossings) first=at;
        last=at;++crossings;
    }
    const double hz=(crossings-1)*tone.native_rate()/(last-first);
    check(std::abs(hz-440*std::exp2(793.75/1200))<.01,"integer PM reaches rendered pitch");
    std::cout<<"PASS: 96 recorded step trains (5689 steps), 128 recorded AM levels, state policy, rendered PM, copy/chunk\n";
} catch(const std::exception& e) { std::cerr<<e.what()<<'\n';return 1; } }
