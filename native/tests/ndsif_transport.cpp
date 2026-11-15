// SPDX-License-Identifier: 0BSD
#include "ndsif_output.h"
using namespace mdx;
using nd::B;
static void check(bool ok){if(!ok)throw std::runtime_error("NDSIF test failed");}
static B reply;static uint32_t accepted,played;static bool running,end,ended,sawSeek,sawFinal;
int WriteFile(int,const void* ptr,DWORD n,DWORD* written,void*){
    *written=n;auto p=static_cast<const uint8_t*>(ptr);B raw;check(p[0]==0&&p[n-1]==0&&nd::decode(B(p+1,p+n-1),raw));
    B data(raw.begin()+9,raw.end()-2),r{0};int cmd=raw[4];
    switch(cmd){
    case 0:accepted=played=0;running=end=ended=false;break;
    case 1:r.insert(r.end(),data.begin(),data.end());break;
    case 2:case 4:break;
    case 5:nd::u16(r,256);nd::u32(r,4096);nd::u16(r,1024);nd::u16(r,0);nd::u16(r,65535);r.push_back(2);
        for(auto id:{0x54,0xb7}){r.insert(r.end(),{uint8_t(id),0,0,uint8_t(id==0x54?7:12),2});nd::u32(r,0);}break;
    case 0x10:check(!end&&nd::r32(data,2)==accepted);accepted+=data.size()-6;break;
    case 0x11:if(data[6]==3){check(nd::r32(data,2)==accepted&&(data.at(7)==0x80||data.at(7)==8));end=true;sawFinal=true;}
        else {for(size_t i=10;i<data.size();i+=2)if(data[i]==0x28&&data[i+1]==0x45){check(nd::r32(data,2)==0);sawSeek=true;}}break;
    case 0x12:check(accepted>0&&!running);running=true;break;
    case 0x13:if(running){played=std::min(accepted,played+100);ended=end&&played==accepted;}
        for(uint32_t x:{accepted,played,accepted-played,0u,0u,0u,0u,uint32_t(ended?2:running),0u,0u})nd::u32(r,x);break;
    default:check(false);}
    if(raw[3]==1)reply=nd::frame(2,cmd,nd::r16(raw,5),r);
    return 1;
}
int ReadFile(int,void* ptr,DWORD n,DWORD* count,void*){*count=std::min<size_t>({n,reply.size(),7});std::memcpy(ptr,reply.data(),*count);reply.erase(reply.begin(),reply.begin()+*count);return 1;}
struct SongTest:Source{
    NdOutput& out;std::atomic<bool>& stop;int blocks=0;bool sought=false;
    SongTest(NdOutput& o,std::atomic<bool>& s):out(o),stop(s){}
    size_t render(int16_t* b,size_t n)override{if(++blocks==18)return 0;std::fill_n(b,n*2,int16_t(blocks%2?19000:-23000));out.scheduled(uint64_t(seconds()*62500),8,0x78,stop);return n;}
    bool servicePending()const override{return blocks==8&&!sought;}
    bool service()override{sought=true;out.reset();out.scheduled(2345678,0x28,0x45,stop);return true;}
    double seconds()const override{return (sought?37.:0)+blocks*.016;}
};
int main(){
    check(nd::crc({'1','2','3','4','5','6','7','8','9'},9)==0x29b1);
    for(unsigned n=0;n<=256;++n){B d(n,0xee),raw;if(n)d[n/2]=0;auto f=nd::frame(1,1,0xffff,d);
        B enc(f.begin()+1,f.end()-1);check(nd::decode(enc,raw)&&B(raw.begin()+9,raw.end()-2)==d);enc.back()^=1;check(!nd::decode(enc,raw));}
    NdOutput out;out.open(3,true);std::atomic<bool> stop{false};std::atomic<double> render{0},audible{0};SongTest song(out,stop);
    out.play(song,stop,render,audible);check(sawFinal&&sawSeek&&ended);out.close();
}

