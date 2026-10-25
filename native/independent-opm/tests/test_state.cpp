// SPDX-License-Identifier: 0BSD
// Copyright (C) 2026 by I.C.KaZe
#include "ym2151.hpp"
#include "independent_opm.h"
#include "voice_regression.hpp"
#include <vector>
#include <cstdio>
#include <stdexcept>
using namespace independent_opm;
void check(bool b){if(!b)throw std::runtime_error("state test failed");}
int main(){try{
 for(unsigned shape=0;shape<4;++shape){
  Ym2151 c(4000000);regression::load([&](unsigned a,unsigned v){c.write_register(a,v);},0);
  c.set_measured_output_timing(true);c.set_output_sample_delays(0,3,12);
  c.write_register(0x1b,shape);c.write_register(0x18,255);c.write_register(0x19,127);c.write_register(0x19,255);c.write_register(0x38,0x73);c.write_register(0xa0,128);
  c.write_register(0x10,251);c.write_register(0x11,2);c.write_register(0x12,254);c.write_register(0x14,143);
  c.advance(19217);c.write_address(0x60);c.write_register(0x60,22);c.advance(7);
  auto bytes=c.save_state();Ym2151 restored;check(restored.load_state(bytes.data(),bytes.size()));check(restored.save_state()==bytes);
  check(c.write_data(14)==restored.write_data(14));
  for(unsigned i=0;i<5000;++i){
   if(i==123){c.write_register(8,0);restored.write_register(8,0);}
   c.advance(37);restored.advance(37);
   check(c.last_sample().left==restored.last_sample().left&&c.last_sample().right==restored.last_sample().right);
   check(c.status()==restored.status());
  }
  check(c.save_state()==restored.save_state());
  auto unchanged=restored.save_state();bytes.back()^=1;
  check(!restored.load_state(bytes.data(),bytes.size()));check(restored.save_state()==unchanged);
  bytes=c.save_state();bytes[8]=2;check(!restored.load_state(bytes.data(),bytes.size()));
  check(restored.save_state()==unchanged);
  check(!restored.load_state(bytes.data(),bytes.size()-1));check(!restored.load_state(nullptr,0));
 }
 iopm_handle a=nullptr,b=nullptr;check(iopm_create(4000000,44100,&a)==0);check(iopm_create(3579545,96000,&b)==0);
 regression::load([&](unsigned r,unsigned v){check(iopm_write_register(a,r,v)==0);},2);
 check(iopm_set_measured_output_timing(a,1)==0);std::vector<float> warm(246),x(8192),y(8192);check(iopm_render_f32(a,warm.data(),123)==0);
 uint32_t size=0,written=0;check(iopm_state_size(a,&size)==0);std::vector<unsigned char> bytes(size),again(size);
 check(iopm_save_state(a,nullptr,0,&written)==0&&written==size);
 check(iopm_save_state(a,bytes.data(),size-1,&written)==IOPM_BUFFER_TOO_SMALL);
 check(iopm_save_state(a,bytes.data(),size,&written)==0);
 check(iopm_load_state(b,bytes.data(),size)==0);
 check(iopm_save_state(b,again.data(),size,&written)==0&&again==bytes);
 check(iopm_render_f32(a,x.data(),4096)==0);check(iopm_render_f32(b,y.data(),4096)==0);check(x==y);
 check(iopm_load_state(a,bytes.data(),size)==0);check(iopm_render_f32(a,y.data(),4096)==0);check(x==y);
 check(iopm_save_state(a,again.data(),size,&written)==0);bytes[30]^=1;
 check(iopm_load_state(a,bytes.data(),size)==IOPM_INVALID_STATE);std::vector<unsigned char> after(size);check(iopm_save_state(a,after.data(),size,&written)==0&&after==again);
 check(iopm_load_state(a,bytes.data(),size-1)==IOPM_INVALID_STATE);
 iopm_destroy(a);iopm_destroy(b);
 std::puts("PASS: core full-state roundtrip, four LFO shapes, timers/BUSY/latch/release, corruption atomicity; DLL filter/remainder restore, cross-config restore, identical 4096 frames");
 return 0;
}catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}}
