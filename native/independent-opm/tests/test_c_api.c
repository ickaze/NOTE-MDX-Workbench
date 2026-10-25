/* SPDX-License-Identifier: 0BSD */
/* Copyright (C) 2026 by I.C.KaZe */
#include "independent_opm.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
#define CHECK(x) do { if(!(x)) {fprintf(stderr,"line %d: %s\n",__LINE__,#x);return 1;} } while(0)
static void tone(iopm_handle h) {
    iopm_write_register(h,0x20,0xc7);
    iopm_write_register(h,0x28,0x48);
    iopm_write_register(h,0x58,1);
    iopm_write_register(h,0x98,31);
    iopm_write_register(h,0xf8,15);
    iopm_write_register(h,8,0x40);
}
int main(void) {
    iopm_handle a=0,b=0,c=0;float x[4096],y[4096];int16_t pcm[4096];uint64_t n=0;
    const iopm_event bad={999999,0x28,0x48};
    iopm_event events[2]={{123,0x28,0x50},{457,0x28,0x48}};
    CHECK(iopm_abi_version()==1);CHECK(sizeof(iopm_event)==12);
    CHECK(iopm_create(0,48000,&a)==IOPM_INVALID_ARGUMENT && a==0);
    CHECK(iopm_create(4000000,48000,&a)==0);tone(a);
    CHECK(iopm_clone(a,&b)==0);
    CHECK(iopm_render_f32(a,x,2048)==0);
    CHECK(iopm_render_f32(b,y,1)==0);
    CHECK(iopm_render_f32(b,y+2,17)==0);
    CHECK(iopm_render_f32(b,y+36,2030)==0);
    CHECK(memcmp(x,y,sizeof(x))==0);CHECK(iopm_clock_count(a,&n)==0 && n==170666);
    {double energy=0;unsigned i;for(i=0;i<4096;++i){CHECK(isfinite(x[i]));energy+=x[i]*x[i];}CHECK(energy>0.001);}
    CHECK(iopm_clone(a,&c)==0);x[0]=123;
    CHECK(iopm_render_events_f32(a,x,8,&bad,1)==IOPM_INVALID_ARGUMENT && x[0]==123);
    CHECK(iopm_render_f32(a,x,8)==0 && iopm_render_f32(c,y,8)==0 && memcmp(x,y,16*sizeof(float))==0);
    iopm_destroy(c);c=0;
    CHECK(iopm_reset(a)==0 && iopm_reset(b)==0);tone(a);tone(b);
    CHECK(iopm_render_events_f32(a,x,2048,events,2)==0);
    CHECK(iopm_render_f32(b,y,1)==0); /* first endpoint: 83 clocks */
    events[0].clock_offset-=83;events[1].clock_offset-=83;
    CHECK(iopm_render_events_f32(b,y+2,2047,events,2)==0);
    CHECK(memcmp(x,y,sizeof(x))==0);
    CHECK(iopm_reset(a)==0);CHECK(iopm_render_s16(a,pcm,2048)==0);
    {unsigned i;for(i=0;i<4096;++i)CHECK(pcm[i]==0);}
    CHECK(iopm_set_measured_output_timing(a,1)==0);
    CHECK(iopm_set_output_delays(a,8,0,0)==IOPM_INVALID_ARGUMENT);
    CHECK(iopm_render_f32(a,0,0)==0);CHECK(iopm_render_f32(a,0,1)==IOPM_INVALID_ARGUMENT);
    iopm_destroy(a);iopm_destroy(b);iopm_destroy(0);puts("C ABI tests passed");return 0;
}
