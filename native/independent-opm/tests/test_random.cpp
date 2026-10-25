// SPDX-License-Identifier: 0BSD
// Copyright (C) 2026 by I.C.KaZe
#include "ym2151.hpp"
#include <array>
#include <iostream>
#include <stdexcept>
using namespace independent_opm;
void require(bool b, const char* s) { if (!b) throw std::runtime_error(s); }
int main() {
 try {
    // Independent recurrence array: output time increases along the array.
    constexpr int P=131071;
    std::array<unsigned char,P> seq{}; seq[0]=1;
    for (int i=17;i<P;++i) seq[i]=seq[i-14]^seq[i-17];
    for (unsigned nf : {0u,1u,7u,15u,23u,30u,31u}) {
        detail::MeasuredRandom r;
        const unsigned divider=32*(32-nf);
        unsigned long long time=0;
        for (unsigned i=0;i<2048;++i) {
            r.advance(65536,static_cast<std::uint8_t>(nf),0x80,false); time+=65536;
            unsigned code=0;
            for (int j=0;j<8;++j) {
                auto index=static_cast<long long>(16*((time-4*j)/divider))-7-j+(j>=6?16:0);
                index=(index%P+P)%P;
                code=(code<<1)|seq[static_cast<std::size_t>(index)];
            }
            require(r.am==(code^255),"independent burst/capture reference");
        }
    }
    detail::MeasuredRandom a,b;
    a.advance(100003,31,0xff,false);
    for(unsigned i=0;i<100003;++i)b.advance(1,31,0xff,false);
    require(a.state==b.state && a.am==b.am && a.age==b.age && a.latch_age==b.latch_age,"chunk invariance");
    detail::MeasuredRandom held,free;
    held.advance(100003,0,0xa0,true); free.advance(100003,0,0xa0,false);
    require(held.state==free.state && held.am==0 && held.latch_age==0,"hold does not freeze source");
    for(unsigned h=0;h<16;++h)require(detail::MeasuredRandom::period(h<<4)==(16777216u>>h),"coarse cadence");
    // Full measured-long length after initial 17 output bits, recurrence at NFRQ=0.
    detail::MeasuredRandom long_run; std::array<unsigned char,17> history{};
    for(unsigned i=0;i<161446;++i){
        long_run.advance(16384,0,0xa0,false);
        const auto bit=static_cast<unsigned char>(!(long_run.am&128));
        if(i>=17)require(bit==(history[(i-14)%17]^history[i%17]),"long MSB recurrence");
        history[i%17]=bit;
    }
    Ym2151 c(4000000),d(4000000);
    c.write_register(0x1b,3);d.write_register(0x1b,3);
    c.write_register(0x18,0xff);d.write_register(0x18,0xff);
    c.advance(211);d.advance(211);c.write_register(0x0f,0x80);
    c.advance(99999);d.advance(99999);
    require(c.inspect_lfo().am==d.inspect_lfo().am,"noise enable must not reset source");
    std::cout<<"PASS: 14336 independent capture vectors, divider/chunk/hold checks, 161446-bit recurrence\n";
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
