#pragma once
#include "player.h"
#include <algorithm>
#include <iomanip>
#include <sstream>
#include <optional>
#include <charconv>
#include <cctype>
namespace mdx {
using Voice = std::array<uint8_t, 27>;
inline std::array<int,47> voiceParameterValues(const Voice &voice) {
    std::array<int,47> result{};
    constexpr int order[]={0,2,1,3};
    for(int row=0;row<4;++row) {
        int op=order[row],dt=voice[3+op],tl=voice[7+op],ar=voice[11+op],dr=voice[15+op],sr=voice[19+op],rr=voice[23+op];
        int values[]={ar&31,dr&31,sr&31,rr&15,rr>>4,tl&127,ar>>6,dt&15,(dt>>4)&7,sr>>6,dr>>7};
        std::copy(std::begin(values),std::end(values),result.begin()+row*11);
    }
    result[44]=voice[1]&7;result[45]=(voice[1]>>3)&7;result[46]=voice[2]&15;
    return result;
}
inline std::string voiceMml(const Voice &v, int id) {
    std::ostringstream s;
    s << "@" << id << " = {\r\n/* AR, DR, SR, RR, SL, OL, KS, ML, DT1, DT2, AME */\r\n";
    // MML/diagram order is OP1, OP2, OP3, OP4; register slots are 0,2,1,3.
    for (int op : {0, 2, 1, 3}) {
        int dt = v[3 + op], tl = v[7 + op], ar = v[11 + op], dr = v[15 + op], sr = v[19 + op],
            rr = v[23 + op];
        const int values[] = {ar & 31, dr & 31, sr & 31,       rr & 15, rr >> 4, tl & 127,
                              ar >> 6, dt & 15, (dt >> 4) & 7, sr >> 6, dr >> 7};
        s << "  ";
        for (int x : values)
            s << std::setw(3) << x << ',';
        s << "\r\n";
    }
    s << "/* CON, FL, OP */\r\n  " << (v[1] & 7) << ", " << ((v[1] >> 3) & 7) << ", " << (v[2] & 15)
      << "\r\n}\r\n";
    return s.str();
}
// Parse the editable standalone definition, without compiler directives or track state.
inline std::optional<Voice> parseVoiceMml(const std::string& text, std::string& error) {
    std::string clean;
    std::istringstream lines(text); std::string line;
    while(std::getline(lines,line)) {
        auto comment=line.find_first_of(";*");
        auto slash=line.find("//"); auto block=line.find("/*");
        for(auto at:{slash,block}) if(at!=std::string::npos && (comment==std::string::npos || at<comment)) comment=at;
        clean+=line.substr(0,comment);clean+='\n';
    }
    size_t at=0;
    auto ws=[&]{while(at<clean.size() && std::isspace(static_cast<unsigned char>(clean[at])))++at;};
    auto symbol=[&](char c){ws();if(at==clean.size() || clean[at]!=c)return false;++at;return true;};
    auto number=[&](int& value){ws();bool neg=false;if(at<clean.size() && (clean[at]=='+'||clean[at]=='-')){neg=clean[at++]=='-';}
        int base=10;if(at<clean.size() && clean[at]=='$'){base=16;++at;}
        auto result=std::from_chars(clean.data()+at,clean.data()+clean.size(),value,base);
        if(result.ec!=std::errc())return false;at=size_t(result.ptr-clean.data());if(neg)value=-value;return true;};
    int id=0;std::array<int,47> values{};
    if(!symbol('@') || !number(id) || id<0 || id>255 || !symbol('=') || !symbol('{')){error="Expected @0..255 = { ... }.";return {};}
    constexpr int limits[]={31,31,31,15,15,127,3,15,7,3,1};
    for(int n=0;n<47;++n) {
        if(!number(values[n])){error="Expected parameter "+std::to_string(n+1)+" of 47.";return {};}
        int maximum=n<44 ? limits[n%11] : n==46 ? 15 : 7;
        if(values[n]<0 || values[n]>maximum){error="Parameter "+std::to_string(n+1)+" must be 0.."+std::to_string(maximum)+".";return {};}
        size_t before=at;ws();bool separated=at>before;bool comma=symbol(',');
        if(n<46 && !comma && !separated){error="Expected comma or whitespace.";return {};}
    }
    if(!symbol('}')){error="Expected closing }.";return {};}
    ws();if(at!=clean.size()){error="Unexpected text after definition.";return {};}
    Voice voice{};voice[0]=uint8_t(id);voice[1]=uint8_t(values[44]|(values[45]<<3));voice[2]=uint8_t(values[46]);
    constexpr int order[]={0,2,1,3};
    for(int slot=0;slot<4;++slot){auto v=values.data()+order[slot]*11;
        voice[3+slot]=uint8_t((v[8]<<4)|v[7]);voice[7+slot]=uint8_t(v[5]);voice[11+slot]=uint8_t((v[6]<<6)|v[0]);
        voice[15+slot]=uint8_t((v[10]<<7)|v[1]);voice[19+slot]=uint8_t((v[9]<<6)|v[2]);voice[23+slot]=uint8_t((v[4]<<4)|v[3]);}
    error.clear();return voice;
}
inline Voice registerVoice(const std::array<uint8_t, 256> &r, int ch, int mask) {
    Voice v{};
    v[1] = r[0x20 + ch] & 63;
    v[2] = uint8_t(mask);
    for (int g = 0; g < 6; ++g)
        for (int op = 0; op < 4; ++op)
            v[3 + g * 4 + op] = r[0x40 + g * 32 + op * 8 + ch];
    return v;
}
inline std::string voiceRegisters(const std::array<uint8_t, 256> &r, int ch) {
    std::ostringstream s;
    s << "/* Channel " << char('A' + ch) << " : y register,value */\r\n";
    s << std::uppercase << std::hex << std::setfill('0');
    auto row = [&](int a) { s << "y$" << std::setw(2) << a << ",$" << std::setw(2) << int(r[a]) << "\r\n"; };
    for (int a = 0x20 + ch; a < 0x40; a += 8)
        row(a);
    for (int g = 0; g < 6; ++g)
        for (int op = 0; op < 4; ++op)
            row(0x40 + g * 32 + op * 8 + ch);
    return s.str();
}
inline std::string songVoicesMml(const Song &s) {
    std::string out =
        "/* IndependentMDX: MXDRV instrument definitions (OP1,OP2,OP3,OP4 / M1,C1,M2,C2) */\r\n";
    for (int i = 0; i < 256; ++i)
        if (s.hasVoice[i])
            out += voiceMml(s.voices[i], i) + "\r\n";
    return out;
}
} // namespace mdx
