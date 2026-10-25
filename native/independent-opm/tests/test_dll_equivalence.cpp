// SPDX-License-Identifier: 0BSD
// Copyright (C) 2026 by I.C.KaZe
#include "independent_opm.h"
#include "ym2151.hpp"
#include "voice_regression.hpp"
#include <cstdio>
#include <cmath>
#include <memory>
#include <vector>
#define CHECK(x) do {if(!(x)){std::fprintf(stderr,"line %d: %s\n",__LINE__,#x);return 1;}}while(0)
int main() {
    using namespace independent_opm;
    auto direct=std::make_unique<Ym2151>(4000000);
    auto filter=std::make_unique<Resampler>(62500.0,48000);
    iopm_handle h=nullptr;
    CHECK(iopm_create(4000000,48000,&h)==0);
    const iopm_event events[]={ {0,0x20,0xc7},{0,0x28,0x48},{0,0x58,1},{0,0x98,31},{0,0xf8,15},{0,8,0x40},
        {64,0x28,0x50},{513,0x28,0x48},{1001,0x19,63},{2000,0x18,0xff} };
    direct->set_measured_output_timing(true);
    CHECK(iopm_set_measured_output_timing(h,1)==0);
    std::vector<float> pcm(4096*2);
    CHECK(iopm_render_events_f32(h,pcm.data(),4096,events,10)==0);
    uint64_t fraction=0;unsigned e=0;
    for(unsigned i=0;i<4096;++i) {
        fraction+=4000000;const auto target=direct->clock_count()+fraction/48000;fraction%=48000;
        while(e<10 && events[e].clock_offset<=target) {
            direct->advance(events[e].clock_offset-direct->clock_count(),Resampler::receive,filter.get());
            direct->write_register(static_cast<uint8_t>(events[e].address),static_cast<uint8_t>(events[e].value));++e;
        }
        direct->advance(target-direct->clock_count(),Resampler::receive,filter.get());
        const auto s=filter->output(direct->sample_fraction());
        CHECK(pcm[i*2]==static_cast<float>(s.left) && pcm[i*2+1]==static_cast<float>(s.right));
    }
    uint32_t status,irq;CHECK(iopm_read_status(h,&status,&irq)==0);
    CHECK(status==direct->status() && irq==static_cast<uint32_t>(direct->irq()));
    for(unsigned v=0;v<3;++v) {
        CHECK(iopm_reset(h)==0);
        int error=0;
        regression::load([&](unsigned a,unsigned d){error |= iopm_write_register(h,a,d);},v);
        CHECK(error==0);
        std::vector<float> voice_pcm(48000*2);
        CHECK(iopm_render_f32(h,voice_pcm.data(),48000)==0);
        double energy=0;
        for(float sample:voice_pcm) energy+=sample*sample;
        CHECK(std::sqrt(energy/voice_pcm.size())>0.001);
    }
    iopm_destroy(h);std::puts("DLL vs direct core: 4096 frames identical");
}
