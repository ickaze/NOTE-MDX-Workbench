#include "algorithm_layout.h"
#include "c86_gain.h"
#include "carrier_notes.h"
#include "live_session.h"
#include "midi_synth.h"
#include "player.h"
#include "lfo_random.h"
#include "vgm_export.h"
#include "resume_position.h"
#include "instrument_waveform.h"
#include "stereo_output.h"
#include "voice_text.h"
#include "x68_render.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <fstream>
#include <filesystem>
#include <iostream>
#include <random>
#include <stdexcept>
#include <tuple>
using mdx::Bytes;
void require(bool ok, const char *what) {
    if (!ok)
        throw std::runtime_error(what);
}
void setWord(Bytes &b, size_t p, size_t v) {
    b[p] = static_cast<uint8_t>(v >> 8);
    b[p + 1] = static_cast<uint8_t>(v);
}
void setLong(Bytes &b, size_t p, size_t v) {
    for (int i = 3; i >= 0; --i) {
        b[p + i] = static_cast<uint8_t>(v);
        v >>= 8;
    }
}
Bytes songFile(const std::vector<Bytes> &commands, bool withPdx = false) {
    Bytes b{'T', 13, 10, 26};
    if (withPdx) {
        b.push_back('T');
    }
    b.push_back(0);
    size_t base = b.size(), count = commands.size();
    b.resize(base + (count + 1) * 2);
    for (size_t i = 0; i < count; ++i) {
        setWord(b, base + 2 + i * 2, b.size() - base);
        b.insert(b.end(), commands[i].begin(), commands[i].end());
    }
    setWord(b, base, b.size() - base);
    // Original test instrument: four additive sines, only slot 1 keyed on.
    const Bytes voice{0,  7, 1, 1, 1, 1, 1, 0, 127, 127, 127, 31, 31, 31,
                      31, 0, 0, 0, 0, 0, 0, 0, 0,   15,  15,  15, 15};
    b.insert(b.end(), voice.begin(), voice.end());
    return b;
}
Bytes oneTrack(Bytes commands) {
    std::vector<Bytes> tracks(9, Bytes{241, 0});
    tracks[0] = std::move(commands);
    return songFile(tracks);
}
std::vector<int16_t> render(mdx::Player &p, size_t block = 2048) {
    std::vector<int16_t> result;
    std::vector<int16_t> buffer(block * 2);
    for (;;) {
        size_t n = p.render(buffer.data(), block);
        if (!n)
            break;
        result.insert(result.end(), buffer.begin(), buffer.begin() + n * 2);
        require(result.size() < 10000000, "unbounded render");
    }
    return result;
}
template <class F> void rejects(F operation, const char *what) {
    try {
        operation();
    } catch (const std::exception &) {
        return;
    }
    throw std::runtime_error(what);
}
int main() {
    try {
        int passed = 0;
        {
            int last=-1;
            auto file=mdx::makeVgm(mdx::Song(oneTrack({253,0,180,16,241,0})),{},[&](int value){
                require(value>=last && value<=1000,"VGM progress is monotonic");last=value;
            });
            require(last==950 && !file.empty(),"VGM progress completed native stages");
            rejects([]{mdx::makeVgm(mdx::Song(oneTrack({253,0,180,16,241,0})),{},[](int value){
                if(value>=50)throw std::runtime_error("cancel");
            });},"VGM cancellation ignored");
        }

        require(mdx::resumePosition(true, 12, 0, -1) == 12, "backend preparation retains position");
        require(mdx::resumePosition(true, 12, 0, 24) == 24, "pending seek supersedes retained position");
        require(mdx::resumePosition(false, 12, 13, -1) == 13, "ready backend uses current position");
        auto le32 = [](const Bytes &b, size_t p) { return uint32_t(b[p]) | uint32_t(b[p+1])<<8 | uint32_t(b[p+2])<<16 | uint32_t(b[p+3])<<24; };
        for (bool loop : {false, true}) {
            auto file = mdx::makeVgm(mdx::Song(oneTrack(loop ? Bytes{253,0,180,1,241,255,251} : Bytes{253,0,180,1,241,0})), {});
            require(file[0]=='V' && file[1]=='g' && file[2]=='m' && file[3]==' ', "VGM signature");
            require(le32(file,4)+4 == file.size() && le32(file,8)==0x171, "VGM header/EOF");
            require(le32(file,0x30)==4000000, "YM2151 clock");
            uint64_t waits=0; size_t writes=0; bool ended=false;
            for(size_t p=0x34+le32(file,0x34);p<file.size();) {
                auto command=file[p++];
                if(command==0x54) {p+=2;++writes;}
                else if(command==0x61) {waits+=file[p]|uint16_t(file[p+1])<<8;p+=2;}
                else if(command>=0x70 && command<=0x7f) waits+=1+(command&15);
                else if(command==0x66) {ended=true;break;}
                else require(false,"unexpected VGM FM command");
            }
            require(ended && writes>0 && waits==le32(file,0x18), "VGM register log/wait total");
            require((le32(file,0x1c)!=0)==loop,"VGM loop presence");
            if(loop) require(le32(file,0x20)>0 && 0x1c+le32(file,0x1c)<file.size(),"VGM loop offset/count");
        }

        mdx::Options opts;
        opts.measuredOutputTiming = false;
        opts.resampleOutput = false;
        opts.maxSeconds = 3;
        opts.fadeSeconds = 0;
        opts.sampleRate = 62500;
        {
            auto data=oneTrack({255,200,253,0,251,128,0x80+54,255,241,0});
            auto observedOptions=opts;
            observedOptions.monitor=std::make_shared<mdx::Monitor>();
            mdx::Player reference(mdx::Song(data),{},opts), observed(mdx::Song(data),{},observedOptions);
            std::array<int16_t,2048> a{},b{};
            for(int block=0;block<16;++block) {
                observedOptions.monitor->waveforms.store(block<3 || block>=10);
                auto n=reference.render(a.data(),1024);
                require(n==observed.render(b.data(),1024) && a==b,"waveform toggle changed audible samples");
                require(observedOptions.monitor->frame.note[0]==54,"waveform OFF lost keyboard note");
                if(block>=3 && block<10)
                    require(std::all_of(observedOptions.monitor->frame.waveform[0].begin(),observedOptions.monitor->frame.waveform[0].end(),[](float x){return x==0;}),"disabled waveform stale");
            }
            ++passed;
        }
        {
            auto file=std::filesystem::path(__FILE__).parent_path().parent_path()/"samples"/"DEMO.MDX";
            std::ifstream input(file, std::ios::binary);
            require(bool(input), "cannot open DEMO.MDX regression fixture");
            Bytes bytes((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
            auto view=std::make_shared<mdx::Monitor>();
            auto o=opts; o.monitor=view; o.externalFm=true;
            mdx::Player demo(mdx::Song(bytes), {}, o);
            std::array<int16_t,4096> block{};
            double peak=0;
            for(int i=0;i<32;++i) {
                demo.render(block.data(),2048);
                for(float x:view->frame.waveform[1]) peak=std::max(peak,std::abs(double(x)));
                require(std::all_of(block.begin(),block.end(),[](auto x){return x==0;}),"DEMO monitor leaked into hardware output");
            }
            require(peak>.001,"DEMO channel B ALG4 waveform missing");
            std::cout << "DEMO channel B monitor peak: " << peak << '\n';
            ++passed;
        }
        auto basic = oneTrack({255, 200, 253, 0, 251, 128, 0x80 + 54, 9, 241, 0});
        mdx::Player player(mdx::Song(basic), {}, opts);
        std::vector<std::tuple<uint64_t, int, int>> trace;
        player.registerTrace = [&](uint64_t tick, int r, int v) { trace.emplace_back(tick, r, v); };
        auto audio = render(player);
        require(std::any_of(audio.begin(), audio.end(), [](int16_t v) { return std::abs(int(v)) > 100; }),
                "FM output is silent");
        std::vector<uint64_t> ons, offs;
        for (auto [time, reg, value] : trace)
            if (reg == 8 && (value & 7) == 0) {
                if (value & 120)
                    ons.push_back(time);
                else
                    offs.push_back(time);
            }
        require(ons.size() == 1 && ons[0] == 0, "FM key-on time");
        require(std::find(offs.begin(), offs.end(), 8960) != offs.end(),
                "10 ticks must equal 8960 samples at tempo 200");
        ++passed;
        mdx::Options resampled = opts;
        resampled.sampleRate = 48000;
        mdx::Player pa(mdx::Song(basic), {}, resampled), pb(mdx::Song(basic), {}, resampled);
        require(render(pa, 1) == render(pb, 4096), "render must be independent of buffer size");
        ++passed;
        {
            auto compare=[&](const Bytes& bytes,mdx::Options options) {
                mdx::Player sequential(mdx::Song(bytes),{},options);
                auto expected=render(sequential,257);
                auto parallel=mdx::makeOfflineSource(mdx::Song(bytes),{},options);
                std::vector<int16_t> actual;std::array<int16_t,8192> block;
                while(auto n=parallel->render(block.data(),4096)) actual.insert(actual.end(),block.begin(),block.begin()+n*2);
                require(actual==expected,"batched threaded WAV differs from ordinary renderer");
            };
            auto options=resampled;options.resampleOutput=true;options.loops=2;options.maxSeconds=0;options.fadeSeconds=.05;
            compare(basic,options);
            // A one-track DLL extraction must match the full mix, including pan and SRC.
            auto full=mdx::makeOfflineSource(mdx::Song(basic),{},options);
            auto selectedOptions=options; selectedOptions.offlineChannel=0;
            auto selected=mdx::makeOfflineSource(mdx::Song(basic),{},selectedOptions);
            std::array<int16_t,8192> mix{},stem{};
            for (;;) {auto n=full->render(mix.data(),4096);auto m=selected->render(stem.data(),4096);
                require(n==m && std::equal(mix.begin(),mix.begin()+n*2,stem.begin()),"DLL channel WAV differs from one-track mix");if(!n)break;}
            options.dacRightDelay=true;compare(basic,options);
            options.measuredOutputTiming=true;compare(basic,options);
            compare(oneTrack({253,0,180,9,182,9,241,255,247}),options);
            ++passed;
        }
        {
            auto state=std::make_shared<mdx::VoiceState>();
            state->defined[0]=true;state->definitions[0]=mdx::Song(basic).voices[0];
            state->voices[0]=state->definitions[0];state->valid[0]=true;
            auto queue=std::make_shared<mdx::MidiQueue>();
            std::array<int,256> written{};
            mdx::MidiSynth synth(state,queue,48000,true,[&](int reg,int value){written[reg]=value;});
            synth.setPolyphonic(true);
            queue->push(0xf0);queue->push(0x90|(60<<8)|(100<<16));queue->push(0x90|(64<<8)|(100<<16));
            std::array<int16_t,256> audio{};synth.mix(audio.data(),128);
            state->definitions[0][1]=(state->definitions[0][1]&~7)|4;++state->catalogRevision;
            queue->push(0xf0);synth.mix(audio.data(),128);
            require((written[0x20]&7)==4 && (written[0x21]&7)==4,"edited voice did not update all held MIDI slots");
            ++passed;
        }
        {
            mdx::VoiceState state;
            auto initial=mdx::Song(basic).voices;
            std::array<bool,256> present{};present[0]=true;present[1]=true;initial[1]=initial[0];
            state.loadDefinitions(initial,present);
            require(!state.modified(0),"new catalogue should be unchanged");
            state.definitions[0][7]=13;state.definitions[1][7]=17;
            require(state.initialDefinitions[0][7]!=13,"playback edits mutated initial voices");
            require(state.modified(0)&&state.modified(1),"per-instrument changes were lost");
            state.loadDefinitions(initial,present);
            require(state.definitions[0][7]==13&&state.definitions[1][7]==17,"recompile dropped unsaved edits");
            initial[0]=state.definitions[0];state.loadDefinitions(initial,present);
            require(!state.modified(0)&&state.modified(1),"saving one voice affected another unsaved voice");
            state.definitions[1]=state.initialDefinitions[1];
            require(!state.modified(1),"reset should equal saved initial voice");
            auto values=mdx::voiceParameterValues(initial[0]);
            require(values[5]==13&&values[44]==(initial[0][1]&7),"parameter-to-knob order differs from MML");
            ++passed;
        }
        {
            auto state=std::make_shared<mdx::VoiceState>();
            auto song=mdx::Song(basic);state->loadDefinitions(song.voices,song.hasVoice);
            state->definitions[0][1]=(state->definitions[0][1]&~7)|4;
            auto live=opts;live.voiceState=state;
            mdx::Player player(mdx::Song(oneTrack({253,0,180,3,241,0})),{},live);
            int alg=-1;player.registerTrace=[&](uint64_t,int reg,int value){if(reg==0x20)alg=value&7;};
            std::array<int16_t,512> audio{};player.render(audio.data(),256);
            require(alg==4,"new track voice command did not use edited playback catalogue");
            require((state->initialDefinitions[0][1]&7)!=(state->definitions[0][1]&7),"player overwrote initial definition");
            ++passed;
        }
        // Tie the first note into the second; exactly one hardware key-on.
        mdx::Player tied(mdx::Song(oneTrack({253, 0, 247, 180, 3, 182, 3, 241, 0})), {}, opts);
        int keys = 0;
        tied.registerTrace = [&](uint64_t, int r, int v) {
            if (r == 8 && (v & 120) && !(v & 7))
                ++keys;
        };
        render(tied);
        require(keys == 1, "tie retriggers key-on");
        ++passed;
        mdx::Player delayed(mdx::Song(oneTrack({253, 0, 240, 2, 180, 7, 241, 0})), {}, opts);
        uint64_t delayedAt = 0;
        delayed.registerTrace = [&](uint64_t t, int r, int v) {
            if (r == 8 && (v & 120) && !(v & 7))
                delayedAt = t;
        };
        render(delayed);
        require(delayedAt == 1792, "key-on delay must be 2 ticks");
        ++passed;
        // Repeat A,B three times, omitting B in the final iteration.
        Bytes loop{253, 0, 246, 3, 0, 180, 1, 244, 0, 3, 182, 1, 245, 255, 244, 241, 0};
        mdx::Player repeated(mdx::Song(oneTrack(loop)), {}, opts);
        keys = 0;
        repeated.registerTrace = [&](uint64_t, int r, int v) {
            if (r == 8 && (v & 120) && !(v & 7))
                ++keys;
        };
        render(repeated);
        require(keys == 5, "repeat / escape note count");
        ++passed;
        // Tempo changes take effect for the next tick interval.
        mdx::Player tempo(mdx::Song(oneTrack({253, 0, 255, 200, 180, 0, 255, 250, 182, 0, 241, 0})), {},
                          opts);
        std::vector<uint64_t> keyTimes;
        tempo.registerTrace = [&](uint64_t t, int r, int v) {
            if (r == 8 && (v & 120) && !(v & 7))
                keyTimes.push_back(t);
        };
        render(tempo);
        require(keyTimes == std::vector<uint64_t>{0, 897}, "tempo event times");
        ++passed;
        // One blocked track, resumed by another track on tick 1.
        std::vector<Bytes> tracks(9, Bytes{241, 0});
        tracks[0] = {238, 253, 0, 180, 0, 241, 0};
        tracks[1] = {0, 239, 0, 241, 0};
        mdx::Player sync(mdx::Song(songFile(tracks)), {}, opts);
        keys = 0;
        sync.registerTrace = [&](uint64_t, int r, int v) {
            if (r == 8 && (v & 120) && !(v & 7))
                ++keys;
        };
        render(sync);
        require(keys == 1, "sync resume");
        ++passed;
        Bytes pdxData(770, 0);
        setLong(pdxData, 0, 768);
        setLong(pdxData, 4, 2);
        pdxData[768] = 0x10;
        pdxData[769] = 0x88;
        mdx::Pdx pdx(pdxData);
        auto decoded = pdx.decode(0);
        require(decoded == std::vector<int16_t>({32, 128, 96, 64}), "ADPCM nibble order / quantizer vector");
        require(pdx.entries.size() == 96, "PDX slot count");
        ++passed;
        pdxData[768] = 128;
        pdxData[769] = 0;
        mdx::Pdx linear(pdxData);
        require(linear.decode(0, 5) == std::vector<int16_t>{-32768}, "big endian P16");
        require(linear.decode(0, 6) == std::vector<int16_t>({-32768, 0}), "signed P8");
        ++passed;
        auto pcmBank = std::make_shared<mdx::Pdx>(pdxData);
        tracks.assign(16, Bytes{241, 0});
        tracks[0] = {232, 241, 0};
        tracks[8] = {251, 128, 128, 3, 241, 0};
        {
            auto file=mdx::makeVgm(mdx::Song(songFile(tracks,true)),pcmBank);
            require(le32(file,0x70)>0,"PCM VGM declares PWM clock");
            bool audible=false;uint64_t waits=0;size_t writes=0;
            for(size_t p=0x100;p<file.size();) {
                auto c=file[p++];
                if(c==0xb2) {auto reg=file[p]>>4;auto value=((file[p]&15)<<8)|file[p+1];p+=2;if(reg==2||reg==3){++writes;audible|=value!=0;}}
                else if(c==0x54) p+=2;
                else if(c==0x61) {waits+=file[p]|uint16_t(file[p+1])<<8;p+=2;}
                else if(c>=0x70&&c<=0x7f) waits+=1+(c&15);
                else if(c==0x66) break;
                else require(false,"invalid PCM VGM command");
            }
            require(audible&&writes==2*le32(file,0x18)&&waits==le32(file,0x18),"PCM stereo samples and waits preserved");
        }
        mdx::Player pcm(mdx::Song(songFile(tracks, true)), pcmBank, opts);
        auto pcmOutput = render(pcm);
        require(std::any_of(pcmOutput.begin(), pcmOutput.end(), [](int16_t v) { return v != 0; }),
                "PCM mixing is silent");
        ++passed;
        {
            auto options=opts; options.resampleOutput=true; options.sampleRate=48000; options.maxSeconds=0; options.loops=1; options.dacRightDelay=true;
            mdx::Player reference(mdx::Song(songFile(tracks,true)),pcmBank,options);
            auto expected=render(reference,257);
            auto parallel=mdx::makeOfflineSource(mdx::Song(songFile(tracks,true)),pcmBank,options);
            std::vector<int16_t> actual;std::array<int16_t,8192> block;
            while(auto n=parallel->render(block.data(),4096)) actual.insert(actual.end(),block.begin(),block.begin()+n*2);
            require(actual==expected,"threaded WAV PCM/resampling mismatch");
            options.fmMuteMask=255;
            mdx::Player mutedReference(mdx::Song(songFile(tracks,true)),pcmBank,options);
            expected=render(mutedReference,257);actual.clear();
            parallel=mdx::makeOfflineSource(mdx::Song(songFile(tracks,true)),pcmBank,options);
            while(auto n=parallel->render(block.data(),4096)) actual.insert(actual.end(),block.begin(),block.begin()+n*2);
            require(actual==expected,"PCM-only WAV fast path differs from ordinary renderer");
            ++passed;
        }
        // Every PCM mask bit gates only its channel, with no sample restart.
        {
            Bytes data(768 + 4096, 0);
            setLong(data, 0, 768);
            setLong(data, 4, 4096);
            for (size_t i = 768; i < data.size(); ++i)
                data[i] = static_cast<uint8_t>(1 + (i % 61));
            auto bank = std::make_shared<mdx::Pdx>(data);
            for (int rate : {62500, 48000})
                for (int ch = 0; ch < 8; ++ch) {
                    auto o = opts;
                    o.sampleRate = rate;
                    o.resampleOutput = true;
                    o.maxSeconds = .2;
                    std::vector<Bytes> commands(16, Bytes{241, 0});
                    commands[0] = {232, 241, 0};
                    commands[8 + ch] = {237, 6, 251, 8, 128, 127, 241, 0};
                    auto song = songFile(commands, true);
                    auto mask = std::make_shared<std::atomic<uint32_t>>(0);
                    auto live = o;
                    live.livePcmMuteMask = mask;
                    live.monitor = std::make_shared<mdx::Monitor>();
                    mdx::Player player(mdx::Song(song), bank, live), reference(mdx::Song(song), bank, o);
                    std::vector<int16_t> a(2048), b(2048);
                    for (int step = 0; step < 4; ++step) {
                        mask->store(step == 1 ? (1u << ch) : step == 3 ? (1u << ((ch + 1) % 8)) : 0);
                        require(player.render(a.data(), 1024) == 1024 &&
                                    reference.render(b.data(), 1024) == 1024,
                                "PCM mask render duration");
                        if (step == 1)
                            require(std::all_of(a.begin() + 32, a.end(), [](int16_t v) { return v == 0; }),
                                    "PCM mask bit failed");
                        else
                            require(std::equal(a.begin() + 32, a.end(), b.begin() + 32),
                                    "PCM unmute restarted or wrong channel muted");
                    }
                    require(std::any_of(live.monitor->frame.waveform[8 + ch].begin(),
                                        live.monitor->frame.waveform[8 + ch].end(),
                                        [](float v) { return v != 0; }),
                            "PCM per-track waveform missing");
                    for (int other = 0; other < 16; ++other)
                        if (other != 8 + ch)
                            require(std::all_of(live.monitor->frame.waveform[other].begin(),
                                                live.monitor->frame.waveform[other].end(),
                                                [](float v) { return v == 0; }),
                                    "PCM waveform channel isolation");
                    o.pcmMuteMask = 255;
                    mdx::Player muted(mdx::Song(song), bank, o);
                    auto silent = render(muted);
                    require(std::all_of(silent.begin(), silent.end(), [](int16_t v) { return v == 0; }),
                            "PCM export snapshot mask failed");
                }
            auto o = opts;
            o.pcmMuteMask = 255;
            mdx::Player fm1(mdx::Song(basic), {}, o), fm2(mdx::Song(basic), {}, opts);
            require(render(fm1) == render(fm2), "PCM mask changed FM output");
            o.pcmMuteMask = 256;
            rejects([&] { mdx::Player invalid(mdx::Song(basic), {}, o); }, "invalid PCM mask accepted");
        }
        ++passed;
        // Absolute frequency regression: X68000 4 MHz clock, MDX A3/A4.
        for (auto [note, target] : {std::pair<int, double>{42, 220}, {54, 440}}) {
            auto o = opts;
            o.maxSeconds = 1.25;
            mdx::Player tone(
                mdx::Song(oneTrack({253, 0, 251, 128, static_cast<uint8_t>(128 + note), 239, 241, 0})), {},
                o);
            auto a = render(tone);
            std::vector<double> crossings;
            for (size_t i = 5001; i < a.size() / 2; ++i)
                if (a[2 * (i - 1)] <= 0 && a[2 * i] > 0)
                    crossings.push_back(i - 1 + double(-a[2 * (i - 1)]) / double(a[2 * i] - a[2 * (i - 1)]));
            require(crossings.size() > 100, "frequency signal too short");
            double hz = 62500.0 * (crossings.size() - 1) / (crossings.back() - crossings.front());
            std::cout << "FM note " << note << ": " << hz << " Hz\n";
            require(std::abs(hz - target) < 1.0, "absolute FM pitch regression");
        }
        ++passed;
        for (int vol : {6, 8}) {
            auto o = opts;
            o.gain = 1;
            tracks[8] = {251, static_cast<uint8_t>(vol), 128, 3, 241, 0};
            mdx::Player p(mdx::Song(songFile(tracks, true)), pcmBank, o);
            auto a = render(p);
            double expected = pcmBank->decode(0)[0] * (vol == 8 ? 1.0 : std::pow(1.1, -5));
            require(std::abs(a[0] - expected) <= 1, "PCM coarse volume scaling");
            o.pcmGain = 0;
            mdx::Player muted(mdx::Song(songFile(tracks, true)), pcmBank, o);
            auto silent = render(muted);
            require(std::all_of(silent.begin(), silent.end(), [](int16_t v) { return v == 0; }),
                    "PCM trim mute");
        }
        ++passed;
        {
            auto dir = std::filesystem::temp_directory_path() /
                       std::filesystem::path(
                           "independent-pdx-test-" +
                           std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
            std::filesystem::create_directory(dir);
            struct Cleanup {
                std::filesystem::path p;
                ~Cleanup() {
                    std::error_code e;
                    std::filesystem::remove_all(p, e);
                }
            } cleanup{dir};
            auto write = [&](const char *name) {
                std::ofstream f(dir / name, std::ios::binary);
                f.write(reinterpret_cast<const char *>(pdxData.data()), pdxData.size());
            };
            auto song = mdx::Song(basic);
            song.pdxName = "B8.pdx";
            write("B8(2).pdx");
            require(mdx::locatePdx(song, dir / "editor.mdx")->sourcePath.filename() == "B8(2).pdx",
                    "numbered PDX resolution");
            write("B8 (3).pdx");
            rejects([&] { mdx::locatePdx(song, dir / "editor.mdx"); }, "ambiguous numbered PDX accepted");
            write("B8.pdx");
            require(mdx::locatePdx(song, dir / "editor.mdx")->sourcePath.filename() == "B8.pdx",
                    "exact PDX must win");
        }
        ++passed;
        rejects([] { mdx::Song s(Bytes{1, 2, 3}); }, "missing header accepted");
        rejects(
            [&] {
                auto b = pdxData;
                setLong(b, 4, 999999);
                mdx::Pdx p(b);
            },
            "bad PDX offset accepted");
        rejects(
            [&] {
                mdx::Player p(mdx::Song(oneTrack({231, 2})), {}, opts);
                render(p);
            },
            "unsupported extension silently accepted");
        rejects(
            [&] {
                mdx::Player p(mdx::Song(oneTrack({238})), {}, opts);
                render(p);
            },
            "sync deadlock accepted");
        rejects(
            [&] {
                mdx::Player p(mdx::Song(oneTrack({241, 255, 253})), {}, opts);
                render(p);
            },
            "zero-time cycle accepted");
        ++passed;
        // Infinite music loops stop at the requested barrier, not one channel early.
        mdx::Options loops = opts;
        loops.loops = 2;
        mdx::Player infinite(mdx::Song(oneTrack({253, 0, 180, 1, 241, 255, 251})), {}, loops);
        auto infinityAudio = render(infinite);
        require(infinityAudio.size() > 7000 && infinityAudio.size() < 8000, "infinite-loop count stop time");
        ++passed;
        {
            // Full-length q8 repeats must have a sampled OFF edge, unlike ties.
            auto bytes = oneTrack({253, 0, 251, 128, 182, 7, 182, 7, 241, 0});
            auto song = mdx::Song(bytes);
            song.voices[0][23] = 255; // SL15 / RR15, allowing D1 decay to be heard.
            song.voices[0][15] = 24;  // Fast-decaying carrier exposes missed retriggers.
            mdx::Player p(song, {}, opts);
            std::vector<std::pair<uint64_t, int>> events;
            p.registerTrace = [&](uint64_t t, int r, int v) {
                if (r == 8)
                    events.emplace_back(t, v);
            };
            auto a = render(p);
            require(std::find(events.begin(), events.end(), std::pair<uint64_t, int>{7168, 0}) !=
                        events.end(),
                    "q8 key off missing");
            require(std::find(events.begin(), events.end(), std::pair<uint64_t, int>{7169, 8}) !=
                        events.end(),
                    "q8 key on needs one sampled OFF");
            auto energy = [&](size_t start) {
                double sum = 0;
                for (size_t i = start; i < start + 1000; ++i)
                    sum += double(a[2 * i]) * a[2 * i];
                return sum;
            };
            require(energy(7169) > energy(6000) * 10, "decaying voice did not reattack");
        }
        ++passed;
        {
            // Pan-output masking must preserve phase/envelopes in the core.
            {
                auto o = opts;
                o.maxSeconds = 1;
                auto mask = std::make_shared<std::atomic<uint32_t>>(0);
                auto live = o;
                live.liveMuteMask = mask;
                auto bytes = oneTrack({253, 0, 251, 128, 182, 239, 241, 0});
                mdx::Player p(mdx::Song(bytes), {}, live), ref(mdx::Song(bytes), {}, o);
                std::vector<int16_t> a(4096), b(4096);
                p.render(a.data(), 2048);
                ref.render(b.data(), 2048);
                require(a == b, "unmasked changed");
                mask->store(1);
                p.render(a.data(), 2048);
                ref.render(b.data(), 2048);
                require(std::all_of(a.begin() + 128, a.end(), [](int16_t v) { return v == 0; }),
                        "A mask not silent after DLL FIR settles");
                mask->store(0);
                p.render(a.data(), 2048);
                ref.render(b.data(), 2048);
                require(std::equal(a.begin() + 128, a.end(), b.begin() + 128),
                        "unmask lost synthesis state after DLL FIR settles");
                mask->store(2);
                p.render(a.data(), 2048);
                ref.render(b.data(), 2048);
                require(a == b, "B mask affected A");
            }
        }
        ++passed;
        {
            // Byte-level relative command direction, independently specified.
            mdx::Player p(
                mdx::Song(oneTrack({253, 0, 251, 8, 182, 0, 250, 0, 249, 0, 251, 148, 0, 250, 0, 249, 0, 241, 0})),
                {}, opts);
            std::vector<int> tl;
            p.registerTrace = [&](uint64_t, int r, int v) {
                if (r == 0x60)
                    tl.push_back(v);
            };
            render(p);
            require(tl == std::vector<int>({0, 21, 21, 24, 21, 20, 21, 20}),
                    "FA/F9 direction or coarse TL table");
        }
        ++passed;
        {
            const int tl[] = {42, 40, 37, 34, 32, 29, 26, 24, 21, 18, 16, 13, 10, 8, 5, 2};
            auto pcmRender = [&](int volume, int relative = -1) {
                std::vector<Bytes> tr(16, Bytes{241, 0});
                tr[0] = {232, 241, 0};
                tr[8] = {251, static_cast<uint8_t>(volume)};
                if (relative >= 0)
                    tr[8].push_back(static_cast<uint8_t>(relative));
                tr[8].insert(tr[8].end(), {128, 3, 241, 0});
                mdx::Player p(mdx::Song(songFile(tr, true)), pcmBank, opts);
                return render(p);
            };
            for (int v = 0; v < 16; ++v)
                require(pcmRender(v) == pcmRender(128 + tl[v]), "PCM coarse/fine volume disagree");
            require(pcmRender(8, 250) == pcmRender(7), "PCM FA must decrease");
            require(pcmRender(8, 249) == pcmRender(9), "PCM F9 must increase");
            auto mute = pcmRender(128 + 43);
            require(std::all_of(mute.begin(), mute.end(), [](int16_t v) { return v == 0; }),
                    "PCM attenuation >=43 must mute");
        }
        ++passed;
        {
            // A DC onset measures the exact sub-sample R delay at 48 kHz:
            // first frame spans 20.833 us; R is absent for the first 7 us.
            for (bool pcmOnly : {false, true}) {
                mdx::Clock64Output stage;
                int index = 0;
                int16_t data[8];
                auto next = [&](mdx::AudioStem &f) {
                    if (index++ >= 100)
                        return false;
                    if (pcmOnly)
                        f.pcm = {10000, 10000};
                    else
                        f.fm = {10000, 10000};
                    return true;
                };
                require(stage.render(data, 4, 48000, true, next) == 4, "DAC output missing");
                require(data[0] == 10000 && data[1] == (pcmOnly ? 10000 : 6640),
                        "R delay is not exactly 28 master clocks");
                require(data[2] == 10000 && data[3] == 10000, "DAC DC not settled");
            }
            auto o = opts;
            o.resampleOutput = true;
            o.dacRightDelay = true;
            o.sampleRate = 48000;
            mdx::Player a(mdx::Song(basic), {}, o), b(mdx::Song(basic), {}, o);
            require(render(a, 1) == render(b, 4096), "64-stage depends on buffer size");
            o.maxSeconds = .1;
            o.fadeSeconds = 0;
            mdx::Player timed(mdx::Song(oneTrack({253, 0, 182, 239, 241, 0})), {}, o);
            require(render(timed).size() == 9600, "resampleOutput exact duration");
            o.maxSeconds = 1;
            o.dacRightDelay = false;
            mdx::Player tone(mdx::Song(oneTrack({253, 0, 251, 128, 182, 239, 241, 0})), {}, o);
            auto signal = render(tone);
            std::vector<double> crosses;
            for (size_t i = 5001; i < signal.size() / 2; ++i)
                if (signal[2 * (i - 1)] <= 0 && signal[2 * i] > 0)
                    crosses.push_back(
                        i - 1 + double(-signal[2 * (i - 1)]) / double(signal[2 * i] - signal[2 * (i - 1)]));
            require(crosses.size() > 100, "64-stage tone missing");
            double hz = 48000 * (crosses.size() - 1) / (crosses.back() - crosses.front());
            require(std::abs(hz - 440) < 1, "64-stage changed FM pitch");
        }
        ++passed;
        {
            // Hold notes across OFF/ON so stale register values are audible.
            auto bytes = oneTrack({253, 0,   236, 0, 0,   100, 64,  0,   247, 182, 3, 236,
                                   128, 247, 182, 3, 236, 129, 247, 182, 3,   241, 0});
            mdx::Player p(mdx::Song(bytes), {}, opts);
            std::vector<std::pair<uint64_t, int>> keys;
            p.registerTrace = [&](uint64_t t, int r, int v) {
                if (r == 0x28)
                    keys.emplace_back(t, v);
            };
            render(p);
            bool restored = false, restarted = false;
            for (auto [t, v] : keys) {
                if (t == 3584 && v == 0x48)
                    restored = true;
                if (t > 3584 && t < 7168)
                    require(v == 0x48, "MPOF retained pitch modulation");
                if (t > 7168 && v != 0x48)
                    restarted = true;
            }
            require(restored && restarted, "EC80 OFF / EC81 ON does not restore and restart pitch");
        }
        ++passed;
        {
            // Measure steady-state phase, not only the first step sample.
            mdx::Clock64Output stage;
            int index = 0;
            auto next = [&](mdx::AudioStem &f) {
                if (index >= 62500)
                    return false;
                double x = 10000 * std::sin(2 * 3.141592653589793 * 1000 * index++ / 62500.0);
                f.fm = {x, x};
                return true;
            };
            std::vector<int16_t> a(96000);
            require(stage.render(a.data(), 48000, 48000, true, next) == 48000, "delay tone duration");
            double ls = 0, lc = 0, rs = 0, rc = 0;
            for (int i = 4800; i < 43200; ++i) {
                double theta = 2 * 3.141592653589793 * 1000 * i / 48000.0;
                ls += a[2 * i] * std::sin(theta);
                lc += a[2 * i] * std::cos(theta);
                rs += a[2 * i + 1] * std::sin(theta);
                rc += a[2 * i + 1] * std::cos(theta);
            }
            double delay = (std::atan2(lc, ls) - std::atan2(rc, rs)) / (2 * 3.141592653589793 * 1000);
            std::cout << "R delay: " << delay * 1e6 << " us / " << delay * 48000 << " output samples\n";
            require(std::abs(delay - 7e-6) < .05e-6, "fractional R delay missing from 48 kHz waveform");
        }
        ++passed;
        {
            // Exercise the actual player at 48 kHz, including live OFF/ON/OFF.
            auto bytes = oneTrack({253, 0, 251, 128, 247, 182, 127, 241, 0});
            // ALG7 with C1 carrier (MDX OP mask bit 1, parameter slot 2): L-only delay.
            bytes[bytes.size() - 27 + 2] = 2;
            bytes[bytes.size() - 27 + 7] = 127;
            bytes[bytes.size() - 27 + 9] = 0;
            for (bool dac : {false, true}) {
                auto o = opts;
                o.resampleOutput = true;
                o.sampleRate = 48000;
                o.dacRightDelay = dac;
                mdx::Player dry(mdx::Song(bytes), {}, o);
                o.measuredOutputTiming = true;
                mdx::Player wet(mdx::Song(bytes), {}, o);
                o.measuredOutputTiming = false;
                o.liveMeasuredOutputTiming = std::make_shared<std::atomic<bool>>(false);
                mdx::Player live(mdx::Song(bytes), {}, o);
                std::vector<int16_t> a(4096), b(4096), c(4096);
                for (int step = 0; step < 3; ++step) {
                    o.liveMeasuredOutputTiming->store(step == 1);
                    require(dry.render(a.data(), 2048) == 2048 && wet.render(b.data(), 2048) == 2048 &&
                                live.render(c.data(), 2048) == 2048,
                            "timing toggle duration");
                    auto &expected = step == 1 ? b : a;
                    require(std::equal(c.begin() + 128, c.end(), expected.begin() + 128),
                            "live timing toggle restarts or ignores mode");
                    require(a != b, "measured mode has no waveform effect");
                }
            }
        }
        ++passed;
        {
            auto o = opts;
            o.monitor = std::make_shared<mdx::Monitor>();
            mdx::Player observed(mdx::Song(basic), {}, o), plain(mdx::Song(basic), {}, opts);
            std::vector<int16_t> a(4096), b(4096);
            require(observed.render(a.data(), 2048) == 2048 && plain.render(b.data(), 2048) == 2048,
                    "monitor render length");
            require(a == b, "monitor changes audio");
            require(o.monitor->frame.note[0] == 54, "FM keyboard note mapping");
            require(std::any_of(o.monitor->frame.waveform[0].begin(), o.monitor->frame.waveform[0].end(),
                                [](float v) { return v != 0; }),
                    "FM channel monitor missing");
            for (int ch = 1; ch < 16; ++ch)
                require(std::all_of(o.monitor->frame.waveform[ch].begin(),
                                    o.monitor->frame.waveform[ch].end(), [](float v) { return v == 0; }),
                        "monitor leaks across tracks");
            require(render(observed) == render(plain), "monitor changes later audio");
            Bytes data(768 + 4096, 64);
            std::fill(data.begin(), data.begin() + 768, 0);
            setLong(data, 0, 768);
            setLong(data, 4, 4096);
            mdx::Pdx bank(data);
            o.monitor = std::make_shared<mdx::Monitor>();
            o.livePcmMuteMask = std::make_shared<std::atomic<uint32_t>>(1);
            mdx::SamplePlayer sample(bank, 0, 6, o), reference(bank, 0, 6, opts);
            sample.render(a.data(), 2048);
            reference.render(b.data(), 2048);
            require(std::all_of(a.begin(), a.end(), [](int16_t v) { return v == 0; }),
                    "PDX P mask not applied");
            require(sample.seconds() == reference.seconds(), "muted PDX time stopped");
            o.livePcmMuteMask->store(0);
            sample.render(a.data(), 2048);
            reference.render(b.data(), 2048);
            require(a == b && std::any_of(a.begin(), a.end(), [](int16_t v) { return v != 0; }),
                    "PDX unmute changed position");
            require(o.monitor->frame.note[8] == 0, "PDX monitor mapping");
            require(std::any_of(o.monitor->frame.waveform[8].begin(), o.monitor->frame.waveform[8].end(),
                                [](float v) { return v != 0; }),
                    "PDX waveform missing");
            for (int ch = 0; ch < 16; ++ch)
                if (ch != 8)
                    require(std::all_of(o.monitor->frame.waveform[ch].begin(),
                                        o.monitor->frame.waveform[ch].end(), [](float v) { return v == 0; }),
                            "PDX monitor leaks");
        }
        ++passed;
        {
            // +4 selects the identical waveform with 256x depth, including type 3.
            for (int wave = 0; wave < 4; ++wave) {
                auto song = [&](int mode, int depth) {
                    return oneTrack({253, 0, 236, uint8_t(mode), 0, 8, uint8_t(depth >> 8), uint8_t(depth),
                                     247, 182, 127, 241, 0});
                };
                auto o = opts;
                o.maxSeconds = .15;
                mdx::Player a(mdx::Song(song(wave, 256)), {}, o), b(mdx::Song(song(wave + 4, 1)), {}, o);
                std::vector<std::pair<int, int>> x, y;
                a.registerTrace = [&](uint64_t, int r, int v) {
                    if (r == 0x28 || r == 0x30)
                        x.emplace_back(r, v);
                };
                b.registerTrace = [&](uint64_t, int r, int v) {
                    if (r == 0x28 || r == 0x30)
                        y.emplace_back(r, v);
                };
                require(render(a) == render(b) && x == y, "LFO +4 is not 256x depth");
            }
            for(int q:{0,9,76,112,126,127}) {
                mdx::Player valid(mdx::Song(oneTrack({248,uint8_t(q),182,3,241,0})),{},opts);
                render(valid);
            }

        }
        ++passed;
        {
            auto shift = std::make_shared<std::atomic<int>>(0);
            auto o = opts;
            o.liveKeyShift = shift;
            mdx::Player p(mdx::Song(oneTrack({253, 0, 247, 182, 127, 241, 0})), {}, o);
            std::vector<int16_t> audio(2048);
            int kc = -1, keys = 0;
            p.registerTrace = [&](uint64_t, int r, int v) {
                if (r == 0x28)
                    kc = v;
                if (r == 8 && (v & 120))
                    ++keys;
            };
            p.render(audio.data(), 1024);
            require(kc == 0x48 && keys == 1, "initial transposition");
            shift->store(12);
            p.render(audio.data(), 1024);
            require(kc == 0x58 && keys == 1, "live +12 restarts note or fails");
            shift->store(-24);
            p.render(audio.data(), 1024);
            require(kc == 0x28 && keys == 1, "live -24 fails");
            o.liveKeyShift.reset();
            o.keyShift = 25;
            rejects([&] { mdx::Player bad(mdx::Song(basic), {}, o); }, "out-of-range key accepted");
            Bytes data(768 + 2048, 64);
            std::fill(data.begin(), data.begin() + 768, 0);
            setLong(data, 0, 768);
            setLong(data, 4, 2048);
            mdx::Pdx bank(data);
            o = opts;
            mdx::SamplePlayer normal(bank, 0, 6, o);
            o.keyShift = 12;
            mdx::SamplePlayer high(bank, 0, 6, o);
            size_t n = 0, h = 0;
            for (size_t count; (count = normal.render(audio.data(), 1024));)
                n += count;
            for (size_t count; (count = high.render(audio.data(), 1024));)
                h += count;
            require(n == h * 2, "PCM octave does not double playback rate");
        }
        ++passed;
        {
            std::vector<Bytes> commands(9, Bytes{241, 0});
            commands[0] = {253, 0, 182, 9, 241, 0};
            commands[8] = {128, 9, 241, 0};
            auto o = opts;
            std::vector<std::string> messages;
            o.diagnostic = [&](const std::string &m) { messages.push_back(m); };
            mdx::Player withoutBank(mdx::Song(songFile(commands, true)), {}, o);
            auto a = render(withoutBank);
            require(!messages.empty() && std::any_of(a.begin(), a.end(), [](int16_t v) { return v != 0; }),
                    "missing PDX stopped FM or hid error");
            commands[0] = {253, 99, 182, 9, 241, 0};
            commands[1] = {253, 0, 182, 9, 241, 0};
            commands[8] = {241, 0};
            messages.clear();
            mdx::Player withoutVoice(mdx::Song(songFile(commands)), {}, o);
            bool wrongKey = false;
            withoutVoice.registerTrace = [&](uint64_t, int r, int v) {
                if (r == 8 && (v & 7) == 0 && (v & 120))
                    wrongKey = true;
            };
            a = render(withoutVoice);
            require(!wrongKey && !messages.empty() && messages[0].find("value=99") != std::string::npos &&
                        std::any_of(a.begin(), a.end(), [](int16_t v) { return v != 0; }),
                    "missing voice stopped other FM or keyed stale instrument");
        }
        ++passed;
        {
            // Infinite means unlimited loop markers and no implicit 600-second stop.
            auto loop = oneTrack({253, 0, 182, 0, 241, 255, 251});
            auto o = opts;
            o.loops = 0;
            o.maxSeconds = 0;
            mdx::Player infinite(mdx::Song(loop), {}, o);
            std::vector<int16_t> audio(4096);
            for (int i = 0; i < 80; ++i)
                require(infinite.render(audio.data(), 2048) == 2048, "infinite loop terminated");
            o.loops = 2;
            mdx::Player finite(mdx::Song(loop), {}, o);
            require(render(finite).size() < 327680, "finite loop ignored count");
            o.loops = 0;
            mdx::Player natural(mdx::Song(basic), {}, o);
            require(!render(natural).empty(), "finite song without loop marker cannot end in infinite mode");
        }
        ++passed;
        {
            // P16 sine fixture: measure actual output pitch and unchanged duration.
            const int length = 15625;
            Bytes data(768 + length * 2, 0);
            setLong(data, 0, 768);
            setLong(data, 4, length * 2);
            for (int i = 0; i < length; ++i)
                setWord(data, 768 + i * 2,
                        uint16_t(int16_t(12000 * std::sin(2 * 3.141592653589793 * 440 * i / 15625))));
            mdx::Pdx bank(data);
            auto collect = [&](mdx::Options o, int block) {
                mdx::SamplePlayer player(bank, 0, 5, o);
                std::vector<int16_t> output, buffer(block * 2);
                for (size_t n; (n = player.render(buffer.data(), block));)
                    output.insert(output.end(), buffer.begin(), buffer.begin() + n * 2);
                return output;
            };
            auto o = opts;
            o.maxSeconds = 0;
            o.keyMode = 0;
            auto original = collect(o, 257);
            o.keyShift = 12;
            require(collect(o, 4096) == original, "FM-only mode changed PCM");
            o.keyMode = 2;
            o.keyShift = 0;
            require(collect(o, 127) == original, "zero pitch shift is not exact bypass");
            for (int shift : {-24, -12, 12, 24}) {
                o.keyShift = shift;
                auto output = collect(o, 257);
                require(output.size() == original.size(), "pitch shift changed sample duration");
                require(output == collect(o, 4096), "pitch shift depends on render block size");
                int begin = o.sampleRate / 4, end = o.sampleRate * 3 / 4;
                double expected = 440 * std::exp2(shift / 12.0), best = 0, measured = 0;
                for (int hz = int(expected * .75); hz <= int(expected * 1.25); ++hz) {
                    std::complex<double> sum{}, phase(1, 0);
                    auto step = std::polar(1.0, -2 * 3.141592653589793 * hz / o.sampleRate);
                    for (int i = begin; i < end; ++i) {
                        double window =
                            .5 - .5 * std::cos(2 * 3.141592653589793 * (i - begin) / (end - begin));
                        sum += double(output[i * 2]) * window * phase;
                        phase *= step;
                    }
                    if (std::norm(sum) > best) {
                        best = std::norm(sum);
                        measured = hz;
                    }
                }
                std::cout << "PCM shift " << shift << ": " << measured << " Hz\n";
                require(std::abs(measured - 440 * std::exp2(shift / 12.0)) < 10,
                        "duration-preserving pitch frequency incorrect");
            }
            o.liveKeyMode = std::make_shared<std::atomic<int>>(2);
            o.liveKeyShift = std::make_shared<std::atomic<int>>(12);
            mdx::SamplePlayer live(bank, 0, 5, o);
            std::vector<int16_t> audio(1024 * 2);
            size_t total = live.render(audio.data(), 1024);
            o.liveKeyMode->store(0);
            total += live.render(audio.data(), 1024);
            o.liveKeyMode->store(2);
            o.liveKeyShift->store(-12);
            for (size_t n; (n = live.render(audio.data(), 1024));)
                total += n;
            require(total * 2 == original.size(), "live mode changes reset PCM cursor");
            o.keyMode = 3;
            rejects([&] { mdx::SamplePlayer bad(bank, 0, 5, o); }, "invalid key mode accepted");
        }
        ++passed;
        {
            mdx::Song original(basic);
            auto v = original.voices[0];
            v[8] = 23;
            v[9] = 57; // Distinct middle slots detect OP2/OP3 reversal.
            Bytes commands{254, 0x20, uint8_t(v[1] | 192)};
            for (int g = 0; g < 6; ++g)
                for (int op = 0; op < 4; ++op) {
                    commands.push_back(254);
                    commands.push_back(uint8_t(0x40 + g * 32 + op * 8));
                    commands.push_back(v[3 + g * 4 + op]);
                }
            commands.insert(commands.end(), {182, 20, 241, 0});
            auto o = opts;
            o.voiceState = std::make_shared<mdx::VoiceState>();
            mdx::Player direct(mdx::Song(oneTrack(commands)), {}, o);
            auto audio = render(direct);
            require(std::any_of(audio.begin(), audio.end(), [](int16_t x) { return std::abs(int(x)) > 50; }),
                    "y-only voice did not play a note");
            require(o.voiceState->valid[0] && o.voiceState->voices[0][3] == v[3], "y voice not retained");
            auto mml = mdx::voiceMml(v, 17);
            require(mml.find("@17 = {") == 0 &&
                        mml.find("AR, DR, SR, RR, SL, OL, KS, ML, DT1, DT2, AME") != std::string::npos,
                    "MML instrument field order");
            std::vector<int> values;
            std::istringstream lines(mml);
            std::string line;
            while (std::getline(lines, line))
                if (line.size() > 1 && line[0] == ' ') {
                    for (char &c : line)
                        if (c == ',')
                            c = ' ';
                    std::istringstream nums(line);
                    int n;
                    while (nums >> n)
                        values.push_back(n);
                }
            require(values.size() == 47, "MML parameter count");
            for (int op = 0; op < 4; ++op) {
                constexpr int order[] = {0, 2, 1, 3};
                auto x = &values[order[op] * 11];
                require(((x[8] << 4) | x[7]) == v[3 + op] && x[5] == v[7 + op] &&
                            ((x[6] << 6) | x[0]) == v[11 + op] && ((x[10] << 7) | x[1]) == v[15 + op] &&
                            ((x[9] << 6) | x[2]) == v[19 + op] && ((x[4] << 4) | x[3]) == v[23 + op],
                        "MML round trip changed instrument");
            }
        }
        ++passed;
        {
            auto song = oneTrack({253, 0, 182, 20, 241, 0});
            mdx::Player seeker(mdx::Song(song), {}, opts), reference(mdx::Song(song), {}, opts);
            auto timeline = seeker.analyze();
            require(timeline.duration > .5 && timeline.loopEnd == 0 && !timeline.truncated,
                    "finite timeline");
            std::vector<int16_t> discarded((opts.sampleRate / 10) * 2), a(2048 * 2), b(2048 * 2);
            require(reference.render(discarded.data(), opts.sampleRate / 10) == size_t(opts.sampleRate / 10),
                    "seek reference duration");
            require(seeker.seek(.1), "seek failed");
            require(seeker.render(a.data(), 2048) == reference.render(b.data(), 2048) && a == b,
                    "seek state differs from continuous synthesis");
            auto loop = oneTrack({253, 0, 182, 2, 241, 255, 251});
            auto o = opts;
            o.loops = 0;
            o.maxSeconds = 0;
            mdx::Player repeating(mdx::Song(loop), {}, o);
            auto t = repeating.analyze();
            require(t.loopEnd > t.loopStart && t.channel == 0, "loop timeline missing");
            repeating.seek(t.loopEnd + .01);
            require(std::abs(repeating.position(t) - (t.loopStart + .01)) < .001,
                    "slider did not wrap to loop start");
            require(repeating.analyze([] { return true; }).duration == 0, "analysis cancellation ignored");
        }
        ++passed;
        {
            auto state = std::make_shared<mdx::VoiceState>();
            auto queue = std::make_shared<mdx::MidiQueue>();
            mdx::Song song(basic);
            state->voices[0] = song.voices[0];
            state->valid[0] = true;
            mdx::MidiSynth synth(state, queue, 48000);
            auto energy = [&] {
                std::vector<int16_t> audio(8192);
                synth.mix(audio.data(), 4096);
                double sum = 0;
                for (auto v : audio)
                    sum += double(v) * v;
                return sum;
            };
            queue->push(0x90 | (69 << 8) | (127 << 16));
            require(energy() > 1000000, "MIDI retained voice silent");
            queue->push(0xb0 | (7 << 8));
            energy();
            require(energy() < 10000, "MIDI CC7 volume ignored");
            queue->push(0xb0 | (7 << 8) | (127 << 16));
            queue->push(0xe0 | (127 << 8) | (127 << 16));
            require(energy() > 1000000, "MIDI pitch/volume recovery failed");
            require(state->registers[0x28] > 0, "MIDI register snapshot not updated");
            queue->push(0xb0 | (120 << 8));
            energy();
            for (int i = 0; i < 8; ++i)
                energy();
            require(energy() < 10000, "MIDI all-sounds-off stuck note");
        }
        ++passed;
        {
            auto state = std::make_shared<mdx::VoiceState>();
            auto queue = std::make_shared<mdx::MidiQueue>();
            mdx::Song song(basic);
            state->definitions[7] = song.voices[0];
            state->defined[7] = true;
            state->definitions[135] = song.voices[0];
            state->definitions[135][3] = 3;
            state->defined[135] = true;
            state->catalogRevision = 1;
            mdx::MidiSynth synth(state, queue, 48000);
            std::vector<int16_t> audio(2048);
            queue->push(0xc0 | (7 << 8));
            queue->push(0x90 | (69 << 8) | (127 << 16));
            synth.mix(audio.data(), 1024);
            require(state->ids[0] == 7 && state->used[7] && state->registers[0x40] == song.voices[0][3],
                    "Program Change before song playback failed");
            queue->push(0xb0 | (1 << 16));
            queue->push(0xc0 | (7 << 8));
            queue->push(0x90 | (70 << 8) | (127 << 16));
            synth.mix(audio.data(), 1024);
            require(state->ids[0] == 135 && state->registers[0x40] == 3,
                    "MIDI bank 1 program mapping failed");
            queue->push(0xc0 | (8 << 8));
            synth.mix(audio.data(), 1024);
            require(state->ids[0] == 135, "undefined Program Change destroyed retained voice");
        }
        ++passed;
        {
            for (int alg = 0; alg < 8; ++alg) {
                auto graph = mdx::algorithmLayout(alg);
                for (int e = 0; e < graph.edgeCount; ++e)
                    require(graph.level[graph.edges[e][0]] < graph.level[graph.edges[e][1]],
                            "algorithm edge points backward");
                for (int op = 0; op < 4; ++op) {
                    require(graph.row[op] < graph.rows[op], "algorithm row out of range");
                    if (graph.carriers & (1 << op))
                        require(graph.level[op] == graph.maxLevel, "carrier not in final layer");
                }
            }
            auto parallel = mdx::algorithmLayout(7);
            require(parallel.maxLevel == 0 && parallel.rows[0] == 4,
                    "algorithm 7 is not four parallel operators");
            auto serial = mdx::algorithmLayout(0);
            require(serial.level == std::array<int, 4>{0, 1, 2, 3}, "algorithm 0 hierarchy order");
        }
        ++passed;

        {
            // A tied note must preserve both the software LFO phase and MD delay.
            auto o = opts;
            o.maxSeconds = .25;
            o.fadeSeconds = 0;
            auto a = oneTrack({253, 0, 236, 2, 0, 6, 1, 0, 233, 3, 247, 182, 7, 247, 182, 7, 241, 0});
            auto b = oneTrack({253, 0, 236, 2, 0, 6, 1, 0, 233, 3, 247, 182, 15, 241, 0});
            mdx::Player tied(mdx::Song(a), {}, o), continuous(mdx::Song(b), {}, o);
            require(render(tied) == render(continuous), "MD/triangle phase restarted at tied note");
        }
        ++passed;
        {
            // EA80 must remain off across the MD threshold and subsequent notes.
            auto bytes = oneTrack({253, 0,   234, 0, 210, 127, 127, 0x73, 233, 2,   247, 182, 3,   234,
                                   128, 247, 182, 3, 247, 184, 3,   234,  129, 247, 184, 7,   241, 0});
            mdx::Player p(mdx::Song(bytes), {}, opts);
            bool disabled = false, enabled = false;
            p.registerTrace = [&](uint64_t t, int r, int v) {
                if (r == 0x38) {
                    if (t >= 3584 && t < 10752) {
                        require(v == 0, "MHOF re-enabled during note/delay");
                        disabled = true;
                    }
                    if (t >= 10752 && v == 0x73)
                        enabled = true;
                }
            };
            render(p);
            require(disabled && enabled, "MHOF/MHON sensitivity restore");
        }
        ++passed;
        {
            auto o = opts;
            o.sampleRate = 48000;
            o.resampleOutput = true;
            o.fastSeek = true;
            o.maxSeconds = 0;
            o.loops = 0;
            o.monitor = std::make_shared<mdx::Monitor>();
            mdx::Player p(mdx::Song(oneTrack({253, 0, 247, 182, 127, 241, 0})), {}, o);
            require(p.seek(.5), "fast seek failed");
            require(std::abs(p.seconds() - .5) < .0001, "fast seek clock incorrect");
            require(o.monitor->frame.seconds == 0, "seek published misleading fast-forward display");
            std::array<int16_t, 8192> out{};
            require(p.render(out.data(), 4096) == 4096, "fast seek cannot resume");
            require(std::any_of(out.begin(), out.end(), [](int16_t v) { return v != 0; }),
                    "fast seek stayed silent");
            require(p.seconds() > .58, "fast seek clock stopped");
            require(p.seek(.1), "backward seek failed");
            require(p.render(out.data(), 4096) > 0, "backward seek cannot resume");
        }
        ++passed;
        {
            // MIDI may use a masked channel, but song pitch/key/voice writes must stay suppressed.
            auto mask = std::make_shared<std::atomic<uint32_t>>(1);
            auto state = std::make_shared<mdx::VoiceState>();
            auto o = opts;
            o.liveMuteMask = mask;
            o.voiceState = state;
            o.externalFm = true;
            o.maxSeconds = .1;
            int forbidden = 0;
            o.registerOutput = [&](uint64_t, int a, int v) {
                if ((a == 8 && (v & 7) == 0) || (a >= 0x28 && (a & 7) == 0))
                    ++forbidden;
            };
            mdx::Player p(mdx::Song(oneTrack({253, 0, 236, 2, 0, 6, 1, 0, 247, 182, 127, 241, 0})), {}, o);
            std::array<int16_t, 4096> out{};
            p.render(out.data(), 2048);
            auto q = std::make_shared<mdx::MidiQueue>();
            mdx::MidiSynth midi(state, q, 48000, true);
            q->push(0x90 | (69 << 8) | (100 << 16));
            midi.mix(out.data(), 2048);
            q->push(0x80 | (69 << 8));
            midi.mix(out.data(), 2048);
            p.render(out.data(), 2048);
            require(forbidden == 0, "masked song resumed writes after MIDI");
            *mask = 0;
            p.render(out.data(), 2048);
            require(forbidden > 0, "unmask failed to restore channel");
        }
        ++passed;
        {
            auto state = std::make_shared<mdx::VoiceState>();
            auto q = std::make_shared<mdx::MidiQueue>();
            state->defined[200] = true;
            state->definitions[200][1] = 7;
            state->definitions[200][2] = 1;
            state->definitions[200][3] = 1;
            mdx::MidiSynth midi(state, q, 48000);
            std::array<int16_t, 512> out{};
            q->push(0xf0 | (200 << 8));
            midi.mix(out.data(), 256);
            require(state->ids[0] == 200 && state->valid[0], "catalog high voice selection not retained");
            q->push(0x90 | (69 << 8) | (100 << 16));
            midi.mix(out.data(), 256);
            require(state->ids[0] == 200, "MIDI ignored catalog selection");
        }
        ++passed;
        {
            // Stored WAVE2 period 6 means 12 ticks per cycle; delta=256 is one detune unit/tick.
            mdx::Player p(mdx::Song(oneTrack({253, 0, 236, 2, 0, 6, 1, 0, 247, 182, 15, 241, 0})), {}, opts);
            std::array<int, 13> fraction{};
            fraction.fill(-1);
            p.registerTrace = [&](uint64_t t, int a, int v) {
                if (a == 0x30 && t % 896 == 0 && t / 896 < 13)
                    fraction[t / 896] = v;
            };
            render(p);
            const int displacement[] = {1, 2, 3, 4, 3, 2, 1, 0, -1, -2, -1, 0, 1};
            for (int i = 0; i < 13; ++i)
                require(fraction[i] == (5 + displacement[i]) * 4, "WAVE2 period/centering/delta wrong");
        }
        ++passed;

        {
            auto state = std::make_shared<mdx::VoiceState>();
            auto q = std::make_shared<mdx::MidiQueue>();
            mdx::Song song(oneTrack({253, 0, 182, 127, 241, 0}));
            for (int ch = 0; ch < 8; ++ch) {
                state->valid[ch] = true;
                state->voices[ch] = song.voices[0];
                state->voices[ch][3] = ch + 1;
            }
            mdx::MidiSynth synth(state, q, 48000);
            synth.setPolyphonic(true);
            std::array<int16_t, 256> out{};
            for (int n = 0; n < 8; ++n)
                q->push(0x90 | ((60 + n) << 8) | (100 << 16));
            synth.mix(out.data(), 128);
            for (int ch = 0; ch < 8; ++ch) {
                require(state->midiNotes[ch] == 60 + ch, "FM chord allocation");
                require(state->registers[0x40 + ch] == 1,
                        "allocated slot did not borrow input channel instrument");
            }
            q->push(0x90 | (80 << 8) | (100 << 16));
            synth.mix(out.data(), 128);
            require(state->midiNotes[0] == 80, "ninth note must steal oldest FM voice");
            q->push(0x80 | (60 << 8));
            synth.mix(out.data(), 128);
            require(state->midiNotes[0] == 80, "stolen note-off silenced replacement");
            q->push(0xb0 | (64 << 8) | (127 << 16));
            q->push(0x80 | (61 << 8));
            synth.mix(out.data(), 128);
            require(state->midiNotes[1] == 61, "poly sustain release too early");
            q->push(0xb0 | (64 << 8));
            synth.mix(out.data(), 128);
            require(state->midiNotes[1] == -1, "poly sustain not released");
            q->push(0xb0 | (123 << 8));
            synth.mix(out.data(), 128);
            for (auto &n : state->midiNotes)
                require(n == -1, "poly all-notes-off routing");
        }
        ++passed;
        {
            auto state = std::make_shared<mdx::VoiceState>();
            auto q = std::make_shared<mdx::MidiQueue>();
            mdx::Song song(oneTrack({253, 0, 247, 182, 127, 241, 0}));
            state->definitions = song.voices;
            state->defined = song.hasVoice;
            state->definitions[1] = song.voices[0];
            state->definitions[1][3] = 9;
            state->defined[1] = true;
            mdx::Options o;
            o.voiceState = state;
            o.maxSeconds = 0;
            mdx::Player player(song, {}, o);
            auto timeline = player.analyze();
            std::atomic<bool> pause{false};
            std::atomic<double> seek{-1}, current{0}, total{0};
            mdx::LiveSession live(&player, &player, timeline, state, q, o, pause, seek, current, total,
                                  [] { return false; });
            std::array<int16_t, 2048> out{};
            live.render(out.data(), 1024);
            auto original = state->voices;
            auto ids = state->ids;
            pause = true;
            live.service();
            q->push(0xc0 | (1 << 8));
            q->push(0x90 | (69 << 8) | (100 << 16));
            live.render(out.data(), 1024);
            require(state->midiNotes[0] == 69 && state->ids[0] == 1, "paused MIDI selection");
            auto frozen = current.load();
            live.render(out.data(), 1024);
            require(current == frozen, "paused song advanced");
            pause = false;
            live.service();
            require(state->voices == original && state->ids == ids, "resume did not restore song voices");
            for (auto &n : state->midiNotes)
                require(n == -1, "paused MIDI notes survived resume");
            live.render(out.data(), 1024);
            require(current > frozen, "song did not resume");
        }
        ++passed;

        {
            // Eight PCM notes borrow the input channel bank and sample format/rate.
            Bytes data(768 + 8 * 2048, 0);
            for (int n = 0; n < 8; ++n) {
                setLong(data, n * 8, 768 + n * 2048);
                setLong(data, n * 8 + 4, 2048);
                std::fill(data.begin() + 768 + n * 2048, data.begin() + 768 + (n + 1) * 2048,
                          uint8_t(16 + n));
            }
            auto state = std::make_shared<mdx::VoiceState>();
            state->pdx = std::make_shared<mdx::Pdx>(data);
            state->pcmFormat.fill(6);
            auto q = std::make_shared<mdx::MidiQueue>();
            mdx::MidiSynth synth(state, q, 48000);
            synth.setPolyphonic(true);
            std::array<int16_t, 256> out{};
            for (int n = 0; n < 8; ++n)
                q->push(0x98 | ((15 + n) << 8) | (100 << 16));
            synth.mix(out.data(), 128);
            for (int n = 0; n < 8; ++n) {
                require(state->midiNotes[8 + n] == 15 + n, "PCM chord allocation");
                require(state->midiNotes[n] == -1, "PCM chord used FM slot");
            }
            q->push(0x88 | (18 << 8));
            synth.mix(out.data(), 128);
            require(state->midiNotes[11] == -1, "PCM note-off routing");
            require(state->midiNotes[10] == 17 && state->midiNotes[12] == 19,
                    "PCM note-off cut other voices");
        }
        ++passed;

        {
            mdx::X68BlockRenderer batched;
            mdx::X68Shared block{};
            uint32_t phase = 0;
            int16_t held = 0;
            int level = 0, referenceLevel = 0, calls = 0;
            for (int pass = 0; pass < 4; ++pass) {
                block.count = 3;
                block.events[0] = {0, 1, uint32_t(20 + pass)};
                block.events[1] = {17, 1, uint32_t(40 + pass)};
                block.events[2] = {999, 1, uint32_t(60 + pass)};
                batched.render(
                    block, [&](uint8_t, uint8_t value) { level = value; },
                    [&](int16_t *out, unsigned n) {
                        ++calls;
                        std::fill_n(out, n * 2, int16_t(level));
                    });
                unsigned event = 0;
                for (unsigned f = 0; f < mdx::x68Frames; ++f) {
                    while (event < block.count && block.events[event].frame == f)
                        referenceLevel = block.events[event++].value;
                    phase += 44100;
                    if (phase >= 62500) {
                        held = int16_t(referenceLevel);
                        phase -= 62500;
                    }
                    require(block.audio[f * 2] == held && block.audio[f * 2 + 1] == held,
                            "X68 batching changed register timing");
                }
            }
            require(calls == 12, "X68 GetPcm was not batched by register span");
        }
        ++passed;
        {
            // Explicit MH must take effect even when a preceding MD is still pending.
            auto bytes = oneTrack({253, 0, 233, 40, 234, 128, 234, 2, 210, 100, 50, 0x73, 0, 241, 0});
            mdx::Player p(mdx::Song(bytes), {}, opts);
            bool immediate = false;
            p.registerTrace = [&](uint64_t t, int a, int v) {
                if (t == 0 && a == 0x38 && v == 0x73)
                    immediate = true;
            };
            render(p);
            require(immediate, "explicit MH left hardware LFO disabled behind MD");
        }
        ++passed;
        {
            auto state = std::make_shared<mdx::VoiceState>();
            auto q = std::make_shared<mdx::MidiQueue>();
            mdx::Song song(oneTrack({253, 0, 182, 127, 241, 0}));
            state->valid[3] = true; // Blank destination slots must still accept a borrowed tone.
            state->voices[3] = song.voices[0];
            state->voices[3][3] = 9;
            auto original = state->voices;
            mdx::MidiSynth synth(state, q, 48000);
            synth.setPolyphonic(true);
            std::array<int16_t, 2048> out{};
            q->push(0x93 | (60 << 8) | (127 << 16));
            q->push(0x93 | (64 << 8) | (127 << 16));
            synth.mix(out.data(), 1024);
            require(state->midiNotes[3] == 60 && state->midiNotes[4] == 64, "blank FM slots not allocated");
            require(state->registers[0x43] == 9 && state->registers[0x44] == 9, "wrong source tone");
            require(state->voices == original, "borrowed voice overwrote retained instruments");
            synth.setAftertouch(true);
            q->push(0xa3 | (60 << 8) | (32 << 16));
            synth.mix(out.data(), 128);
            require(state->registers[0x63] > state->registers[0x64], "poly aftertouch changed wrong note");
            q->push(0xd3 | (16 << 8));
            synth.mix(out.data(), 128);
            require(state->registers[0x63] == state->registers[0x64] && state->registers[0x63] > 0,
                    "channel aftertouch did not reach all borrowed voices");
            synth.setPolyphonic(false);
            q->push(0x93 | (61 << 8) | (100 << 16));
            q->push(0x93 | (65 << 8) | (100 << 16));
            synth.mix(out.data(), 128);
            for (int ch = 0; ch < 16; ++ch)
                require(state->midiNotes[ch] == (ch == 3 ? 65 : -1), "monophonic mode retained chord");
        }
        ++passed;
        {
            Bytes data(768 + 12000, 16);
            std::fill_n(data.begin(), 768, 0);
            setLong(data, 0, 768);
            setLong(data, 4, 12000);
            auto bank = std::make_shared<mdx::Pdx>(data);
            std::vector<Bytes> tr(9, Bytes{241, 0});
            tr[8] = {239, 8, 237, 6, 128, 127, 241, 0};
            mdx::Options a = opts;
            a.sampleRate = 62500;
            a.maxSeconds = 0;
            a.externalFm = true;
            a.gain = .1;
            mdx::Options b = a;
            b.pcmDelayMs = 10;
            mdx::Player plain(mdx::Song(songFile(tr, true)), bank, a);
            mdx::Player delayed(mdx::Song(songFile(tr, true)), bank, b);
            auto x = render(plain), y = render(delayed);
            require(y.size() == x.size() + 1250, "PCM delay tail was truncated");
            require(std::all_of(y.begin(), y.begin() + 1250, [](int16_t v) { return v == 0; }),
                    "PCM delay pre-roll not silent");
            require(std::equal(x.begin(), x.end(), y.begin() + 1250), "PCM delay changed sample data");
            mdx::Options live = a;
            live.liveGain = std::make_shared<std::atomic<double>>(0);
            live.livePcmGain = std::make_shared<std::atomic<double>>(1);
            mdx::SamplePlayer sample(*bank, 0, 6, live);
            std::array<int16_t, 256> out{};
            sample.render(out.data(), 128);
            require(std::all_of(out.begin(), out.end(), [](int16_t v) { return v == 0; }),
                    "master zero not silent");
            *live.liveGain = 4;
            sample.render(out.data(), 128);
            require(out[0] > 0, "400 percent master change not live");
            *live.livePcmGain = 0;
            sample.render(out.data(), 128);
            require(out[0] == 0, "PCM gain change not live");
        }
        ++passed;
        {
            auto bytes = oneTrack({253, 0,   251, 128, 235, 0,   0,   100, 64,  0, 247, 182, 3,
                                   235, 128, 247, 182, 3,   235, 129, 247, 182, 3, 241, 0});
            mdx::Player p(mdx::Song(bytes), {}, opts);
            bool off = false, on = false;
            p.registerTrace = [&](uint64_t t, int a, int v) {
                if (a != 0x60)
                    return;
                if (t == 3584 && v == 0)
                    off = true;
                if (t > 3584 && t < 7168)
                    require(v == 0, "MAOF retained volume LFO");
                if (t > 7168 && v > 0)
                    on = true;
            };
            render(p);
            require(off && on, "EB80 OFF / EB81 ON did not restore volume");
        }
        ++passed;
        {
            for (bool poly : {false, true}) {
                auto state = std::make_shared<mdx::VoiceState>();
                auto q = std::make_shared<mdx::MidiQueue>();
                mdx::Song song(oneTrack({253, 0, 182, 127, 241, 0}));
                state->valid[0] = true;
                state->voices[0] = song.voices[0];
                mdx::MidiSynth synth(state, q, 48000);
                synth.setPolyphonic(poly);
                std::array<int16_t, 256> out{};
                auto send = [&](uint32_t m) {
                    q->push(m);
                    synth.mix(out.data(), 128);
                };
                send(0x90 | (60 << 8) | (127 << 16));
                send(0xd0 | (16 << 8));
                require(state->registers[0x60] == 0, "aftertouch must default off");
                synth.setAftertouch(true);
                send(0xd0 | (16 << 8));
                require(state->registers[0x60] > 0, "enabled pressure ignored");
                send(0x80 | (60 << 8));
                send(0xd0); // release-pressure packet after the last key-up must not latch zero
                send(0x90 | (62 << 8) | (127 << 16));
                require(state->registers[0x60] == 0, "pressure leaked to a fresh phrase");
                send(0xd0 | (16 << 8));
                synth.setAftertouch(false);
                require(state->registers[0x60] == 0, "disabling pressure did not restore held voice");
            }
        }
        ++passed;
        {
            Bytes prefix{253, 0, 234, 64, 210, 100, 50, 0x73};
            Bytes notes{182, 7, 184, 7, 234, 128, 186, 7, 234, 129, 188, 7, 241, 0};
            auto plain = prefix;
            plain.insert(plain.end(), notes.begin(), notes.end());
            auto delayed = prefix;
            delayed.insert(delayed.end(), {233, 80});
            delayed.insert(delayed.end(), notes.begin(), notes.end());
            mdx::Player a(mdx::Song(oneTrack(plain)), {}, opts), b(mdx::Song(oneTrack(delayed)), {}, opts);
            std::vector<std::tuple<uint64_t, int, int>> ra, rb;
            a.registerTrace = [&](uint64_t t, int r, int v) { ra.emplace_back(t, r, v); };
            b.registerTrace = [&](uint64_t t, int r, int v) { rb.emplace_back(t, r, v); };
            require(render(a) == render(b), "MD changed OPMLFO audio");
            require(ra == rb, "MD changed OPMLFO register timing");
        }
        ++passed;
        {
            std::vector<Bytes> tracks(9, Bytes{241, 0});
            tracks[0] = {253, 0, 234, 0, 210, 100, 50, 0x73, 182, 20, 241, 0};
            mdx::Player a(mdx::Song(songFile(tracks)), {}, opts);
            tracks[8] = {234, 3, 7, 9, 11, 0xff, 234, 128, 234, 129, 241, 0};
            mdx::Player b(mdx::Song(songFile(tracks)), {}, opts);
            std::vector<std::tuple<uint64_t, int, int>> ra, rb;
            a.registerTrace = [&](uint64_t t, int r, int v) { ra.emplace_back(t, r, v); };
            b.registerTrace = [&](uint64_t t, int r, int v) { rb.emplace_back(t, r, v); };
            require(render(a) == render(b), "PCM MH modified FM audio");
            require(ra == rb, "PCM MH wrote OPM registers");
        }
        ++passed;
        {
            std::array<uint8_t, 256> regs{};
            for (int op = 0; op < 4; ++op)
                regs[0x40 + op * 8] = 1;
            regs[0x20] = 4;
            regs[0x50] = 3;  // C1: +19.02 -> +7 semitones
            regs[0xd8] = 64; // C2: DT2 +6
            auto notes = mdx::carrierNotes(40, regs, 0, true);
            require(notes == mdx::DisplayNotes{59, 46, -1, -1}, "ALG4 carrier absolute octave");
            regs[0x20] = 7;
            regs[0x40] = 0; // ML=0 halves frequency: exactly -12 retained
            regs[0x48] = 4; // +24 folds to 0
            notes = mdx::carrierNotes(40, regs, 0, true);
            require(notes == mdx::DisplayNotes{28, 64, 59, 46}, "ALG7 four pitches / ML zero");
            require(mdx::carrierNotes(40, regs, 0, false) == mdx::DisplayNotes{40, -1, -1, -1},
                    "display off");
            require(mdx::carrierNotes(40, regs, 8, true) == mdx::DisplayNotes{40, -1, -1, -1}, "PCM changed");
            regs[0x20] = 0;
            require(mdx::carrierNotes(40, regs, 0, true) == mdx::DisplayNotes{46, -1, -1, -1},
                    "ALG0 carrier");
        }
        ++passed;
        {
            auto o = opts;
            o.maxSeconds = 0;
            o.sampleRate = 48000;
            o.resampleOutput = true;
            o.liveSpeed = std::make_shared<std::atomic<int>>(200);
            mdx::Player fast(mdx::Song(oneTrack({253, 0, 182, 100, 241, 0})), {}, o);
            auto melody = oneTrack({253, 0, 182, 20, 184, 20, 241, 0});
            mdx::Player rapid(mdx::Song(melody), {}, o);
            auto normalOptions = o;
            normalOptions.liveSpeed.reset();
            mdx::Player normal(mdx::Song(melody), {}, normalOptions);
            std::vector<uint64_t> rapidOns, normalOns;
            rapid.registerTrace = [&](uint64_t time, int reg, int value) {
                if (reg == 8 && (value & 0x78) && !(value & 7))
                    rapidOns.push_back(time);
            };
            normal.registerTrace = [&](uint64_t time, int reg, int value) {
                if (reg == 8 && (value & 0x78) && !(value & 7))
                    normalOns.push_back(time);
            };
            render(rapid);
            render(normal);
            require(rapidOns.size() == 2 && normalOns.size() == 2 &&
                        std::abs(double(rapidOns[1]) * 2 - normalOns[1]) <= 2,
                    "200% changed clock display but not note scheduling");
            auto t = fast.analyze();
            std::vector<int16_t> block(4800 * 2);
            fast.render(block.data(), 4800);
            require(std::abs(fast.position(t) - .2) < .002, "200% score position");
            o.liveSpeed->store(50);
            fast.render(block.data(), 4800);
            require(std::abs(fast.position(t) - .25) < .002, "live 50% score position");
            require(fast.seek(.4), "speed-independent seek failed");
            require(std::abs(fast.position(t) - .4) < .002, "seek used playback multiplier");
            fast.render(block.data(), 4800);
            require(std::abs(fast.position(t) - .45) < .002, "speed lost after seek");
            auto voices = std::make_shared<mdx::VoiceState>();
            auto queue = std::make_shared<mdx::MidiQueue>();
            std::atomic<bool> paused{false};
            std::atomic<double> seek{-1}, current{0}, total{0};
            mdx::LiveSession session(&fast, &fast, t, voices, queue, o, paused, seek, current, total,
                                     [] { return false; });
            o.liveSpeed->store(0);
            require(session.servicePending(), "zero speed must service transport");
            session.service();
            session.render(block.data(), 4800);
            require(std::abs(current.load() - .45) < .002, "zero speed advanced song");
            o.liveSpeed->store(100);
            session.service();
            session.render(block.data(), 4800);
            require(std::abs(current.load() - .55) < .002, "zero speed failed to resume");
        }
        ++passed;
        {
            auto state = std::make_shared<mdx::VoiceState>();
            mdx::Song song(basic);
            state->valid[0] = true;
            state->voices[0] = song.voices[0];
            auto queue = std::make_shared<mdx::MidiQueue>();
            mdx::MidiSynth synth(state, queue, 48000, false);
            synth.setPolyphonic(false); // External MIDI is mono during song playback.
            queue->push(0xc0000000u | 0x90 | (60 << 8) | (100 << 16));
            queue->push(0xc0000000u | 0x90 | (64 << 8) | (100 << 16));
            std::array<int16_t, 512> buffer{};
            synth.mix(buffer.data(), 256);
            int count = 0;
            for (auto &n : state->midiNotes)
                count += n >= 0;
            require(count == 2, "viewer Poly did not override song MIDI policy");
            queue->push(0xc0000000u | 0x80 | (60 << 8));
            queue->push(0xc0000000u | 0x80 | (64 << 8));
            synth.mix(buffer.data(), 256);
            for (auto &n : state->midiNotes)
                require(n < 0, "viewer note off left borrowed voice");
            queue->push(0x80000000u | 0x90 | (60 << 8) | (100 << 16));
            queue->push(0x80000000u | 0x90 | (64 << 8) | (100 << 16));
            synth.mix(buffer.data(), 256);
            count = 0;
            for (auto &n : state->midiNotes)
                count += n >= 0;
            require(count == 1 && state->midiNotes[0] == 64, "viewer Mono not respected");
        }
        ++passed;
        {
            // ALG5 FB7: M1/C1 register slots active (raw KON mask 3).
            mdx::Voice v{};
            v[0] = 11;
            v[1] = 5 | (7 << 3);
            v[2] = 3;
            for (int slot = 0; slot < 4; ++slot) {
                v[3 + slot] = slot == 0 ? 2 : slot == 2 ? 1 : 0;
                v[7 + slot] = slot == 0 ? 24 : slot == 2 ? 15 : 127;
                v[11 + slot] = slot == 0 || slot == 2 ? 31 : 0;
                v[23 + slot] = 15;
            }
            auto bytes = oneTrack({253, 11, 182, 100, 241, 0});
            mdx::Song song(bytes);
            song.hasVoice[11] = true;
            song.voices[11] = v;
            auto view = std::make_shared<mdx::Monitor>();
            auto o = opts;
            o.monitor = view;
            o.externalFm = true;
            mdx::Player hardware(song, {}, o);
            std::array<int16_t, 4096> block{};
            hardware.render(block.data(), 2048);
            double peak = 0;
            for (auto x : view->frame.waveform[0])
                peak = std::max(peak, std::abs(double(x)));
            require(peak > .001, "@11 hardware-mode waveform missing");
            require(std::all_of(block.begin(), block.end(), [](auto x) { return x == 0; }),
                    "visualization leaked into hardware audio output");
            for (int c = 1; c < 8; ++c)
                for (auto x : view->frame.waveform[c])
                    require(x == 0, "monitor channel crosstalk");
            auto state = std::make_shared<mdx::VoiceState>();
            state->valid[0] = true;
            state->voices[0] = v;
            auto queue = std::make_shared<mdx::MidiQueue>();
            mdx::MidiSynth audition(state, queue, 48000, true, {}, 0, view);
            audition.setWaveformExclusive(true);
            queue->push(0x90 | (60 << 8) | (100 << 16));
            audition.mix(block.data(), 2048);
            peak = 0;
            for (auto x : view->frame.waveform[0])
                peak = std::max(peak, std::abs(double(x)));
            require(peak > .001, "@11 MIDI audition waveform missing");
            require(std::all_of(block.begin(), block.end(), [](auto x) { return x == 0; }),
                    "MIDI monitor leaked into hardware audio output");
        }
        ++passed;
        {
            auto makeState = [&] {
                auto state = std::make_shared<mdx::VoiceState>();
                state->valid[0] = true;
                auto &v = state->voices[0];
                v[1] = 7;
                v[2] = 15;
                for (int op = 0; op < 4; ++op) {
                    v[3 + op] = op + 1;
                    v[7 + op] = 24;
                    v[11 + op] = 31;
                    v[23 + op] = 15;
                }
                return state;
            };
            auto qa = std::make_shared<mdx::MidiQueue>(), qb = std::make_shared<mdx::MidiQueue>();
            auto o = opts;
            o.sampleRate = 48000;
            o.gain = .5;
            o.liveMeasuredOutputTiming = std::make_shared<std::atomic<bool>>(true);
            auto other = o;
            other.liveMeasuredOutputTiming = std::make_shared<std::atomic<bool>>(true);
            std::atomic<bool> paused{false};
            std::atomic<double> seekA{-1}, seekB{-1}, posA{0}, posB{0}, totalA{0}, totalB{0};
            mdx::LiveSession a(nullptr, nullptr, {}, makeState(), qa, o, paused, seekA, posA, totalA,
                               [] { return false; });
            mdx::LiveSession b(nullptr, nullptr, {}, makeState(), qb, other, paused, seekB, posB, totalB,
                               [] { return false; });
            qa->push(0x90 | (84 << 8) | (100 << 16));
            qb->push(0x90 | (84 << 8) | (100 << 16));
            std::array<int16_t, 4096> left{}, right{};
            a.render(left.data(), 2048);
            b.render(right.data(), 2048);
            require(left == right, "timing test setup is not deterministic");
            o.liveMeasuredOutputTiming->store(false);
            a.render(left.data(), 2048);
            b.render(right.data(), 2048);
            require(left != right, "live timing switch ignored by MIDI session");
            o.liveMeasuredOutputTiming->store(true);
            a.render(left.data(), 2048);
            b.render(right.data(), 2048);
            a.render(left.data(), 2048);
            b.render(right.data(), 2048);
            require(left == right, "timing re-enable changed synthesis state");
        }
        ++passed;
        {
            auto trace=[&](int command,int value) {
                Bytes data{253,0,234,2,210,100,50,0x73,
                           236,2,0,6,1,0,235,0,0,100,1,0};
                data.insert(data.end(),{uint8_t(command),uint8_t(value),182,7,241,0});
                auto analysis=opts;analysis.externalFm=true;
                mdx::Player p(mdx::Song(oneTrack(data)),{},analysis);
                std::vector<std::tuple<uint64_t,int,int>> result;
                p.registerTrace=[&](uint64_t t,int a,int v){result.emplace_back(t,a,v);};
                render(p); return result;
            };
            for(int command:{234,235,236}) {
                auto off=trace(command,0x80),on=trace(command,0x81);
                require(off!=on,"LFO switch test lacks ON/OFF distinction");
                for(int value=0x80;value<=0xff;++value)
                    require(trace(command,value)==((value&1)?on:off),"unused LFO switch bits changed behavior");
            }
            auto waveform=[&](int mode) {
                auto data=oneTrack({253,0,236,uint8_t(mode),0,6,1,0,182,7,241,0});
                mdx::Player p(mdx::Song(data),{},opts);return render(p);
            };
            for(int mode=0;mode<8;++mode)
                require(waveform(mode)==waveform(mode|0x78),"unused software LFO mode bits changed sound");
            // Reserved PMS/AMS bits must not reach the MH sensitivity register.
            auto data=oneTrack({234,0x3e,210,100,50,0xff,241,0});
            mdx::Player p(mdx::Song(data),{},opts);bool sensitivity=false;
            p.registerTrace=[&](uint64_t,int a,int v){if(a==0x38){require(v==0x73,"MH sensitivity bits unmasked");sensitivity=true;}};
            render(p);require(sensitivity,"MH sensitivity write missing");
        }
        ++passed;
        {
            auto bytes=oneTrack({253,0,182,7,241,0});
            mdx::Song original(bytes);
            auto duplicate=original.voices[0]; duplicate[7]=99;
            bytes.insert(bytes.end(),duplicate.begin(),duplicate.end());
            auto unique=duplicate;unique[0]=17;
            bytes.insert(bytes.end(),unique.begin(),unique.end());
            mdx::Song parsed(bytes);
            require(parsed.voices[0]==original.voices[0],"duplicate replaced first instrument");
            require(parsed.hasVoice[17] && parsed.voices[17]==unique,"duplicate skipped a later distinct instrument");
            mdx::Player a(original,{},opts),b(parsed,{},opts);
            require(render(a)==render(b),"duplicate instrument changed playback");
        }
        ++passed;
        // MA1,12,9: fixed-point depth 9*256 must change TL by 9, not 2304.
        for(int wave:{1,5}) {
            int depth=wave==1 ? 9*256 : 9;
            auto bytes=oneTrack({253,0,251,128,235,uint8_t(wave),0,12,uint8_t(depth>>8),uint8_t(depth),233,0,235,129,182,47,241,0});
            mdx::Song song(bytes); song.voices[0][7]=40;
            mdx::Player player(song,{},opts); bool keyed=false; int low=127,high=0;
            player.registerTrace=[&](uint64_t,int a,int v) {
                if(a==8 && (v&7)==0) keyed=(v&120)!=0;
                if(keyed && a==0x60) {low=std::min(low,v);high=std::max(high,v);}
            };
            render(player);
            require(low==40 && high==49,"MA square attenuation must alternate base and base+9");
        }
        ++passed;
        {
            auto data=oneTrack({253,0,251,128,235,1,0,12,9,0,233,0,182,16,182,16,241,0});
            mdx::Song song(data);song.voices[0][7]=40;
            mdx::Player player(song,{},opts);int tl=0,ons=0;
            player.registerTrace=[&](uint64_t,int a,int v){
                if(a==0x60) tl=v;
                if(a==8 && (v&7)==0 && (v&120)) {require(tl==(ons==0?40:49),"MD0 did not preserve MA phase on retrigger");++ons;}
            };
            render(player);require(ons==2,"missing MA retrigger coverage");
        }
        ++passed;
        {
            Bytes commands{241,1,0}; commands.resize(259,0xe3);
            commands.insert(commands.end(),{253,0,182,3,241,0});
            mdx::Player player(mdx::Song(oneTrack(commands)),{},opts);
            require(!player.analyze().truncated,"forward F1 relocation failed");
            require(!render(player).empty(),"forward F1 lost notes");
            auto o=opts;o.maxSeconds=10;
            mdx::Player repeated(mdx::Song(oneTrack({253,0,246,0,0,182,0,245,255,250,241,0})),{},o);
            int ons=0;repeated.registerTrace=[&](uint64_t,int a,int v){if(a==8 && (v&7)==0 && (v&120))++ons;};
            render(repeated); require(ons==256,"zero repeat count must wrap through 256 iterations");
        }
        ++passed;
        {
            for(int destination:{9,16,47,255}) {
                auto bytes=oneTrack({239,uint8_t(destination),253,0,182,3,241,0});
                mdx::Player player(mdx::Song(bytes),{},opts);
                int keys=0;player.registerTrace=[&](uint64_t,int a,int v){if(a==8 && (v&120))++keys;};
                render(player);
                require(keys==1,"invalid sync signal prevented following note");
                bool warned=false;
                for(const auto &warning:player.warnings()) warned|=warning.find("Ignored sync signal")!=std::string::npos;
                require(warned,"ignored invalid sync was not reported");
            }
        }
        ++passed;
        {
            Bytes relocated{241,1,0,253,0,182,3,241,0};
            relocated.resize(259,0xe3);
            relocated.insert(relocated.end(),{241,0xfe,0xfd}); // back to offset 3, not visited yet
            mdx::Player a(mdx::Song(oneTrack(relocated)),{},opts);
            mdx::Player b(mdx::Song(oneTrack({253,0,182,3,241,0})),{},opts);
            auto ta=a.analyze(),tb=b.analyze();
            require(ta.loopEnd==0 && std::abs(ta.duration-tb.duration)<1e-9,"backward relocation misclassified as song loop");
            require(render(a)==render(b),"relocation changed playback duration/audio");
            require(std::abs(a.position(ta)-b.position(tb))<1e-9,"relocation changed displayed position");
        }
        ++passed;
        {
            std::vector<Bytes> tracks(9,Bytes{241,0});
            tracks[0]={253,0,182,59,241,0};
            tracks[8]={1,241,255,252}; // two-tick auxiliary rest loop
            mdx::Player player(mdx::Song(songFile(tracks)),{},opts);
            auto timeline=player.analyze();
            require(timeline.loopEnd==0 && timeline.duration>.8,"auxiliary loop replaced full-song timeline");
            std::array<int16_t,2048> buffer{};
            double last=0;
            for(int i=0;i<20;++i) {
                player.render(buffer.data(),1024);
                double now=player.position(timeline);
                require(now>=last,"position wrapped on auxiliary loop");last=now;
            }
        }
        ++passed;
        // Finite songs repeat only after release silence in interactive unlimited mode.
        {
            mdx::Options o; o.loops=0; o.maxSeconds=0;
            auto state=std::make_shared<mdx::VoiceState>();
            auto queue=std::make_shared<mdx::MidiQueue>();
            mdx::Song song(oneTrack({253,0,182,3,241,0}));
            mdx::Player player(song,{},o);
            auto timeline=player.analyze();
            std::atomic<bool> pause{false};
            std::atomic<double> seek{-1},current{0},total{0};
            mdx::LiveSession session(&player,&player,timeline,state,queue,o,pause,seek,current,total,[]{return false;});
            std::array<int16_t,2048> buffer{};
            int restarts=0; double previous=0;
            for(int i=0;i<200;++i) {
                require(session.render(buffer.data(),1024)>0,"finite unlimited playback ended");
                double now=player.seconds();
                if(now<previous) { ++restarts; require(previous>=.1,"restart before silence guard"); }
                previous=now;
            }
            require(restarts>=2,"finite unlimited playback failed to repeat");
        }
        ++passed;
        // Live loop switches are accepted only before fading starts.
        {
            auto run=[](int scenario) {
                mdx::Options o; o.maxSeconds=0; o.fadeSeconds=.1;
                o.liveLoops=std::make_shared<std::atomic<int>>(scenario==0 ? 0:2);
                mdx::Player p(mdx::Song(oneTrack({253,0,180,1,241,255,251})),{},o);
                std::array<int16_t,960> out{};
                for(int n=0;n<30;++n) {
                    auto count=p.render(out.data(),480);
                    if(scenario!=2) require(count>0,"live unlimited playback stopped");
                    if(scenario==1 && n==1) o.liveLoops->store(0);
                    if(scenario==2 && n==10) o.liveLoops->store(0);
                }
                if(scenario==0) o.liveLoops->store(2); // many loops already elapsed
                if(scenario==1) { require(p.unlimited(),"ON did not enable unlimited"); return; }
                for(int n=0;n<30;++n) p.render(out.data(),480);
                require(p.render(out.data(),480)==0,"OFF or fade lock failed to finish");
                require(!p.unlimited(),"switch changed a committed fade");
            };
            run(0); run(1); run(2);
        }
        ++passed;
        // Square pitch LFO: equivalent fixed-point and WAVE+4 encodings.
        for(int amplitude:{1,-24}) for(int wave:{1,5}) {
            int delta=wave==1 ? amplitude*256 : amplitude;
            unsigned word=unsigned(delta)&65535;
            auto bytes=oneTrack({253,0,236,uint8_t(wave),0,4,uint8_t(word>>8),uint8_t(word),182,23,241,0});
            mdx::Player p(mdx::Song(bytes),{},opts);
            int kc=0,seen=0;
            p.registerTrace=[&](uint64_t tick,int address,int value) {
                if(address==0x28) kc=value;
                if(address!=0x30 || tick>=24*896) return;
                constexpr int semitones[16]={0,1,2,2,3,4,5,5,6,7,8,8,9,10,11,11};
                int actual=((kc>>4)*12+semitones[kc&15])*64+(value>>2);
                int sign=tick==0 ? 0 : ((tick/896-1)/4)%2 ? -1 : 1;
                require(actual==54*64+5+sign*amplitude,"square pitch LFO amplitude/period incorrect");
                ++seen;
            };
            render(p);
            require(seen>=20,"square LFO register coverage missing");
        }
        ++passed;
        // MH raw operand bank bits are preserved, as observed in black-box logs.
        for(int pm:{0x20,0xa0}) for(int am:{0x40,0xc0}) {
            auto bytes=oneTrack({253,0,234,0,128,uint8_t(pm),uint8_t(am),0x73,176,7,241,0});
            mdx::Player p(mdx::Song(bytes),{},opts);
            std::vector<int> writes;
            p.registerTrace=[&](uint64_t,int address,int value){if(address==0x19)writes.push_back(value);};
            render(p);
            require(writes==std::vector<int>({pm,am}),"MH altered explicit PMD/AMD bank bits");
        }
        ++passed;
        // Independent black-box register capture: MP6,8,1, note 48, tempo 200.
        // Values are final KC/KF states per driver tick, not write-order dependent.
        {
            const int offsets[]={1,2,3,4,5,4,3,2,1,0,-1,-2,-3,-2,-1,0,1};
            auto bytes=oneTrack({255,200,253,0,236,6,0,8,0,1,176,31,241,0});
            mdx::Player p(mdx::Song(bytes),{},opts);
            int kc=0,seen=0;
            p.registerTrace=[&](uint64_t tick,int address,int value) {
                if(address==0x28)kc=value;
                if(address!=0x30 || tick/896>=std::size(offsets))return;
                constexpr int semitones[16]={0,1,2,2,3,4,5,5,6,7,8,8,9,10,11,11};
                int actual=((kc>>4)*12+semitones[kc&15])*64+(value>>2);
                require(actual==48*64+5+offsets[tick/896],"MP triangle disagrees with captured register values");
                ++seen;
            };
            render(p);require(seen>=17,"MP triangle trace coverage missing");
        }
        ++passed;
        // Negative MA triangle: captured descending half, plus full-cycle,
        // tied-note and retrigger regression coverage using original inputs.
        for (int wave : {2,6}) for (int sign : {-1,1}) for(int delay : {0,1,4}) {
            int period = wave == 2 ? 192 : 8;
            int depth = sign * (wave == 2 ? 93 : 1);
            unsigned word = unsigned(depth) & 65535;
            Bytes cmd{255,200,253,0,251,15,233,uint8_t(delay),235,uint8_t(wave),
                      uint8_t(period>>8),uint8_t(period),uint8_t(word>>8),uint8_t(word)};
            for(int n=0;n<4;++n) {
                if(n<3)cmd.push_back(247);
                cmd.insert(cmd.end(),{176,127});
            }
            cmd.insert(cmd.end(),{176,31,235,128,176,15,241,0});
            auto file=oneTrack(cmd);
            size_t voice=5+((file[5]<<8)|file[6]);
            file[voice+1]=5;file[voice+2]=15;
            for(int op=0;op<4;++op)file[voice+7+op]=10;
            auto o=opts;o.maxSeconds=10;
            mdx::Player p(mdx::Song(file),{},o);
            std::array<std::array<int,3>,560> states{};
            for(auto &row:states)row.fill(-1);
            p.registerTrace=[&](uint64_t tick,int address,int value) {
                auto n=tick/896;
                if(n>=states.size())return;
                for(int op=0;op<3;++op)
                    if(address==0x68+8*op)states[n][op]=value;
            };
            render(p);
            for(int n=0;n<560;++n) {
                int age=n<512?n:n-512;
                int phase=delay==0?n:age;
                if(delay)phase=std::max(0,phase-delay+1);
                int q=phase%(2*period),ramp=std::min(q,2*period-q);
                int amplitude=wave==2?93:256;
                int expected=n>=544 || age<std::max(0,delay-1)?12:12+(sign<0?period-ramp:ramp)*amplitude/256;
                for(int op=0;op<3;++op) {
                    if(n>=545 && states[n][op]<0)states[n][op]=states[n-1][op];
                    if(states[n][op]!=expected) throw std::runtime_error("signed MA triangle mismatch wave="+std::to_string(wave)+" sign="+std::to_string(sign)+" tick="+std::to_string(n)+" op="+std::to_string(op)+" actual="+std::to_string(states[n][op])+" expected="+std::to_string(expected));
                }
            }
        }
        ++passed;
        // Black-box captures: negative MA saw/square start attenuated.
        for (int wave : {0,1}) for (int depth : {-256,-93,0,93,256}) {
            unsigned word = unsigned(depth) & 65535;
            auto file = oneTrack({255,200,253,0,251,128,233,0,235,uint8_t(wave),
                                  0,8,uint8_t(word>>8),uint8_t(word),176,31,241,0});
            size_t voice = 5 + ((file[5]<<8)|file[6]);
            file[voice+7] = 32;
            mdx::Player p(mdx::Song(file),{},opts);
            std::array<int,32> values{};
            values.fill(-1);
            p.registerTrace = [&](uint64_t tick,int address,int value) {
                if (address == 0x60 && tick/896 < values.size())
                    values[tick/896] = value;
            };
            render(p);
            for (int tick=0;tick<32;++tick) {
                int displacement = wave==0 ? (tick%8)*depth + std::max(0,-8*depth)
                                           : ((tick/8)%2)*depth + std::max(0,-depth);
                require(values[tick]==32+displacement/256,"signed MA saw/square trace mismatch");
            }
        }
        ++passed;
        {
            mdx::SoftwareLfoRandom rng;
            const int captured[] = {1844,-16525,21930,17995,-6111,-21489,14545,-21830};
            for(int expected : captured)
                require(rng.next()==expected,"random recurrence disagrees with independent capture");
        }
        ++passed;
        for(int detune : {-32768,-20480,20480,32767}) {
            unsigned word=unsigned(detune)&65535;
            auto data=oneTrack({255,200,253,0,243,uint8_t(word>>8),uint8_t(word),236,3,0,1,0,93,176,15,241,0});
            mdx::Player player(mdx::Song(data),{},opts);
            std::array<int,16> pitches{};
            int kc=0;
            player.registerTrace=[&](uint64_t tick,int address,int value) {
                if(address==0x28)kc=value;
                if(address!=0x30 || tick/896>=pitches.size())return;
                constexpr int semitones[16]={0,1,2,2,3,4,5,5,6,7,8,8,9,10,11,11};
                pitches[tick/896]=((kc>>4)*12+semitones[kc&15])*64+(value>>2);
            };
            render(player);
            mdx::SoftwareLfoRandom rng;
            for(int tick=0;tick<16;++tick) {
                int sample=tick?rng.next():0;
                int raw=3077+detune+int(std::floor(sample*93.0/256));
                int wrapped=int(unsigned(raw)&65535);
                if(wrapped>=32768)wrapped-=65536;
                require(pitches[tick]==std::clamp(wrapped,0,6143),"random pitch overflow handling mismatch");
            }
        }
        ++passed;
        // Captured first random sample = 1844. Depth boundary arithmetic is
        // independent of the sequence generator.
        {
            const int depths[] = {1,93,127,128,129,255,256,257,-1,-93,-127,-128,-129,-255,-256,-257};
            const int pitchDepths[] = {1,93,127,-128,-127,-1,0,1,-1,-93,-127,-128,127,1,0,-1};
            const int amplitude[] = {7,157,146,154,161,44,52,59,248,98,109,102,94,211,204,196};
            for(size_t i=0;i<std::size(depths);++i) {
                require(mdx::randomPitchDepth(depths[i])==pitchDepths[i],"random MP signed byte mismatch");
                require(mdx::randomAmplitude(1844,depths[i])==amplitude[i],"random MA integer extraction mismatch");
            }
            require(mdx::randomAmplitude(-16525,1)==191,"negative random product rounding mismatch");
            require(mdx::randomAmplitude(-16525,-1)==64,"negative depth random product mismatch");
        }
        ++passed;
        for(bool pitch : {false,true}) {
            auto data=oneTrack({255,200,253,0,251,128,uint8_t(pitch?236:235),3,0,4,0,1,176,31,241,0});
            mdx::Player player(mdx::Song(data),{},opts);
            std::array<int,32> values{};
            int kc=0;
            player.registerTrace=[&](uint64_t tick,int address,int value) {
                if(tick/896>=values.size())return;
                if(address==0x28)kc=value;
                if(pitch && address==0x30)values[tick/896]=kc*256+value;
                if(!pitch && address==0x60)values[tick/896]=value;
            };
            render(player);
            int changes=0;
            for(int tick=1;tick<32;++tick) if(values[tick]!=values[tick-1]) {
                require((tick-(pitch?1:0))%4==0,"random update occurred outside period boundary");
                ++changes;
            }
            require(changes>=3,"random sample-and-hold lacks variation");
        }
        ++passed;
        // MP/MA retain modulation during active-track rests and quantized tails.
        for (int scenario : {0,1,2}) {
            Bytes commands{253,0,251,128,236,2,0,6,48,0,235,0,0,100,1,0};
            if(scenario==2) commands.insert(commands.end(),{0xf8,1}); // q1
            commands.insert(commands.end(),{182,9});
            if(scenario==1) commands.push_back(15);
            commands.insert(commands.end(),{241,0});
            mdx::Player p(mdx::Song(oneTrack(commands)),{},opts);
            uint64_t offAt=0;
            bool keyed=false;
            int heldPitch=0,heldVolume=0,releaseWrites=0;
            p.registerTrace=[&](uint64_t t,int a,int v) {
                if(a==8 && (v&7)==0) {
                    if(v&120) { require(!keyed,"unexpected LFO note retrigger");keyed=true; }
                    else if(keyed && !offAt) offAt=t;
                }
                if(keyed && !offAt) {
                    if(a==0x30) ++heldPitch;
                    if(a==0x60) ++heldVolume;
                }
                if(offAt && t>offAt && (a==0x30 || a==0x60)) ++releaseWrites;
            };
            auto output=render(p);
            require(keyed && offAt,"LFO gate test never keyed off");
            if(scenario!=2) require(heldPitch>3 && heldVolume>3,"held-note LFO stopped");
            if(scenario!=0) require(releaseWrites>3,"release/rest LFO stopped");
            require(output.size()<150000,"unexpected final release duration");
        }
        ++passed;
        {
            // Generic payload order is independent of track/channel order.
            for(int count:{9,16}) {
                std::vector<Bytes> tracks(count,Bytes{241,0});
                for(int ch=0;ch<8;++ch) tracks[ch]={253,0,251,128,uint8_t(170+ch),7,241,0};
                auto ordinary=songFile(tracks);
                const size_t base=5,tableSize=size_t(count+1)*2;
                const size_t voice=base+((ordinary[base]<<8)|ordinary[base+1]);
                Bytes alternate(ordinary.begin(),ordinary.begin()+base+tableSize);
                alternate.insert(alternate.end(),ordinary.begin()+voice,ordinary.end());
                alternate.insert(alternate.end(),ordinary.begin()+base+tableSize,ordinary.begin()+voice);
                setWord(alternate,base,tableSize);
                for(int ch=0;ch<count;++ch) {
                    size_t p=base+2+ch*2;
                    setWord(alternate,p,((ordinary[p]<<8)|ordinary[p+1])+27);
                }
                mdx::Player a(mdx::Song(ordinary),{},opts), b(mdx::Song(alternate),{},opts);
                require(render(a)==render(b),"payload relocation changed sound or startup timing");
                auto reordered=ordinary;
                for(int k=0;k<2;++k) std::swap(reordered[base+2+k],reordered[base+4+k]);
                mdx::Song reorderedSong(reordered);
                require(reorderedSong.tracks[0]>reorderedSong.tracks[1],"nonmonotonic track table rejected");
                auto shared=ordinary;
                setWord(shared,base+4,tableSize);
                mdx::Song sharedSong(shared);
                require(sharedSong.tracks[0]==sharedSong.tracks[1],"shared track entry rejected");
                for(int offset:{1,65535}) {
                    auto invalid=alternate;setWord(invalid,base+4,offset);
                    rejects([&]{mdx::Song rejected(invalid);},"invalid track offset accepted");
                }
                for(size_t n=0;n<base+tableSize;++n) {
                    Bytes truncated(alternate.begin(),alternate.begin()+n);
                    rejects([&]{mdx::Song rejected(truncated);},"truncated track table accepted");
                }
            }
        }
        ++passed;
        {
            // q1 must not shorten the pitch trajectory relative to q8.
            auto trajectory = [&](int q) {
                mdx::Options o = opts; o.maxSeconds = 20;
                Bytes commands{253, 0, 248, uint8_t(q)};
                for (int note = 88; note >= 52; note -= 12) {
                    if (note != 88) commands.push_back(247);
                    commands.insert(commands.end(), {242, 252, 0, uint8_t(128 + note), 191});
                }
                commands.insert(commands.end(), {15, 241, 0});
                mdx::Player p(mdx::Song(oneTrack(commands)), {}, o);
                std::vector<std::tuple<uint64_t,int,int>> pitches;
                uint64_t firstOff = 0;
                p.registerTrace = [&](uint64_t t, int a, int v) {
                    if (a == 8 && !(v & 120) && t && !firstOff) firstOff = t;
                    if (a == 0x28 || a == 0x30) {
                        require(t <= 768 * 896, "portamento continued into rest/end");
                        pitches.emplace_back(t,a,v);
                    }
                };
                render(p);
                require(firstOff != 0, "portamento test did not key off");
                require(std::any_of(pitches.begin(), pitches.end(), [&](auto x) {
                    return std::get<0>(x) > firstOff && std::get<0>(x) < 192 * 896;
                }) || q == 8, "quantized release lost portamento");
                return pitches;
            };
            require(trajectory(1) == trajectory(8), "q shortened portamento duration");
        }
        ++passed;
        {
            // On hardware a voice must remain gated until every final TL is written.
            auto o = opts; o.externalFm = true;
            auto file = oneTrack({253,0,251,15,182,3,253,1,182,3,241,0});
            Bytes voice{1,7,15,1,1,1,1,0,0,0,0,31,31,31,31,0,0,0,0,0,0,0,0,5,5,5,5};
            file.insert(file.end(),voice.begin(),voice.end());
            mdx::C86Gain gain;
            std::vector<std::pair<int,int>> output;
            uint64_t time = 0;
            std::array<int,256> actual{};
            auto emit = [&](int a,int v){
                actual[a] = v;
                if (time == 4 * 896) {
                    if (a == 0x20 && (v & 192))
                        for (int op=0;op<4;++op) require(actual[0x60 + op*8] == 2, "voice unmuted before final TL");
                    output.emplace_back(a,v);
                }
            };
            gain.setGain(1,1,emit); output.clear();
            mdx::Player p(mdx::Song(file),{},o);
            p.registerTrace = [&](uint64_t t,int a,int v) {
                time = t; gain.write(a,v,emit);
            };
            render(p);
            bool gated = false, restored = false;
            int tl = 0;
            for (auto [a,v] : output) {
                if (a == 0x20 && !(v & 192)) gated = true;
                if (a >= 0x40) require(gated && !restored, "voice changed while output enabled");
                if (a >= 0x60 && a < 0x80) {
                    require(v == 2, "unadjusted loud TL leaked during voice change"); ++tl;
                }
                if (a == 0x20 && (v & 192)) restored = true;
            }
            require(gated && restored, "hardware voice gating missing");
            time = 4 * 896;
            output.clear(); gain.write(0x20,0x47,emit);
            require(output.size() == 1, "pan-only write replayed TL");
            output.clear(); gain.write(0x60,2,emit);
            require(output.empty(), "unchanged TL not suppressed");
            gain.setGain(1,1,emit); require(output.empty(), "unchanged gain replayed registers");
            gain.setGain(0,1,emit);
            require(!output.empty(), "gain mute did not update hardware");
            for (auto [a,v] : output) if (a >= 0x20 && a < 0x28) require(!(v&192), "zero gain retained pan");
            auto capture = [&](int a,int v){actual[a]=v;};
            gain.setGain(.5,1,capture);
            require(actual[0x20] == 0x47 && actual[0x60] == 10 && actual[0x78] == 10,
                    "gain restore did not restore pan and carrier attenuation");
            gain.write(0x20,0xc0,capture);
            require(actual[0x60] == 2 && actual[0x68] == 2 && actual[0x70] == 2 && actual[0x78] == 10,
                    "algorithm change retained gain on modulators");
        }
        ++passed;
        {
            mdx::MonitorFrame frame;
            for (int ch = 0; ch < 16; ++ch) frame.waveform[ch].fill(float(ch + 1));
            auto selected = mdx::instrumentWaveform(frame, 2, 0);
            auto audition = mdx::instrumentWaveform(frame, 2, (1u << 2) | (1u << 5) | (1u << 7));
            auto empty = mdx::instrumentWaveform(frame, -1, 0);
            for (size_t n = 0; n < selected.size(); ++n) {
                require(selected[n] == 3, "instrument waveform included unrelated song parts");
                require(audition[n] == 17, "instrument waveform missed audition or counted selected part twice");
                require(empty[n] == 0, "instrument waveform without selected/audition part not empty");
            }
        }
        ++passed;
        // Bound malformed inputs under ASan/UBSan without importing any music.
        std::mt19937 random(0x68000);
        mdx::Options fuzz = opts;
        fuzz.maxSeconds = .01;
        for (int i = 0; i < 1000; ++i) {
            auto bytes = basic;
            for (int k = 0; k < 1 + int(random() % 5); ++k)
                bytes[random() % bytes.size()] = static_cast<uint8_t>(random());
            try {
                mdx::Player p(mdx::Song(bytes), {}, fuzz);
                render(p, 257);
            } catch (const std::exception &) {
            }
        }
        ++passed;
        std::cout << "PASS " << passed << " test groups (including 1000 malformed MDX mutations)\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FAIL: " << e.what() << "\n";
        return 1;
    }
}
