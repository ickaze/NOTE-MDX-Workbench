// SPDX-License-Identifier: 0BSD
// Copyright (C) 2026 by I.C.KaZe
#pragma once
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>
struct NdsifVoice {
    std::vector<float> samples;
    uint32_t sourceRate = 15625;
    size_t sourceSamples = 0;
    uint64_t start = 0;
    size_t resumeFrame = 0;
    size_t generated = 0;
    std::atomic<size_t> position{0};
    std::atomic<bool> stopped{false};
    std::atomic<bool> finished{false};
};

// One shared PCM queue. Only the playback thread advances the stream; editor
// note events and connection changes use the same mutex as voice publication.
class NdsifPreview {
    std::mutex mutex_;
    std::vector<std::shared_ptr<NdsifVoice>> voices_;
    uint64_t generated_ = 0;
    bool enabled_ = false;
public:
    void enable(bool value) {
        std::lock_guard<std::mutex> guard(mutex_);
        enabled_ = value; generated_ = 0;
        for (auto& v : voices_) v->stopped = true;
        voices_.clear();
    }
    std::shared_ptr<NdsifVoice> play(const std::vector<float>& samples, uint32_t rate) {
        if (samples.empty() || !rate) return {};
        auto v=std::make_shared<NdsifVoice>(); v->sourceRate=rate; v->sourceSamples=samples.size();
        size_t count=std::max<size_t>(1,size_t(std::llround(samples.size()*15625.0/rate)));
        v->samples.resize(count);
        for(size_t i=0;i<count;++i) {
            double x=i*double(rate)/15625; size_t a=std::min(size_t(x),samples.size()-1), b=std::min(a+1,samples.size()-1);
            v->samples[i]=float(samples[a]*(1-(x-a))+samples[b]*(x-a));
        }
        std::lock_guard<std::mutex> guard(mutex_);
        if (!enabled_) return {};
        v->start=generated_; voices_.push_back(v); return v;
    }
    void beginStream() {
        std::lock_guard<std::mutex> guard(mutex_); generated_=0;
        for(auto& v:voices_) {
            v->resumeFrame=std::min(v->samples.size(),size_t(v->position.load()*15625ULL/v->sourceRate));
            v->generated=v->resumeFrame; v->start=0;
        }
    }
    void consumed(uint64_t frames) {
        std::lock_guard<std::mutex> guard(mutex_);
        for(auto& v:voices_) {
            uint64_t n=v->resumeFrame+(frames>v->start?frames-v->start:0);
            v->position=n>=v->samples.size()?v->sourceSamples:std::min(v->sourceSamples,size_t(n*v->sourceRate/15625));
            if(n>=v->samples.size()) v->finished=true;
        }
    }
    void mix(int16_t* stereo,size_t frames) {
        std::lock_guard<std::mutex> guard(mutex_);
        for(size_t i=0;i<frames;++i) {
            double sum=0;
            for(auto& v:voices_) if(!v->stopped && !v->finished && v->generated<v->samples.size()) sum+=v->samples[v->generated++];
            int64_t add=std::llround(sum*32767);
            for(int ch=0;ch<2;++ch) stereo[2*i+ch]=int16_t(std::clamp<int64_t>(int64_t(stereo[2*i+ch])+add,-32768,32767));
        }
        generated_+=frames;
        voices_.erase(std::remove_if(voices_.begin(),voices_.end(),[](const auto& v){return v->stopped||v->finished;}),voices_.end());
    }
};
