// SPDX-License-Identifier: 0BSD
// Copyright (C) 2026 by I.C.KaZe
#include "NdsifPreview.h"
#include <stdexcept>
#include <iostream>
#include <thread>
static void check(bool ok){if(!ok)throw std::runtime_error("shared NDSIF mixer regression");}
int main(){
    NdsifPreview mix; std::vector<float> tone(1000,.25f);
    check(!mix.play(tone,15625)); mix.enable(true);
    auto a=mix.play(tone,15625); auto b=mix.play(tone,31250); check(a&&b);
    int16_t music[200];std::fill_n(music,200,1000);mix.mix(music,100);
    check(music[0]==17384&&music[1]==17384&&a->position==0);
    mix.consumed(75); check(a->position==75&&b->position==150);
    mix.beginStream();std::fill_n(music,200,0);mix.mix(music,100);mix.consumed(100);
    check(a->position==175&&b->position==350);
    a->stopped=true;std::fill_n(music,200,0);mix.mix(music,100);check(music[0]==8192);
    auto loud=mix.play(std::vector<float>(1000,8.f),15625);std::fill_n(music,200,30000);mix.mix(music,100);check(music[0]==32767);
    mix.enable(false);check(loud->stopped&&!mix.play(tone,15625));
    mix.enable(true);
    std::thread notes([&]{for(int i=0;i<200;++i){auto v=mix.play(tone,48000);if(v)v->stopped=true;}});
    for(int i=0;i<200;++i){std::fill_n(music,200,0);mix.mix(music,100);mix.consumed((i+1)*100);}
    notes.join();mix.enable(false);
    std::cout<<"shared music/preview sum, device position, stop/seek continuation, clipping, detach and concurrent notes: passed\n";
}
