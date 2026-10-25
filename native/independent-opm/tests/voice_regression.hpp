// SPDX-License-Identifier: 0BSD
// Copyright (C) 2026 by I.C.KaZe
#pragma once
// Corrected user patches, rows in voice order M1, C1, M2, C2.
// Each row: AR DR SR RR SL OL KS ML DT1 DT2 AME.
namespace regression {
constexpr int ids[3] = {80,83,11};
constexpr int algorithms[3] = {6,7,5};
constexpr int feedback[3] = {7,6,7};
constexpr int voices[3][4][11] = {
 {{31,0,0,5,0,40,0,4,3,0,0},{17,10,0,9,2,10,0,4,7,0,0},
  {0,0,0,0,0,127,0,0,0,0,0},{0,0,0,0,0,127,0,0,0,0,0}},
 {{31,10,9,15,4,33,0,8,3,0,0},{27,10,9,15,4,15,0,2,7,0,0},
  {0,0,0,0,0,127,0,0,0,0,0},{0,0,0,0,0,127,0,0,0,0,0}},
 {{31,0,0,15,0,24,0,2,0,0,0},{31,0,0,15,0,15,0,1,0,0,0},
  {0,0,0,0,0,127,0,0,0,0,0},{0,0,0,0,0,127,0,0,0,0,0}}
};
template<class Write> void load(Write write, unsigned voice, unsigned ch=0) {
    write(0x20+ch,0xc0|(feedback[voice]<<3)|algorithms[voice]);
    write(0x28+ch,0x48);
    constexpr unsigned address_offsets[4]={0,16,8,24};
    for(unsigned row=0;row<4;++row) {
        const auto* r=voices[voice][row];const unsigned o=address_offsets[row]+ch;
        write(0x40+o,(r[8]<<4)|r[7]);write(0x60+o,r[5]);
        write(0x80+o,(r[6]<<6)|r[0]);write(0xa0+o,(r[10]<<7)|r[1]);
        write(0xc0+o,(r[9]<<6)|r[2]);write(0xe0+o,(r[4]<<4)|r[3]);
    }
    write(0x08,0x18|ch); // OP=3: M1 + C1
}
}
