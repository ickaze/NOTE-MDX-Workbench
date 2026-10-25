// SPDX-License-Identifier: 0BSD
// Copyright (C) 2026 by I.C.KaZe
#include "independent_opm.h"
#include "ym2151.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <new>
#include <cstddef>
using independent_opm::Ym2151;
using independent_opm::Resampler;
struct iopm_instance {
    Ym2151 chip;
    Resampler filter;
    std::array<Resampler,8> channel_filters;
    uint32_t rate;
    uint64_t remainder = 0;
    iopm_instance(uint32_t clock, uint32_t output_rate)
        : chip(clock), filter(clock/64.0,output_rate),
          channel_filters{Resampler(clock/64.0,output_rate),Resampler(clock/64.0,output_rate),
          Resampler(clock/64.0,output_rate),Resampler(clock/64.0,output_rate),
          Resampler(clock/64.0,output_rate),Resampler(clock/64.0,output_rate),
          Resampler(clock/64.0,output_rate),Resampler(clock/64.0,output_rate)}, rate(output_rate) {}
    static void receive(void* context, independent_opm::Stereo frame) {
        auto& h=*static_cast<iopm_instance*>(context);
        h.filter.push(frame);
        const auto& channels=h.chip.last_channel_samples();
        for(unsigned ch=0;ch<8;++ch) h.channel_filters[ch].push(channels[ch]);
    }
};
static_assert(sizeof(iopm_event)==12 && offsetof(iopm_event,value)==8,"C ABI layout");
template<class F> static int32_t guard(F&& f) noexcept {
    try { f(); return IOPM_OK; }
    catch(const std::bad_alloc&) { return IOPM_OUT_OF_MEMORY; }
    catch(...) { return IOPM_INTERNAL_ERROR; }
}
uint32_t IOPM_CALL iopm_abi_version(void) { return IOPM_ABI_VERSION; }
const char* IOPM_CALL iopm_version(void) { return "0.14 / C ABI 1"; }
int32_t IOPM_CALL iopm_create(uint32_t clock,uint32_t rate,iopm_handle* out) {
    if(!out) return IOPM_INVALID_ARGUMENT;
    *out=nullptr;
    if(clock<100000 || clock>10000000 || rate<8000 || rate>192000) return IOPM_INVALID_ARGUMENT;
    return guard([&]{*out=new iopm_instance(clock,rate);});
}
void IOPM_CALL iopm_destroy(iopm_handle h) { delete h; }
int32_t IOPM_CALL iopm_clone(iopm_handle h,iopm_handle* out) {
    if(!out) return IOPM_INVALID_ARGUMENT;
    *out=nullptr;
    if(!h) return IOPM_INVALID_ARGUMENT;
    return guard([&]{*out=new iopm_instance(*h);});
}
int32_t IOPM_CALL iopm_reset(iopm_handle h) {
    if(!h) return IOPM_INVALID_ARGUMENT;
    // Construct first: allocation failure leaves original state untouched.
    return guard([&]{auto fresh=std::make_unique<iopm_instance>(h->chip.clock_hz(),h->rate);*h=*fresh;});
}
int32_t IOPM_CALL iopm_write_register(iopm_handle h,uint32_t a,uint32_t v) {
    if(!h||a>255||v>255) return IOPM_INVALID_ARGUMENT;
    return guard([&]{h->chip.write_register(static_cast<uint8_t>(a),static_cast<uint8_t>(v));});
}
int32_t IOPM_CALL iopm_read_status(iopm_handle h,uint32_t* status,uint32_t* irq) {
    if(!h||!status||!irq) return IOPM_INVALID_ARGUMENT;
    *status=h->chip.status();*irq=h->chip.irq()?1u:0u;return IOPM_OK;
}
int32_t IOPM_CALL iopm_clock_count(iopm_handle h,uint64_t* clocks) {
    if(!h||!clocks) return IOPM_INVALID_ARGUMENT;
    *clocks=h->chip.clock_count();return IOPM_OK;
}
int32_t IOPM_CALL iopm_set_measured_output_timing(iopm_handle h,uint32_t on) {
    if(!h||on>1) return IOPM_INVALID_ARGUMENT;
    h->chip.set_measured_output_timing(on!=0);return IOPM_OK;
}
int32_t IOPM_CALL iopm_set_output_delays(iopm_handle h,uint32_t ch,uint32_t l,uint32_t r) {
    if(!h||ch>7||l>15||r>15) return IOPM_INVALID_ARGUMENT;
    h->chip.set_output_sample_delays(ch,static_cast<uint8_t>(l),static_cast<uint8_t>(r));return IOPM_OK;
}
template<class Sample> static int32_t render(iopm_handle h,Sample* out,uint32_t frames,const iopm_event* events,uint32_t count,float* channels=nullptr) {
    if(!h||(!out&&frames)||(!events&&count)||frames>std::numeric_limits<size_t>::max()/2/sizeof(Sample)) return IOPM_INVALID_ARGUMENT;
    if(channels && frames>std::numeric_limits<size_t>::max()/16/sizeof(float)) return IOPM_INVALID_ARGUMENT;
    const uint64_t span=(h->remainder+uint64_t(frames)*h->chip.clock_hz())/h->rate;
    const uint64_t start=h->chip.clock_count();
    if(span>std::numeric_limits<uint64_t>::max()-start) return IOPM_INVALID_ARGUMENT;
    for(uint32_t i=0;i<count;++i)
        if(events[i].address>255||events[i].value>255||events[i].clock_offset>span||(i&&events[i].clock_offset<events[i-1].clock_offset)) return IOPM_INVALID_ARGUMENT;
    return guard([&]{
        uint32_t e=0;
        auto advance=[&](uint64_t target) {
            h->chip.advance(target-h->chip.clock_count(),iopm_instance::receive,h);
        };
        auto write_until=[&](uint64_t target) {
            while(e<count && start+events[e].clock_offset<=target) {
                advance(start+events[e].clock_offset);
                h->chip.write_register(static_cast<uint8_t>(events[e].address),static_cast<uint8_t>(events[e].value));++e;
            }
        };
        write_until(start);
        for(uint32_t i=0;i<frames;++i) {
            h->remainder+=h->chip.clock_hz();
            const uint64_t target=h->chip.clock_count()+h->remainder/h->rate;
            h->remainder%=h->rate;
            write_until(target);advance(target);
            const auto s=h->filter.output(h->chip.sample_fraction());
            if(channels) for(unsigned ch=0;ch<8;++ch) {
                const auto c=h->channel_filters[ch].output(h->chip.sample_fraction());
                channels[16*size_t(i)+2*ch]=static_cast<float>(c.left);
                channels[16*size_t(i)+2*ch+1]=static_cast<float>(c.right);
            }
            if constexpr(sizeof(Sample)==sizeof(float)) {out[2*size_t(i)]=static_cast<Sample>(s.left);out[2*size_t(i)+1]=static_cast<Sample>(s.right);}
            else {
                auto cv=[](double v){return static_cast<int16_t>(std::lround(std::clamp(v,-1.0,32767.0/32768.0)*32768.0));};
                out[2*size_t(i)]=cv(s.left);out[2*size_t(i)+1]=cv(s.right);
            }
        }
    });
}
int32_t IOPM_CALL iopm_render_f32(iopm_handle h,float* out,uint32_t n) {return render(h,out,n,nullptr,0);}
int32_t IOPM_CALL iopm_render_s16(iopm_handle h,int16_t* out,uint32_t n) {return render(h,out,n,nullptr,0);}
int32_t IOPM_CALL iopm_render_events_f32(iopm_handle h,float* out,uint32_t n,const iopm_event* ev,uint32_t count) {return render(h,out,n,ev,count);}

int32_t IOPM_CALL iopm_render_channels_f32(iopm_handle h,float* out,float* channels,uint32_t n) {
    if(!channels && n) return IOPM_INVALID_ARGUMENT;
    return render(h,out,n,nullptr,0,channels);
}
int32_t IOPM_CALL iopm_render_events_channels_f32(iopm_handle h,float* out,float* channels,uint32_t n,const iopm_event* ev,uint32_t count) {
    if(!channels && n) return IOPM_INVALID_ARGUMENT;
    return render(h,out,n,ev,count,channels);
}

#include "state_codec.hpp"
static std::vector<std::uint8_t> instance_state(iopm_handle h) {
    independent_opm::state_detail::Archive a;
    auto core=h->chip.save_state(),filter=h->filter.save_state();
    a(h->rate,h->remainder);a.blob(core);a.blob(filter);
    for(auto& f:h->channel_filters) {auto b=f.save_state();a.blob(b);}
    return a.finish(3,2);
}
int32_t IOPM_CALL iopm_state_size(iopm_handle h,uint32_t* size) {
    if(!h||!size)return IOPM_INVALID_ARGUMENT;
    return guard([&]{*size=static_cast<uint32_t>(instance_state(h).size());});
}
int32_t IOPM_CALL iopm_save_state(iopm_handle h,void* buffer,uint32_t capacity,uint32_t* written) {
    if(!h||!written||(!buffer&&capacity))return IOPM_INVALID_ARGUMENT;
    int32_t result=IOPM_OK;
    const auto status=guard([&]{auto bytes=instance_state(h);*written=static_cast<uint32_t>(bytes.size());
        if(!buffer&&capacity==0)return;
        if(capacity<bytes.size()){result=IOPM_BUFFER_TOO_SMALL;return;}
        std::memcpy(buffer,bytes.data(),bytes.size());
    });return status==IOPM_OK?result:status;
}
int32_t IOPM_CALL iopm_load_state(iopm_handle h,const void* buffer,uint32_t size) {
    if(!h||!buffer)return IOPM_INVALID_ARGUMENT;
    int32_t result=IOPM_OK;
    const auto status=guard([&]{try {
        independent_opm::state_detail::Archive a(buffer,size,3,2);
        auto next=std::make_unique<iopm_instance>(*h);
        a(next->rate,next->remainder);std::vector<std::uint8_t> core,filter;a.blob(core);a.blob(filter);
        for(auto& f:next->channel_filters) {std::vector<std::uint8_t> b;a.blob(b);
            if(!f.load_state(b.data(),b.size())) {result=IOPM_INVALID_STATE;return;}}
        a.end();
        if(next->rate<8000||next->rate>192000||next->remainder>=next->rate||!next->chip.load_state(core.data(),core.size())||!next->filter.load_state(filter.data(),filter.size())) {
            result=IOPM_INVALID_STATE;return;
        }
        *h=*next;
    }catch(const independent_opm::state_detail::Invalid&){result=IOPM_INVALID_STATE;}});
    return status==IOPM_OK?result:status;
}
