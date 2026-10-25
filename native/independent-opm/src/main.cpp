// SPDX-License-Identifier: 0BSD
// Copyright (C) 2026 by I.C.KaZe
#include "ym2151.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <stdexcept>
using namespace independent_opm;
struct Event { std::uint64_t clock; unsigned address, value; };
unsigned number(const std::string& s) {
    std::size_t end=0; auto n=std::stoul(s,&end,0);
    if(end!=s.size() || n>0xffffffffUL) throw std::runtime_error("Invalid number: "+s);
    return static_cast<unsigned>(n);
}
std::vector<Event> read_trace(const std::string& path) {
    std::ifstream f(path); if(!f) throw std::runtime_error("Cannot open trace");
    std::vector<Event> events; std::string line; unsigned lineno=0;
    while(std::getline(f,line)) {
        ++lineno; line=line.substr(0,line.find('#'));
        std::istringstream in(line); std::string a,b,c,extra;
        if(!(in>>a)) continue;
        if(!(in>>b>>c) || (in>>extra)) throw std::runtime_error("Bad trace line "+std::to_string(lineno));
        // Deliberately cap time to 32-bit clocks; long traces can use API directly.
        Event e{number(a),number(b),number(c)};
        if(e.address>255 || e.value>255 || (!events.empty() && e.clock<events.back().clock))
            throw std::runtime_error("Invalid/unsorted event at line "+std::to_string(lineno));
        events.push_back(e);
    }
    return events;
}
std::vector<Event> demo(unsigned hz) {
    std::vector<Event> e;
    auto w=[&](double t,unsigned a,unsigned v){e.push_back({static_cast<std::uint64_t>(t*hz),a,v});};
    const unsigned note_code[12]={14,0,1,2,4,5,6,8,9,10,12,13};
    for(unsigned ch=0;ch<8;++ch) {
        w(0,0x20+ch,(ch%2 ? 128u:64u)|ch); // eight algorithms
        for(unsigned slot=0;slot<4;++slot) {
            unsigned offset=slot*8+ch;
            w(0,0x40+offset,slot==0 ? 2 : 1);
            w(0,0x60+offset,slot==0 ? 32 : 16);
            w(0,0x80+offset,0xdf); // KS=3, instantaneous maximum attack
            w(0,0xa0+offset,12);
            w(0,0xc0+offset,5);
            w(0,0xe0+offset,0x5b);
        }
    }
    const unsigned melody[16]={60,64,67,72,71,67,64,62,60,63,67,70,69,65,62,60};
    for(unsigned i=0;i<16;++i) {
        unsigned ch=i%8, midi=melody[i], octave=midi/12-1;
        if(midi%12==0) --octave;
        double t=.1+i*.32;
        w(t,0x28+ch,(octave<<4)|note_code[midi%12]);
        w(t,8,0x78|ch); w(t+.25,8,ch);
    }
    std::stable_sort(e.begin(),e.end(),[](auto a,auto b){return a.clock<b.clock;});
    return e;
}
void u16(std::ostream& f,unsigned v) { f.put(static_cast<char>(v)); f.put(static_cast<char>(v>>8)); }
void u32(std::ostream& f,std::uint32_t v) { u16(f,v&65535); u16(f,v>>16); }
int main(int argc,char** argv) {
    try {
        if(argc<3) {
            std::cout<<"Independent OPM 0.14 experimental - functional approximation, not silicon-exact\n"
              "opm_render --demo output.wav [seconds=6] [clock=3579545] [rate=48000] [gain=1] [timing_profile.txt]\n"
              "opm_render trace.txt output.wav [seconds=6] [clock=3579545] [rate=48000] [gain=1] [timing_profile.txt]\n"
              "Use --gmc-opt04-4mhz in place of timing_profile.txt for measured ALG5/ALG7 capture timing (4MHz/96kHz).\n"
              "Trace: absolute_clock address value (decimal or 0x hex), # comments\n";
            return argc==1 ? 0 : 1;
        }
        double seconds=argc>3 ? std::stod(argv[3]):6;
        unsigned clock=argc>4 ? number(argv[4]):3579545;
        unsigned rate=argc>5 ? number(argv[5]):48000;
        double gain=argc>6 ? std::stod(argv[6]):1;
        if(argc>8 || !std::isfinite(seconds) || seconds<=0 || seconds>600 ||
           !std::isfinite(gain) || gain<0 || gain>32)
            throw std::runtime_error("Invalid duration/gain (0..600 seconds, gain 0..32)");
        Ym2151 chip(clock); Resampler filter(chip.native_rate(),rate);
        const bool measured = argc>7 && std::string(argv[7])=="--gmc-opt04-4mhz";
        if (measured) {
            if (clock != 4000000 || rate != 96000)
                throw std::runtime_error("Measured capture profile requires clock=4000000 and rate=96000");
            chip.set_measured_output_timing(true);
        }
        if (argc>7 && !measured) {
            // Profile rows: channel left_previous_mask right_previous_mask.
            // Reuse the strict three-number parser; channel rows must be sorted.
            for (const auto& row : read_trace(argv[7])) {
                if (row.clock>=8 || row.address>15 || row.value>15)
                    throw std::runtime_error("Invalid output timing profile row");
                chip.set_output_sample_delays(static_cast<unsigned>(row.clock),
                    static_cast<std::uint8_t>(row.address),static_cast<std::uint8_t>(row.value));
            }
        }
        auto events=std::string(argv[1])=="--demo" ? demo(clock):read_trace(argv[1]);
        auto frames=static_cast<std::uint32_t>(seconds*rate);
        if(!events.empty() && events.back().clock>static_cast<std::uint64_t>(seconds*clock))
            throw std::runtime_error("Trace extends beyond requested duration");
        std::ofstream out(argv[2],std::ios::binary);
        if(!out) throw std::runtime_error("Cannot open output WAV");
        out.write("RIFF",4); u32(out,36+frames*4); out.write("WAVEfmt ",8);
        u32(out,16); u16(out,1); u16(out,2); u32(out,rate); u32(out,rate*4);
        u16(out,4); u16(out,16); out.write("data",4); u32(out,frames*4);
        std::size_t event=0; std::uint64_t remainder=0, target=0, clipped=0;
        double previous_right = 0;
        for(std::uint32_t i=0;i<frames;++i) {
            remainder+=clock; target+=remainder/rate; remainder%=rate;
            while(event<events.size() && events[event].clock<=target) {
                const auto& e=events[event++];
                chip.advance(e.clock-chip.clock_count(),Resampler::receive,&filter);
                chip.write_register(static_cast<std::uint8_t>(e.address),static_cast<std::uint8_t>(e.value));
            }
            chip.advance(target-chip.clock_count(),Resampler::receive,&filter);
            auto f=filter.output(chip.sample_fraction());
            if (measured) { // Common measured capture-path offset: one 96 kHz frame.
                const double current = f.right; f.right = previous_right; previous_right = current;
            }
            for(double sample:{f.left,f.right}) {
                sample*=gain; if(sample<-1 || sample>1) ++clipped;
                int value=static_cast<int>(std::lrint(std::clamp(sample,-1.0,1.0)*32767));
                u16(out,static_cast<unsigned>(value)&65535);
            }
        }
        out.close(); if(!out) throw std::runtime_error("Failed writing WAV");
        std::cout<<frames<<" stereo frames, "<<rate<<" Hz, clipped samples: "<<clipped<<"\n";
        return 0;
    } catch(const std::exception& ex) { std::cerr<<"Error: "<<ex.what()<<"\n"; return 1; }
}
