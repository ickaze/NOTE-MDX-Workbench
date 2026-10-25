#include "notemdx/compiler.hpp"
#include "player.h"
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void check(bool value, const char *message) {
    if (!value) throw std::runtime_error(message);
}
const std::string voice = "@0={31,0,0,8,0,48,0,1,0,0,0,31,0,0,8,0,48,0,1,0,0,0,31,0,0,8,0,48,0,1,0,0,0,31,0,0,8,0,48,0,1,0,0,0,7,0,15}\n";
struct Capture {
    std::array<std::vector<uint64_t>,8> keyOns;
    std::array<std::vector<int>,8> firstLevels;
    std::array<int,8> levels{};
};
void observe(mdx::Options &options, Capture &capture) {
    options.registerLogOutput = [&](uint64_t time,int address,int value) {
        if (address >= 0x60 && address < 0x68) capture.levels[address-0x60]=value;
        if(address==8 && (value&0x78)) {
            const int channel=value&7;
            capture.keyOns[channel].push_back(time);
            capture.firstLevels[channel].push_back(capture.levels[channel]);
        }
    };
}
notemdx::Result selected(const std::string &body,int start,int end) {
    notemdx::Options options;
    options.selection = notemdx::SelectionRange{start,1,end,1000};
    auto result=notemdx::compile({"loop.mus",voice+body},options);
    if(!result.ok()) for(const auto &d:result.diagnostics) std::cerr<<d.message<<'\n';
    check(result.ok(),"selection compile failed");
    return result;
}
mdx::Options playback() {
    mdx::Options options;
    options.repeatSelection=true;
    options.loops=1;
    options.fadeSeconds=0;
    options.maxSeconds=0;
    options.sampleRate=62500;
    options.resampleOutput=false;
    options.skipFmSynthesis=true;
    options.skipPcmSynthesis=true;
    return options;
}
void render(mdx::Player &player, size_t frames) {
    std::array<int16_t,2048> scratch{};
    while(frames) {
        auto n=std::min<size_t>(frames,1024);
        check(player.render(scratch.data(),n)==n,"selection stopped between cycles");
        frames-=n;
    }
}
}
int main() {
    try {
        {
            // Earlier/later notes are excluded; different lengths share one boundary.
            auto result=selected("A @0 g1\nA @0 c4\nB @0 e2\nA @0 a1\n",3,4);
            check(result.statistics.tracks[0].total_steps.value==48 && result.statistics.tracks[1].total_steps.value==96,"selected steps mismatch");
            auto options=playback();Capture capture;observe(options,capture);
            mdx::Player player(mdx::Song(result.mdx),{},options);
            auto timeline=player.analyze();
            const uint64_t period=96*896;
            check(std::abs(timeline.duration-double(period)/62500)<1e-9 && timeline.loopEnd==timeline.duration,"selection timeline includes release tail");
            check(timeline.trackLoopEnd[0]==timeline.duration && timeline.trackLoopEnd[1]==timeline.duration,"piano roll loops are not synchronized");
            render(player,period*3+128);
            for(int channel:{0,1}) {
                check(capture.keyOns[channel]==std::vector<uint64_t>({0,period,2*period,3*period}),"tracks did not restart together at the longest selected end");
            }
            for(int channel=2;channel<8;++channel) check(capture.keyOns[channel].empty(),"unselected track played");
            check(std::abs(player.position(timeline)-128.0/62500)<1e-9,"playhead did not wrap to selection start");
        }
        {
            // Relative volume must not accumulate; single-track selections loop too.
            auto result=selected("A @0 ) c8\n",2,2);
            auto options=playback();Capture capture;observe(options,capture);
            mdx::Player player(mdx::Song(result.mdx),{},options);
            render(player,24*896*3+128);
            check(capture.keyOns[0].size()==4,"single-track selection did not repeat");
            for(int level:capture.firstLevels[0]) check(level==capture.firstLevels[0].front(),"relative volume accumulated across selection loops");
        }
        {
            // Initial tempo inheritance and explicit tempo changes repeat identically.
            auto result=selected("A @0 t120 c8 t200 d8\nB @0 e4\n",2,3);
            auto options=playback();Capture capture;observe(options,capture);
            mdx::Player player(mdx::Song(result.mdx),{},options);
            auto timeline=player.analyze();
            render(player,220000);
            check(capture.keyOns[1].size()>=3,"tempo selection did not repeat");
            auto period=capture.keyOns[1][1]-capture.keyOns[1][0];
            check(std::abs(timeline.duration-double(period)/62500)<1e-9,"tempo-change timeline differs from playback");
            for(size_t i=1;i<capture.keyOns[1].size();++i) check(capture.keyOns[1][i]-capture.keyOns[1][i-1]==period,"tempo changed loop length on later cycles");
        }
        {
            // A selected rest track contributes to the shared loop length.
            auto result=selected("A @0 c8\nB r2\n",2,3);
            auto options=playback();Capture capture;observe(options,capture);
            mdx::Player player(mdx::Song(result.mdx),{},options);
            render(player,96*896*2+128);
            check(capture.keyOns[0]==std::vector<uint64_t>({0,96*896,192*896}),"rest-only longest track ignored");
        }
        {
            // PCM tracks use the same shared boundary as FM tracks.
            auto result=selected("#ex-pcm\nP c4\nQ e2\n",3,4);
            auto options=playback();
            std::array<std::vector<uint64_t>,8> starts;
            options.pcmLogOutput=[&](uint64_t time,int ch,int op,uint32_t,int) {
                if(op==0) starts[ch].push_back(time);
            };
            mdx::Player player(mdx::Song(result.mdx),{},options);
            render(player,96*896*2+128);
            for(int channel:{0,1}) check(starts[channel]==std::vector<uint64_t>({0,96*896,192*896}),"PCM tracks looped independently");
        }
        {
            // Seeking and rebuilding the sequencer preserves selection looping.
            auto result=selected("A @0 c4\nB @0 e2\n",2,3);
            auto options=playback();options.fastSeek=true;
            Capture capture;observe(options,capture);
            mdx::Player player(mdx::Song(result.mdx),{},options);
            const auto timeline=player.analyze();
            render(player,96*896+1024);
            check(player.seek(48*896.0/62500),"selection seek failed");
            check(std::abs(player.position(timeline)-48*896.0/62500)<1e-6,"selection seek position wrong");
            capture.keyOns={};
            render(player,48*896+128);
            check(capture.keyOns[0]==std::vector<uint64_t>({96*896}) && capture.keyOns[1]==capture.keyOns[0],"selection failed to loop together after seek");
        }
        {
            // Only initial rests survive before selected notes; they repeat each cycle.
            auto result=selected("A r4 @0 g4\nB r8 @0 f4\nA c4\nB e2\n",4,5);
            check(result.statistics.tracks[0].total_steps.value==96 && result.statistics.tracks[1].total_steps.value==120,"initial rests missing from selection steps");
            auto options=playback();Capture capture;observe(options,capture);
            mdx::Player player(mdx::Song(result.mdx),{},options);
            const uint64_t period=120*896;
            render(player,period*2+64*896);
            check(capture.keyOns[0]==std::vector<uint64_t>({48*896,period+48*896,2*period+48*896}),"FM initial rest not replayed every cycle");
            check(capture.keyOns[1]==std::vector<uint64_t>({24*896,period+24*896,2*period+24*896}),"track-specific initial rest or shared loop wrong");
        }
        {
            // Intro silence after an instrument command is not a delay part.
            auto result=selected("A @0 r4 g4\nB @0 r8 f4\nA c4\nB e2\n",4,5);
            check(result.statistics.tracks[0].total_steps.value==48 && result.statistics.tracks[1].total_steps.value==96,"intro silence added after instrument command");
            auto options=playback();Capture capture;observe(options,capture);
            mdx::Player player(mdx::Song(result.mdx),{},options);
            const uint64_t period=96*896;
            render(player,period*2+128);
            check(capture.keyOns[0]==std::vector<uint64_t>({0,period,2*period}) && capture.keyOns[1]==capture.keyOns[0],"intro silence shifted selected notes");
        }
        {
            auto result=selected("#ex-pcm\nP r8 c4\nQ r4 d4\nP e4\nQ f4\n",5,6);
            auto options=playback();std::array<std::vector<uint64_t>,8> starts;
            options.pcmLogOutput=[&](uint64_t time,int ch,int op,uint32_t,int) { if(op==0) starts[ch].push_back(time); };
            mdx::Player player(mdx::Song(result.mdx),{},options);
            const uint64_t period=96*896;
            render(player,period*2+64*896);
            check(starts[0]==std::vector<uint64_t>({24*896,period+24*896,2*period+24*896}),"PCM initial rest not added");
            check(starts[1]==std::vector<uint64_t>({48*896,period+48*896,2*period+48*896}),"PCM initial delay not synchronized");
        }
        {
            // Normal playback retains its finite end and zero-time selections terminate.
            auto result=selected("A @0 c8\n",2,2);
            auto options=playback();options.repeatSelection=false;
            mdx::Player player(mdx::Song(result.mdx),{},options);
            std::array<int16_t,2048> scratch{};size_t total=0;
            while(auto n=player.render(scratch.data(),1024)) { total+=n;check(total<100000,"ordinary finite playback started looping"); }
            auto empty=selected("A @0 v8\n",2,2);options.repeatSelection=true;
            int resets=0;
            options.registerLogOutput=[&](uint64_t,int address,int) { if(address==0x18) ++resets; };
            mdx::Player zero(mdx::Song(empty.mdx),{},options);total=0;
            while(auto n=zero.render(scratch.data(),1024)) { total+=n;check(total<100000,"zero-time selection looped forever"); }
            check(resets==0,"zero-time selection restarted while releasing");
        }
        std::cout<<"selection loops: synchronized ends, excerpt bounds, state, tempo, rests and finite/empty guards passed\n";
        return 0;
    } catch(const std::exception &error) { std::cerr<<error.what()<<'\n';return 1; }
}
