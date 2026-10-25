// SPDX-License-Identifier: 0BSD
// Copyright (C) 2026 by I.C.KaZe
#include "ym2151.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace independent_opm;
void check(bool condition,const char* message) { if(!condition) throw std::runtime_error(message); }
// Independent published-time checks: do not import the implementation's time table.
void attack(unsigned raw,unsigned kc,double milliseconds) {
    Ym2151 c(3600000);
    c.write_register(0x28,static_cast<std::uint8_t>(kc));
    c.write_register(0x80,static_cast<std::uint8_t>(192|raw));
    c.write_register(8,8);
    unsigned samples=0;
    while(c.inspect(0,0).stage==Envelope::attack && samples<1000000) { c.advance(64); ++samples; }
    double measured=samples*64.0/3600000*1000;
    check(std::abs(measured-milliseconds)<=64.0/3600000*1000+1e-8,"published attack time");
}
int main() {
 try {
    // A complete nonzero orbit checks width, taps and absence of short cycles.
    std::uint32_t state=1;
    check(detail::sequence_step(1)==0x10000u,"LFSR bit0 feeds bit16");
    check(detail::sequence_step(8)==0x10004u,"LFSR bit3 feeds bit16");
    check(detail::sequence_step(9)==4u,"LFSR taps XOR, not OR");
    for(unsigned step=1;step<=131071;++step) {
        state=detail::sequence_step(state);
        check(state>0 && state<=0x1ffffu,"LFSR range/nonzero");
        check((state==1)==(step==131071),"LFSR full period");
    }

    attack(2,0,7723.38);attack(2,12,4354.31);attack(12,0,249.17);
    attack(24,0,3.89);attack(30,0,.51);attack(30,12,0);
    std::cout<<"PASS: sequence orbit and published envelope times\n";
 }catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}
}
