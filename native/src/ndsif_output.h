// SPDX-License-Identifier: 0BSD
// Copyright (c) 2026 IndependentMDX contributors
// Shared preview hooks: Copyright (C) 2026 by I.C.KaZe
#pragma once
#include "ndsif_wire.h"
#include "c86_gain.h"
#include "player.h"
#include <windows.h>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <thread>
#include <set>
namespace mdx {
class NdOutput {
    using B=nd::B;
    using Clock=std::chrono::steady_clock;
    HANDLE port=INVALID_HANDLE_VALUE;
    uint16_t tx=0;
    uint32_t hz=4000000,dataCap=0,eventCap=0,fixed=0,okiFixed=0;
    bool variable=false,okiVariable=false,pcm=false,started=false;
    size_t maxPayload=256;
    uint32_t accepted=0,played=0;
    uint64_t baseTick=0;
    uint32_t blockPos=0;
    nd::Encoder encoder;
    struct Group{uint32_t pos;B pairs;};
    std::deque<Group> groups;
    std::deque<std::pair<uint32_t,unsigned>> committed;
    unsigned reserved=0;
    struct Due{Clock::time_point at;int a,v;};
    std::deque<Due> queue;
    std::mutex lock;
    std::condition_variable wake;
    std::thread sender;
    bool quit=false,timed=false;
    Clock::time_point origin;
    uint64_t first=0;
    std::string failure;
    std::atomic<int> delay{0};
    bool rendering=false;
    C86Gain gain;
    std::atomic<double> master{1},fm{1};
    const std::atomic<uint32_t>* overrides=nullptr;
    const std::atomic<uint32_t>* mask=nullptr;
    void send(int type,int cmd,const B& data){
        auto bytes=nd::frame(type,cmd,++tx,data);DWORD count=0;
        if(!WriteFile(port,bytes.data(),DWORD(bytes.size()),&count,nullptr)||count!=bytes.size())
            throw std::runtime_error("NDSIF: serial write failed");
    }
    B ask(int cmd,const B& data={}){
        send(1,cmd,data);B enc,raw;bool overflow=false;
        auto until=Clock::now()+std::chrono::milliseconds(700);
        while(Clock::now()<until){uint8_t buf[512];DWORD n=0;
            if(!ReadFile(port,buf,sizeof(buf),&n,nullptr))throw std::runtime_error("NDSIF: serial read failed");
            for(DWORD i=0;i<n;++i){if(buf[i]){if(enc.size()<269)enc.push_back(buf[i]);else overflow=true;continue;}
                if(!overflow&&nd::decode(enc,raw)&&raw[3]==2&&raw[4]==cmd&&nd::r16(raw,5)==tx){
                    if(raw.size()<12||raw[9])throw std::runtime_error("NDSIF: control rejected ("+std::to_string(cmd)+")");
                    return B(raw.begin()+9,raw.end()-2);}
                enc.clear();overflow=false;}}
        throw std::runtime_error("NDSIF: response timeout ("+std::to_string(cmd)+")");
    }
    void clockSet(int id,uint32_t value,bool var,uint32_t fix){
        if(!var){if(fix!=value)throw std::runtime_error("NDSIF: incompatible chip clock");return;}
        B p={uint8_t(id),0};nd::u32(p,value);ask(4,p);
    }
    B status(){
        auto s=ask(0x13,{0xb7,0});
        if(s.size()!=41||nd::r32(s,29)&4)throw std::runtime_error("NDSIF: ADPCM status/fault");
        played=nd::r32(s,5);
        while(!committed.empty() && committed.front().first<played){
            reserved-=committed.front().second;committed.pop_front();}
        return s;
    }
    void resetLocked(){
        rendering=false;timed=false;queue.clear();groups.clear();committed.clear();reserved=0;gain.reset();encoder={};started=false;accepted=played=blockPos=0;baseTick=0;
        wake.notify_all();
        if(active()){ask(0);clockSet(0x54,hz,variable,fixed);
            if(pcm){clockSet(0xb7,8000000,okiVariable,okiFixed);auto s=status();
                if(nd::r32(s,1)||played||nd::r32(s,9)||nd::r32(s,29))throw std::runtime_error("NDSIF: stream reset failed");}}
    }
    template<class Emit>void adjusted(int a,int v,Emit emit){gain.setGain(master.load(),fm.load(),emit);gain.write(a,v,emit);}
    void flush(B& b){if(b.size()>2){send(0,0x54,b);b.resize(2);}}
    void immediate(B& b,int a,int v){if(b.size()+2>maxPayload)flush(b);b.push_back(uint8_t(a));b.push_back(uint8_t(v));}
    void loop(){
        std::unique_lock<std::mutex> g(lock);
        try{while(!quit){if(queue.empty()){wake.wait(g);continue;}auto at=queue.front().at;
            if(Clock::now()<at){wake.wait_until(g,at);continue;}
            B b={0,0};
            while(!queue.empty()&&queue.front().at<=Clock::now()){auto e=queue.front();queue.pop_front();
                int ch=e.a==8?(e.v&7):(e.a>=32?(e.a&7):-1);
                if(ch>=0&&((overrides&&(overrides->load()&(1u<<ch)))||(mask&&(mask->load()&(1u<<ch))&&(e.a==8||e.a>=0x28))))continue;
                adjusted(e.a,e.v,[&](int a,int v){immediate(b,a,v);});}
            flush(b);}}
        catch(const std::exception& e){failure=e.what();queue.clear();}
    }
    void data(const B& bytes){
        for(size_t i=0;i<bytes.size();){size_t n=std::min({size_t(250),maxPayload-6,bytes.size()-i});
            if(n>UINT32_MAX-accepted)throw std::runtime_error("NDSIF: stream position exhausted");
            B p={0xb7,0};nd::u32(p,accepted);p.insert(p.end(),bytes.begin()+i,bytes.begin()+i+n);
            send(0,0x10,p);accepted+=uint32_t(n);i+=n;}
    }
    void beginStream(){
        ask(1);B start={0xb7,0};nd::u32(start,8000000);nd::u16(start,512);
        send(0,0x12,start);started=true;
    }
    void events(uint32_t end,const std::atomic<bool>& stop){
        while(!groups.empty()&&groups.front().pos<=end){auto& e=groups.front();
            unsigned count=unsigned(e.pairs.size()/2);
            auto limit=Clock::now()+std::chrono::seconds(2);
            while(reserved+count>eventCap){
                if(stop.load())return;
                if(committed.empty()||committed.front().first>=accepted || Clock::now()>limit)
                    throw std::runtime_error("NDSIF: FM event density exceeds device queue capacity");
                if(!started)beginStream();
                status();if(reserved+count>eventCap)Sleep(1);
            }
            reserved+=count;committed.emplace_back(e.pos,count);
            B b={0xb7,0};nd::u32(b,e.pos);b.insert(b.end(),{0,0x54,0,0});b.insert(b.end(),e.pairs.begin(),e.pairs.end());send(0,0x11,b);groups.pop_front();}
    }
  public:
    bool active()const{return port!=INVALID_HANDLE_VALUE;}
    bool devicePcm()const{return active()&&pcm;}
    void setGain(double m,double f){master=m;fm=f;}
    void setDelay(int ms){delay=ms;}
    void setChannelOverride(const std::atomic<uint32_t>* o,const std::atomic<uint32_t>* m){overrides=o;mask=m;}
    void open(int com,bool device){
        close();pcm=device;hz=4000000;fixed=okiFixed=0;variable=okiVariable=false;failure.clear();
        auto name=L"\\\\.\\COM"+std::to_wstring(com);
        port=CreateFileW(name.c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_EXISTING,0,nullptr);
        if(!active())throw std::runtime_error("NDSIF: cannot open selected COM port");
        try{DCB d{};d.DCBlength=sizeof(d);if(!GetCommState(port,&d))throw std::runtime_error("NDSIF: COM configuration");
            d.BaudRate=115200;d.ByteSize=8;d.Parity=NOPARITY;d.StopBits=ONESTOPBIT;d.fBinary=1;d.fParity=d.fOutX=d.fInX=d.fOutxCtsFlow=d.fOutxDsrFlow=d.fDsrSensitivity=0;
            d.fDtrControl=DTR_CONTROL_ENABLE;d.fRtsControl=RTS_CONTROL_DISABLE;
            COMMTIMEOUTS t{MAXDWORD,MAXDWORD,10,0,300};
            if(!SetCommState(port,&d)||!SetCommTimeouts(port,&t))throw std::runtime_error("NDSIF: COM configuration");
            PurgeComm(port,PURGE_RXCLEAR|PURGE_TXCLEAR);if(ask(1,{0x49,0x4d})!=B({0,0x49,0x4d}))throw std::runtime_error("NDSIF: PING mismatch");
            ask(2);bool opm=false,positioned=false,oki=false;std::set<unsigned> seen;unsigned off=0;
            do{if(!seen.insert(off).second)throw std::runtime_error("NDSIF: capability page cycle");
                B p;nd::u16(p,off);auto b=ask(5,p);if(b.size()<14||b.size()!=size_t(14+b[13]*9))throw std::runtime_error("NDSIF: invalid capabilities");
                maxPayload=nd::r16(b,1);dataCap=nd::r32(b,3);eventCap=nd::r16(b,7);off=nd::r16(b,11);
                for(size_t i=14;i<b.size();i+=9){if(b[i+1]||b[i+2])continue;
                    if(b[i]==0x54){opm=b[i+3]&1;positioned=b[i+3]&2;variable=b[i+4]==2&&(b[i+3]&4);fixed=b[i+4]==1?nd::r32(b,i+5):0;}
                    if(b[i]==0xb7){oki=b[i+3]&8;okiVariable=b[i+4]==2&&(b[i+3]&4);okiFixed=b[i+4]==1?nd::r32(b,i+5):0;}}}while(off!=65535);
            if(!opm||maxPayload<12||maxPayload>256||(pcm&&(!oki||!positioned||dataCap<256||eventCap<128)))throw std::runtime_error("NDSIF: required capability missing");
            if(!variable&&fixed)hz=fixed;
            resetLocked();quit=false;if(!pcm)sender=std::thread([this]{loop();});
        }catch(...){CloseHandle(port);port=INVALID_HANDLE_VALUE;throw;}
    }
    void close()noexcept{
        {std::lock_guard<std::mutex> g(lock);quit=true;queue.clear();wake.notify_all();}
        if(sender.joinable())sender.join();
        if(active()){try{ask(0);}catch(...){}CloseHandle(port);port=INVALID_HANDLE_VALUE;}
    }
    ~NdOutput(){close();}
    void clock(uint32_t value){std::lock_guard<std::mutex> g(lock);clockSet(0x54,value,variable,fixed);hz=value;}
    void reset(){std::lock_guard<std::mutex> g(lock);resetLocked();}
    void write(int a,int v){std::lock_guard<std::mutex> g(lock);B b={0,0};adjusted(a,v,[&](int x,int y){immediate(b,x,y);});flush(b);}
    void scheduled(uint64_t tick,int a,int v,const std::atomic<bool>& stop){
        if(stop.load())return;
        std::lock_guard<std::mutex> g(lock);if(!failure.empty())throw std::runtime_error(failure);
        if(pcm){uint64_t at=rendering ? uint64_t(blockPos)+(tick>baseTick?(tick-baseTick)/8:0) : accepted;
            if(at>UINT32_MAX)throw std::runtime_error("NDSIF: event position overflow");
            adjusted(a,v,[&](int x,int y){if(groups.empty()||groups.back().pos!=at||groups.back().pairs.size()+12>maxPayload)groups.push_back({uint32_t(at),{}});
                groups.back().pairs.insert(groups.back().pairs.end(),{uint8_t(x),uint8_t(y)});});}
        else{if(!timed){first=tick;origin=Clock::now();timed=true;}
            if(queue.size()>262144)throw std::runtime_error("NDSIF: FM queue overflow");
            queue.push_back({origin+std::chrono::microseconds((tick>=first?tick-first:0)*16)+std::chrono::milliseconds(delay.load()),a,v});wake.notify_one();}
    }
    // Render at 8 MHz / 512 = 15625 Hz. A byte represents eight 62500-Hz host ticks.
    void play(Source& s,const std::atomic<bool>& stop,std::atomic<double>& rendered,std::atomic<double>& audible,std::atomic<double>* display=nullptr,
        std::function<void(int16_t*,size_t)> mix={}, std::function<void(uint64_t)> consumed={}){
        std::array<int16_t,500> stereo{};std::deque<std::pair<uint32_t,double>> timeline;
        const uint32_t ahead=std::min<uint32_t>(dataCap-128,750);
        uint32_t pending=0;bool ending=false;auto lastProgress=Clock::now();uint32_t prev=0;
        while(!stop.load()){
            if(s.servicePending()){s.service();timeline.clear();ending=false;pending=prev=0;lastProgress=Clock::now();audible=rendered.load();}
            if(started){auto st=status();pending=nd::r32(st,9);if(consumed)consumed(uint64_t(played)*2);
                if(played!=prev){prev=played;lastProgress=Clock::now();}
                if(Clock::now()-lastProgress>std::chrono::seconds(2))throw std::runtime_error("NDSIF: device playback stalled");
                while(!timeline.empty()&&timeline.front().first<=played){audible=timeline.front().second;if(display)*display=timeline.front().second;timeline.pop_front();}
                if(ending&&(nd::r32(st,29)&2))break;
                if(ending||pending>=ahead){Sleep(2);continue;}}
            baseTick=uint64_t(std::llround(s.seconds()*62500));blockPos=accepted;
            rendering=true;auto n=s.render(stereo.data(),250);rendering=false;if(mix&&n)mix(stereo.data(),n);if(accepted==0 && prev){timeline.clear();prev=pending=0;}B bytes;
            for(size_t i=0;i<n;++i)encoder.sample((int(stereo[2*i])+stereo[2*i+1])/32,bytes);
            if(!n){for(int i=0;i<128||encoder.low>=0;++i)encoder.sample(0,bytes);ending=true;}
            events(accepted+uint32_t(bytes.size()),stop);if(stop.load())break;data(bytes);timeline.emplace_back(accepted,rendered.load());
            if(ending){B end={0xb7,0};nd::u32(end,accepted);auto copy=encoder;auto lo=copy.encode(0);auto hi=copy.encode(0);end.insert(end.end(),{3,uint8_t(lo|(hi<<4))});send(0,0x11,end);}
            if(!started&&(accepted>=ahead||ending)){beginStream();lastProgress=Clock::now();}
        }
    }
};
}

