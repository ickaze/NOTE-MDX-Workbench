// SPDX-License-Identifier: 0BSD
// Copyright (C) 2026 by I.C.KaZe
#include "independent_opm.h"
#include "voice_regression.hpp"
#include <vector>
#include <cmath>
#include <cstdio>
#define CHECK(x) do {if(!(x)){std::fprintf(stderr,"line %d: %s\n",__LINE__,#x);return 1;}}while(0)
int main() {
 for(unsigned alg=0;alg<8;++alg) {
  iopm_handle h=nullptr,other=nullptr;CHECK(iopm_create(4000000,48000,&h)==0);
  for(unsigned ch=0;ch<8;++ch) {
   regression::load([&](unsigned a,unsigned v){iopm_write_register(h,a,v);},2,ch);
   CHECK(iopm_write_register(h,0x20+ch,0xc0|alg)==0);
   CHECK(iopm_write_register(h,0x28+ch,0x38+ch)==0);
   CHECK(iopm_write_register(h,8,0x78|ch)==0);
  }
  CHECK(iopm_write_register(h,0x0f,0x9f)==0);
  CHECK(iopm_set_measured_output_timing(h,1)==0);
  CHECK(iopm_clone(h,&other)==0);
  const unsigned n=1024;std::vector<float> mix(n*2),taps(n*16),ref(n*2);
  CHECK(iopm_render_channels_f32(h,mix.data(),nullptr,n)==IOPM_INVALID_ARGUMENT);
  const iopm_event bad{1,256,0};
  CHECK(iopm_render_events_channels_f32(h,mix.data(),taps.data(),n,&bad,1)==IOPM_INVALID_ARGUMENT);
  CHECK(iopm_render_channels_f32(h,mix.data(),taps.data(),n)==0);
  CHECK(iopm_render_f32(other,ref.data(),n)==0);CHECK(mix==ref);
  for(unsigned i=0;i<n;++i)for(unsigned side=0;side<2;++side) {
   double sum=0;for(unsigned ch=0;ch<8;++ch)sum+=taps[16*i+2*ch+side];
   CHECK(std::abs(sum-mix[2*i+side])<2e-7);
  }
  uint32_t size=0,written=0;CHECK(iopm_state_size(h,&size)==0);std::vector<unsigned char> state(size);
  CHECK(iopm_save_state(h,state.data(),size,&written)==0);
  CHECK(iopm_render_channels_f32(h,mix.data(),taps.data(),n)==0);
  CHECK(iopm_load_state(h,state.data(),size)==0);
  std::vector<float> replay(n*16);CHECK(iopm_render_channels_f32(h,ref.data(),replay.data(),n)==0);
  CHECK(mix==ref && taps==replay);
  iopm_destroy(other);iopm_destroy(h);
 }
 // Pan bits do not affect taps; event and split rendering produce identical samples.
 iopm_handle h=nullptr,b=nullptr;CHECK(iopm_create(4000000,96000,&h)==0);
 regression::load([&](unsigned a,unsigned v){iopm_write_register(h,a,v);},2);
 CHECK(iopm_clone(h,&b)==0);
 std::vector<float> m(512*2),c(512*16),r(512*2),d(512*16);
 const iopm_event ev{0,0x20,(7<<3)|5};
 CHECK(iopm_render_events_channels_f32(h,m.data(),c.data(),512,&ev,1)==0);
 CHECK(iopm_render_channels_f32(b,r.data(),d.data(),128)==0);
 CHECK(iopm_render_channels_f32(b,r.data()+256,d.data()+2048,384)==0);
 CHECK(c==d);for(float v:m)CHECK(v==0);
 CHECK(iopm_reset(h)==0);CHECK(iopm_render_channels_f32(h,m.data(),c.data(),512)==0);
 for(float v:c)CHECK(v==0);
 CHECK(iopm_render_channels_f32(h,nullptr,nullptr,0)==0);
 iopm_destroy(h);iopm_destroy(b);puts("channel render passed");
}
