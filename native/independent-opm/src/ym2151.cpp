// SPDX-License-Identifier: 0BSD
// Copyright (C) 2026 by I.C.KaZe
#include "ym2151.hpp"
#include "envelope_times.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace independent_opm {
namespace {
constexpr double pi = 3.1415926535897932384626433832795;
constexpr double reference_clock = 3579545.0;
// Yamaha OPM application manual, Fig.2.4. Undefined codes 3/7/11/15
// deliberately alias the preceding note; that alias is NOT hardware-verified.
constexpr int notes[16] = {1,2,3,3,4,5,6,6,7,8,9,9,10,11,12,12};
constexpr double dt2_cents[4] = {0,600,781,950}; // Fig.2.7 (rounded published values)
// Fig.2.6 frequency offsets, transcribed as multiples of clock/2^26.
// Quantization unit inferred from the manual's rounded Hz values, not from a ROM dump.
constexpr unsigned dt1_units[4][32] = {
 {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
 {0,0,0,0,1,1,1,1,1,1,1,1,2,2,2,2,2,3,3,3,4,4,4,5,5,6,6,7,8,8,8,10},
 {1,1,1,1,2,2,2,2,2,3,3,3,4,4,4,5,5,6,6,7,8,8,9,10,11,12,13,14,16,17,19,20},
 {2,2,2,2,2,3,3,3,4,4,4,5,5,6,6,7,8,8,9,10,11,12,13,14,16,17,19,20,22,22,22,22}
};
// Published full attack times replace the fitted octave formula.
// Lower decay rates still use the explicitly documented interpolation.
double duration(unsigned rate, bool attack) {
    if (attack) return specification::attack_ms[rate]/1000.0;
    if (rate < 4) return std::numeric_limits<double>::infinity();
    if (rate >= 52) return specification::fast_decay_ms[rate-52]/1000.0;
    unsigned group=rate/4, fraction=rate%4;
    return std::ldexp(.013452,14-static_cast<int>(group))/(1+.25*fraction);
}
double wave(std::uint32_t phase, double modulation_cycles) {
    // Analytic sine: no third-party lookup table and no claim to chip ROM accuracy.
    return std::sin(2*pi*(phase/4294967296.0 + modulation_cycles));
}
}
Ym2151::Ym2151(std::uint32_t clock_hz) : clock_hz_(clock_hz) {
    if (clock_hz < 100000 || clock_hz > 10000000)
        throw std::invalid_argument("Clock must be between 100000 and 10000000 Hz");
    reset();
}
void Ym2151::reset() {
    for (auto& channel : increment_valid_) channel.fill(false);
    for (auto& channel : amplitude_valid_) channel.fill(false);
    registers_.fill(0); manual_keys_.fill(0);
    for (auto& channel : operators_) for (auto& op : channel) op = Operator{};
    for (auto& f : feedback_) f.fill(0);
    clocks_ = 0; timer_a_ = timer_b_ = busy_ = sample_phase_ = 0;
    address_ = flags_ = amd_ = pmd_ = 0;
    periodic_ = {}; random_ = {}; lfo_info_ = {};
    csm_release_ = false; last_ = {}; channel_samples_ = {};
    clear_output_sample_delays(); previous_outputs_ = {}; measured_alg5_timing_ = false;
}
void Ym2151::set_output_sample_delays(unsigned channel, std::uint8_t left_mask,
                                      std::uint8_t right_mask) {
    if (channel >= 8 || left_mask > 15 || right_mask > 15)
        throw std::out_of_range("Output delay channel/mask out of range");
    left_previous_mask_[channel] = left_mask;
    right_previous_mask_[channel] = right_mask;
}
void Ym2151::clear_output_sample_delays() {
    left_previous_mask_.fill(0); right_previous_mask_.fill(0);
}
unsigned Ym2151::reg(unsigned base, unsigned channel, unsigned slot) const {
    // Register order M1, M2, C1, C2 (manual Fig.2.2), NOT serial algorithm order.
    return registers_[base + slot*8 + channel];
}
unsigned Ym2151::rate(unsigned ch, unsigned slot, unsigned raw) const {
    if (raw == 0) return 0;
    unsigned ks = reg(0x80,ch,slot) >> 6;
    unsigned keycode = (registers_[0x28+ch] & 127) >> 2;
    return std::min(63u, 2*raw + (keycode >> (3-ks)));
}
double Ym2151::frequency(unsigned ch, unsigned slot, double cents) const {
    unsigned kc = registers_[0x28+ch] & 127;
    double semitones = 12.0 + 12*(kc >> 4) + notes[kc & 15]
                    + (registers_[0x30+ch] >> 2)/64.0;
    unsigned dt2 = reg(0xc0,ch,slot) >> 6;
    double f = 440*std::exp2((semitones-69+(dt2_cents[dt2]+cents)/100)/12)
               * clock_hz_/reference_clock;
    unsigned dt1 = (reg(0x40,ch,slot) >> 4) & 7;
    double delta = dt1_units[dt1&3][kc>>2]*clock_hz_/67108864.0;
    f += (dt1 & 4) ? -delta : delta;
    unsigned mul = reg(0x40,ch,slot) & 15;
    return std::max(0.0,f)*(mul ? mul : .5);
}
std::uint32_t Ym2151::period_a() const {
    return 64*(1024-((registers_[0x10]<<2)|(registers_[0x11]&3)));
}
std::uint32_t Ym2151::period_b() const { return 1024*(256-registers_[0x12]); }
void Ym2151::key(unsigned ch, unsigned slot, bool on, bool force) {
    auto& op = operators_[ch][slot];
    if (on && (!op.key || force)) {
        op.phase = 0;
        op.stage = Envelope::attack;
        if (rate(ch,slot,reg(0x80,ch,slot)&31) == 63) {
            op.attenuation = 0; op.stage = Envelope::decay1;
        }
    } else if (!on && op.key && op.stage != Envelope::off) {
        op.stage = Envelope::release;
    }
    op.key = on;
}
void Ym2151::write_register(std::uint8_t a, std::uint8_t v) {
    auto previous = registers_[a];
    registers_[a] = v;
    if (previous != v && ((a >= 0x28 && a <= 0x37) ||
                          (a >= 0x40 && a <= 0x5f) ||
                          (a >= 0xc0 && a <= 0xdf))) {
        for (auto& channel : increment_valid_) channel.fill(false);
    }
    busy_ = 68; // Application manual p.5: write busy duration.
    if (a == 0x08) {
        unsigned ch = v & 7;
        // KON bits 3..6: M1, C1, M2, C2 (Yamaha manual, section 2.1.1).
        // Internal/register slots: M1, M2, C1, C2. Keep manual_keys_ in
        // internal order as well, so CSM release restores the same operators.
        constexpr unsigned key_bit[4] = {3, 5, 4, 6};
        manual_keys_[ch] = 0;
        for (unsigned slot=0; slot<4; ++slot) {
            const bool on = (v & (1u << key_bit[slot])) != 0;
            if (on) manual_keys_[ch] |= static_cast<std::uint8_t>(1u << slot);
            key(ch,slot,on);
        }
    } else if (a == 0x18) {
        periodic_.frequency_written();
        random_.latch_age = 0;
    } else if (a == 0x0f && ((previous ^ v) & 31)) {
        random_.change_nfrq();
    } else if (a == 0x19) {
        if (v & 128) pmd_ = v & 127; else amd_ = v & 127;
    } else if (a == 0x01 && (v & 2)) {
        periodic_.hold_written();
    } else if (a == 0x14) {
        if (v & 16) flags_ &= ~1u;
        if (v & 32) flags_ &= ~2u;
        if (!(v & 1)) timer_a_ = 0;
        else if (!(previous & 1)) timer_a_ = period_a();
        if (!(v & 2)) timer_b_ = 0;
        else if (!(previous & 2)) timer_b_ = period_b();
    }
}
bool Ym2151::write_data(std::uint8_t value) {
    if (busy_) return false;
    write_register(address_, value);
    return true;
}
std::uint8_t Ym2151::status() const { return flags_ | (busy_ ? 128 : 0); }
void Ym2151::envelope(unsigned ch, unsigned slot) {
    auto& op = operators_[ch][slot];
    if (op.stage == Envelope::off) return;
    unsigned raw = 0;
    if (op.stage == Envelope::attack) raw = reg(0x80,ch,slot)&31;
    if (op.stage == Envelope::decay1) raw = reg(0xa0,ch,slot)&31;
    if (op.stage == Envelope::decay2) raw = reg(0xc0,ch,slot)&31;
    if (op.stage == Envelope::release) raw = 2*(reg(0xe0,ch,slot)&15)+1;
    bool attack = op.stage == Envelope::attack;
    double seconds = duration(rate(ch,slot,raw),attack)*3600000.0/clock_hz_;
    double step = 1/native_rate();
    if (attack) {
        // Exponential attenuation curve with finite zero crossing, calibrated to
        // table traversal time. Internal chip EG increment sequences are unknown.
        if (seconds == 0) op.attenuation = 0;
        else op.attenuation = std::max(0.0,(op.attenuation+.09375)
                      *std::exp(-std::log(1025.0)*step/seconds)-.09375);
        if (op.attenuation <= 1e-12) { op.attenuation = 0; op.stage=Envelope::decay1; }
    } else {
        op.attenuation = std::min(96.0,op.attenuation+96*step/seconds);
    }
    if (op.stage == Envelope::decay1) {
        unsigned level = reg(0xe0,ch,slot) >> 4;
        if (op.attenuation >= (level==15 ? 93 : level*3)) op.stage=Envelope::decay2;
    }
    if (op.attenuation >= 96 && op.stage != Envelope::attack) op.stage=Envelope::off;
}
Stereo Ym2151::synthesize() {
    const unsigned shape = registers_[0x1b] & 3;
    periodic_.advance(registers_[0x18],shape,(registers_[1]&2)!=0);
    // Preserve the recording-derived random capture model.
    const int random_am=random_.am;
    const auto values=shape==3
        ? detail::ModulationValues{random_am,random_am>=128 ? 256-random_am : -random_am}
        : detail::periodic_values(shape,periodic_.position);
    const unsigned am_depth=detail::attenuation_units(static_cast<unsigned>(values.am),amd_);
    const int pm_depth=detail::signed_depth(values.pm,pmd_);
    lfo_info_={static_cast<std::uint16_t>(shape==3 ? random_.am : periodic_.position),
               static_cast<std::uint16_t>(values.am),static_cast<std::int16_t>(values.pm),
               double(am_depth),double(pm_depth),shape==3};
    Stereo mix;
    for (unsigned ch=0;ch<8;++ch) {
        unsigned control=registers_[0x20+ch], sensitivity=registers_[0x38+ch];
        // Integer displacement, approximate equal-tempered pitch conversion.
        const double cents=detail::pitch_units(pm_depth,sensitivity>>4)*(100.0/64.0);
        std::array<double,4> out{};
        auto evaluate = [&](unsigned slot,double mod_cycles) {
            auto& op=operators_[ch][slot];
            envelope(ch,slot);
            double attenuation = op.attenuation + .75*(reg(0x60,ch,slot)&127);
            if (reg(0xa0,ch,slot)&128) attenuation += detail::attenuation_db(am_depth,sensitivity);
            double value = 0;
            if (op.stage != Envelope::off) {
                if (ch==7 && slot==3 && (registers_[0x0f]&128))
                    // 43NOISE: noise level is approximately 1/4 of a TL0 sine peak.
                    // Flat-EG normalization only; EG/AM and endpoint quantization remain approximate.
                    value = .25*(random_.state&1 ? -1 : 1)*std::max(0.0,1-attenuation/96);
                else {
                    if (!amplitude_valid_[ch][slot] ||
                        cached_amplitude_attenuation_[ch][slot] != attenuation) {
                        cached_amplitude_[ch][slot] = std::pow(10.0,-attenuation/20);
                        cached_amplitude_attenuation_[ch][slot] = attenuation;
                        amplitude_valid_[ch][slot] = true;
                    }
                    value = wave(op.phase,mod_cycles)*cached_amplitude_[ch][slot];
                }
            }
            if (!increment_valid_[ch][slot] || cached_cents_[ch][slot] != cents) {
                double cycles=frequency(ch,slot,cents)/native_rate();
                auto increment=static_cast<std::uint64_t>(std::llround(cycles*4294967296.0));
                cached_increment_[ch][slot] = static_cast<std::uint32_t>(increment);
                cached_cents_[ch][slot] = cents;
                increment_valid_[ch][slot] = true;
            }
            op.phase += cached_increment_[ch][slot];
            out[slot]=value;
            return value;
        };
        unsigned fb=(control>>3)&7;
        // Fig.2.10: pi/16 .. 4*pi radians, expressed here in cycles.
        double feedback_cycles=fb ? std::ldexp(1.0,static_cast<int>(fb)-6)
                       *(feedback_[ch][0]+feedback_[ch][1])*.5 : 0;
        double m1=evaluate(0,feedback_cycles);
        // Full-scale operator -> 4 phase cycles is an explicit provisional model
        // parameter. Pipeline delays/quantization are not hardware-verified.
        constexpr double depth=4;
        double result=0;
        switch(control&7) {
        case 0: evaluate(2,m1*depth); evaluate(1,out[2]*depth); result=evaluate(3,out[1]*depth); break;
        case 1: evaluate(2,0); evaluate(1,(m1+out[2])*depth); result=evaluate(3,out[1]*depth); break;
        case 2: evaluate(2,0); evaluate(1,out[2]*depth); result=evaluate(3,(m1+out[1])*depth); break;
        case 3: evaluate(2,m1*depth); evaluate(1,0); result=evaluate(3,(out[2]+out[1])*depth); break;
        case 4: evaluate(2,m1*depth); evaluate(1,0); result=out[2]+evaluate(3,out[1]*depth); break;
        case 5: evaluate(2,m1*depth); evaluate(1,m1*depth); result=out[2]+out[1]+evaluate(3,m1*depth); break;
        case 6: evaluate(2,m1*depth); evaluate(1,0); result=out[2]+out[1]+evaluate(3,0); break;
        case 7: evaluate(2,0); evaluate(1,0); result=m1+out[2]+out[1]+evaluate(3,0); break;
        }
        feedback_[ch][1]=feedback_[ch][0]; feedback_[ch][0]=m1;
        // Host normalization; analog DAC voltage and clipping are not modeled.
        double left_result = result, right_result = result;
        auto left_mask = left_previous_mask_[ch], right_mask = right_previous_mask_[ch];
        if (measured_alg5_timing_ && ((control&7) == 5 || (control&7) == 7) && !(left_mask || right_mask)) {
            // 4 MHz recordings: C1/C2 lead M2 by a relative native frame on
            // channels 0..6; channel 7 instead groups M2/C1 ahead of C2.
            // Use only current/past samples; this is output framing, not a
            // reconstruction of the internal operator execution pipeline.
            left_mask = 0x0f;
            right_mask = ch == 7 ? 0x09 : 0x03;
        }
        if (left_mask || right_mask) {
            // Register slot masks for audible outputs of each algorithm.
            constexpr unsigned carrier_mask[8] = {8,8,8,8,12,14,14,15};
            left_result = right_result = 0;
            for (unsigned slot=0; slot<4; ++slot) {
                if (!(carrier_mask[control&7] & (1u<<slot))) continue;
                const double old = previous_outputs_[ch][slot];
                left_result += (left_mask & (1u<<slot)) ? old : out[slot];
                right_result += (right_mask & (1u<<slot)) ? old : out[slot];
            }
        }
        previous_outputs_[ch] = out;
        channel_samples_[ch] = {left_result/8, right_result/8};
        if(control&64) mix.left += left_result/8;
        if(control&128) mix.right += right_result/8;
    }
    return mix;
}
void Ym2151::advance(std::uint64_t count, Sink sink, void* context) {
    if (count > std::numeric_limits<std::uint64_t>::max()-clocks_)
        throw std::overflow_error("Clock counter overflow");
    while(count) {
        auto n=static_cast<unsigned>(std::min<std::uint64_t>(count,64-sample_phase_));
        if(timer_a_) n=std::min(n,timer_a_);
        if(timer_b_) n=std::min(n,timer_b_);
        random_.advance(n, registers_[0x0f], registers_[0x18], (registers_[1] & 2) != 0);
        clocks_+=n; count-=n; sample_phase_+=n;
        busy_=busy_>n ? busy_-n : 0;
        if(timer_a_) {
            timer_a_-=n;
            if(!timer_a_) {
                timer_a_=period_a();
                if(registers_[0x14]&4) flags_|=1;
                if(registers_[0x14]&128) {
                    for(unsigned ch=0;ch<8;++ch) for(unsigned slot=0;slot<4;++slot)
                        key(ch,slot,true,true);
                    csm_release_=true;
                }
            }
        }
        if(timer_b_) {
            timer_b_-=n;
            if(!timer_b_) { timer_b_=period_b(); if(registers_[0x14]&8) flags_|=2; }
        }
        if(sample_phase_==64) {
            sample_phase_=0;
            last_=synthesize();
            if(sink) sink(context,last_);
            if(csm_release_) {
                // Approximate one-native-sample CSM gate; manual keys preserved below.
                for(unsigned ch=0;ch<8;++ch) for(unsigned slot=0;slot<4;++slot)
                    key(ch,slot,(manual_keys_[ch] & (1u << slot)) != 0);
                csm_release_=false;
            }
        }
    }
}
OperatorInfo Ym2151::inspect(unsigned ch, unsigned slot) const {
    if(ch>=8 || slot>=4) throw std::out_of_range("Channel/slot out of range");
    const auto& op=operators_[ch][slot];
    return {op.attenuation,frequency(ch,slot,0),op.stage,op.key};
}
Resampler::Resampler(double native_rate,std::uint32_t output_rate) {
    if(!std::isfinite(native_rate) || native_rate<=0 || output_rate<8000 || output_rate>192000)
        throw std::invalid_argument("Invalid resampling rate (output: 8000..192000 Hz)");
    double cutoff=.90*std::min(1.0,output_rate/native_rate);
    for(unsigned p=0;p<phases;++p) {
        double sum=0;
        for(unsigned i=0;i<taps;++i) {
            double x=i+p/static_cast<double>(phases)-16;
            double window=std::abs(x)<=16 ? .42+.5*std::cos(pi*x/16)+.08*std::cos(2*pi*x/16) : 0;
            double sinc=std::abs(x)<1e-12 ? cutoff : std::sin(pi*cutoff*x)/(pi*x);
            weights_[p][i]=sinc*window; sum+=weights_[p][i];
        }
        for(auto& v:weights_[p]) v/=sum;
    }
}
void Resampler::push(Stereo f) { head_=(head_+taps-1)%taps; history_[head_]=f; }
Stereo Resampler::output(double fraction) const {
    if(!std::isfinite(fraction) || fraction<0 || fraction>=1)
        throw std::invalid_argument("Native fraction must be in [0,1)");
    unsigned p=std::min(phases-1,static_cast<unsigned>(fraction*phases));
    Stereo out;
    unsigned index = head_;
    for(unsigned i=0;i<taps;++i) {
        auto f=history_[index];
        out.left+=f.left*weights_[p][i]; out.right+=f.right*weights_[p][i];
        if (++index == taps) index = 0;
    }
    return out;
}
}

// Explicit, versioned serialization: no object padding, addresses or callbacks.
#include "state_codec.hpp"
#include <memory>
namespace independent_opm {
template<class Archive> void Ym2151::state_fields(Archive& a) {
    a(clock_hz_,registers_);
    for(auto& ch:operators_) for(auto& o:ch)a(o.phase,o.attenuation,o.stage,o.key);
    a(feedback_,manual_keys_,clocks_,timer_a_,timer_b_,busy_,sample_phase_,address_,flags_,amd_,pmd_);
    a(periodic_.wait_samples,periodic_.order,periodic_.position);
    a(random_.state,random_.age,random_.latch_age,random_.am);
    a(lfo_info_.phase,lfo_info_.am,lfo_info_.pm,lfo_info_.am_after_depth,lfo_info_.pm_after_depth,lfo_info_.uses_unverified_random);
    a(csm_release_,last_.left,last_.right,left_previous_mask_,right_previous_mask_,measured_alg5_timing_,previous_outputs_);
}
std::vector<std::uint8_t> Ym2151::save_state() const {
    auto copy=*this;state_detail::Archive a;copy.state_fields(a);return a.finish(1);
}
bool Ym2151::load_state(const void* data,std::size_t size) {
    try {
        state_detail::Archive a(data,size,1);auto c=*this;c.state_fields(a);a.end();
        if(c.clock_hz_<100000||c.clock_hz_>10000000||c.sample_phase_>=64||c.busy_>68||c.flags_>3||c.amd_>127||c.pmd_>127) return false;
        if(c.periodic_.order>15||c.periodic_.position>511||c.periodic_.wait_samples>262144) return false;
        if(c.random_.state>0x1ffff||c.random_.age>=32u*(32u-(c.registers_[15]&31))||c.random_.latch_age>=detail::MeasuredRandom::period(c.registers_[24])) return false;
        for(const auto& ch:c.operators_)for(const auto& o:ch)if(static_cast<unsigned>(o.stage)>4)return false;
        for(unsigned i=0;i<8;++i)if(c.manual_keys_[i]>15||c.left_previous_mask_[i]>15||c.right_previous_mask_[i]>15)return false;
        for (auto& channel : c.increment_valid_) channel.fill(false);
        for (auto& channel : c.amplitude_valid_) channel.fill(false);
        c.channel_samples_={};*this=c;return true;
    } catch(const state_detail::Invalid&) {return false;}
}
template<class Archive> void Resampler::state_fields(Archive& a) {
    for(auto& s:history_)a(s.left,s.right);
    a(weights_,head_);
}
std::vector<std::uint8_t> Resampler::save_state() const {
    auto c=std::make_unique<Resampler>(*this);state_detail::Archive a;c->state_fields(a);return a.finish(2);
}
bool Resampler::load_state(const void* data,std::size_t size) {
    try {
        state_detail::Archive a(data,size,2);auto c=std::make_unique<Resampler>(*this);c->state_fields(a);a.end();
        if(c->head_>=taps)return false;
        *this=*c;return true;
    } catch(const state_detail::Invalid&) {return false;}
}
}
