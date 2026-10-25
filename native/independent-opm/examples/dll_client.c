/* SPDX-License-Identifier: 0BSD */
/* Copyright (C) 2026 by I.C.KaZe */
#include "independent_opm.h"
#include <stdio.h>
/* Example only: raw stereo float32 output; no MDX parser or audio device API. */
int main(void) {
    iopm_handle chip=0;float pcm[512*2];unsigned block;FILE* f;
    if(iopm_abi_version()!=IOPM_ABI_VERSION || iopm_create(4000000,48000,&chip)!=IOPM_OK)return 1;
    iopm_set_measured_output_timing(chip,1);
    iopm_write_register(chip,0x20,0xc7); /* ALG7, L/R on */
    iopm_write_register(chip,0x28,0x48);
    iopm_write_register(chip,0x58,1);    /* C2 multiplier */
    iopm_write_register(chip,0x98,31);   /* C2 attack */
    iopm_write_register(chip,0xf8,15);
    iopm_write_register(chip,8,0x40);    /* C2 key on, channel 0 */
    f=fopen("dll_example.f32","wb");if(!f){iopm_destroy(chip);return 2;}
    for(block=0;block<94;++block) {
        if(iopm_render_f32(chip,pcm,512)!=IOPM_OK || fwrite(pcm,sizeof(pcm),1,f)!=1){fclose(f);iopm_destroy(chip);return 3;}
    }
    fclose(f);iopm_destroy(chip);return 0;
}
