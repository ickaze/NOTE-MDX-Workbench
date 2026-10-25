// SPDX-License-Identifier: 0BSD
// Copyright (C) 2026 by I.C.KaZe
#include "ym2151.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace independent_opm;
void check(bool v){if(!v)throw std::runtime_error("output timing test failed");}
bool same(double a,double b){return std::abs(a-b)<1e-14;}
Ym2151 tone(unsigned ch,unsigned alg){
 Ym2151 x;x.write_register(0x20+ch,static_cast<std::uint8_t>(0xc0|alg));x.write_register(0x28+ch,0x4a);
 for(unsigned op=0;op<4;op++){x.write_register(0x40+op*8+ch,static_cast<std::uint8_t>(op+1));x.write_register(0x80+op*8+ch,31);}
 x.write_register(8,static_cast<std::uint8_t>(0x78|ch));return x;
}
int main(){
 for(unsigned alg=0;alg<8;alg++)for(unsigned ch=0;ch<8;ch++){
  auto dry=tone(ch,alg),delayed=dry,other=dry;delayed.set_output_sample_delays(ch,15,0);other.set_output_sample_delays((ch+1)%8,15,0);
  double old=0;bool audible=false;
  for(unsigned n=0;n<512;n++){
   dry.advance(64);delayed.advance(64);other.advance(64);
   check(same(delayed.last_sample().left,old));check(same(delayed.last_sample().right,dry.last_sample().right));
   check(other.last_sample().left==dry.last_sample().left);audible|=std::abs(dry.last_sample().left)>1e-4;old=dry.last_sample().left;
  }
  check(audible);
 }
 // Verify the measured mapping against independently configured causal masks.
 for(unsigned alg=0;alg<8;alg++)for(unsigned ch=0;ch<8;ch++) {
  auto measured=tone(ch,alg), expected=measured;
  measured.set_measured_output_timing(true);
  if(alg==5 || alg==7) expected.set_output_sample_delays(ch,15,ch==7?9:3);
  for(int n=0;n<1024;n++) {
   measured.advance(64);expected.advance(64);
   check(same(measured.last_sample().left,expected.last_sample().left));
   check(same(measured.last_sample().right,expected.last_sample().right));
  }
  measured.set_measured_alg5_timing(false);measured.advance(64);
  check(same(measured.last_sample().left,measured.last_sample().right));
 }
 auto part=tone(3,5);part.set_output_sample_delays(3,14,2);bool different=false;
 for(int i=0;i<1024;i++){part.advance(64);different|=part.last_sample().left!=part.last_sample().right;}check(different);
 auto copy=part;part.advance(12345);for(int i=0;i<12345;i++)copy.advance(1);
 check(part.last_sample().left==copy.last_sample().left&&part.last_sample().right==copy.last_sample().right);
 part.clear_output_sample_delays();part.advance(64);check(part.last_sample().left==part.last_sample().right);
 part.reset();Ym2151 clean;part.advance(64);clean.advance(64);check(part.last_sample().left==clean.last_sample().left);
 bool threw=false;try{part.set_output_sample_delays(8,0,0);}catch(const std::out_of_range&){threw=true;}check(threw);
 threw=false;try{part.set_output_sample_delays(0,16,0);}catch(const std::out_of_range&){threw=true;}check(threw);
 std::cout<<"PASS: all 8 algorithms x 8 channels; one-frame causal output, channel isolation, partial stereo, copy/chunking/reset/range\n";
}
