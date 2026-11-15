// SPDX-License-Identifier: 0BSD
// Copyright (C) 2026 by I.C.KaZe
#include "text_logic.h"
#include "compilation_cache.h"
#include "notemdx/compiler.hpp"
#include <iostream>
#include <algorithm>
#include <stdexcept>
using namespace workbench;
static int checks = 0;
void check(bool value, const char* message)
{
    ++checks;
    if (!value) throw std::runtime_error(message);
}
int main()
{
    try
    {
        auto dimmed=[](const std::string& source,int at){auto spans=editorDimSpans(source);return std::any_of(spans.begin(),spans.end(),[&](auto p){return p.first<=at&&at<p.second;});};
        std::string ignored="A c4 ?d4? e4 !f4\nB g4\nA a4\n";
        check(dimmed(ignored,int(ignored.find("d4")))&&!dimmed(ignored,int(ignored.find("e4"))),"question range dimmed, subsequent MML bright");
        check(dimmed(ignored,int(ignored.find("f4")))&&dimmed(ignored,int(ignored.find("a4"))),"bang dims remainder of same track across lines");
        check(!dimmed(ignored,int(ignored.find("g4"))),"bang does not dim another track");
        check(dimmed("A ?c4\nA d4\n",9),"unmatched question dims following track line");
        check(logNewlines(L"one\ntwo\r\nthree\rfour")==L"one\r\ntwo\r\nthree\r\nfour","log LF CRLF CR normalized once");
        check(logNewlines(logNewlines(L"one\ntwo"))==logNewlines(L"one\ntwo"),"log normalization is idempotent");
        const std::string text = " A c4\n; comment\n// comment\n@0={\n  31,0,0 // continuation\n}\n#nlist\nA c4\n#list\nABC c4\nmacro=\"c4\"\nA macro\n\n";
        const auto lines = classifyEditorLines(text);
        check(lines.size() == 14, "line count");
        check(lines[0].comment && !lines[0].track, "indented track is a comment");
        check(lines[1].comment && lines[2].comment, "explicit comments");
        check(!lines[3].comment && !lines[4].comment && !lines[5].comment, "definition continuation stays bright");
        check(lines[7].comment && !lines[7].track && lines[9].track, "nlist/list track eligibility");
        check(!lines[10].track && lines[11].track, "macro definition versus track call");
        const auto quoted = classifyEditorLines("@@0=\"{;*//}\"\n A c4\nA c4\n");
        check(quoted[1].comment && quoted[2].track, "quoted macro braces do not open definitions");
        const auto japanese = classifyEditorLines(u8" 日本語\r\nA c4\r\n");
        check(japanese[0].comment && japanese[1].begin == 6, "UTF16 and CRLF comment positions");
        std::map<int, TrackSteps> steps;
        steps[1][0] = 999; steps[8][0] = 888; steps[10][0] = 48; steps[11][0] = 777; steps[12][0] = 96;
        const auto gutter = buildGutterText(text, steps);
        check(gutter.find("999") == std::string::npos && gutter.find("888") == std::string::npos && gutter.find("777") == std::string::npos, "non-track rows hide even mapped macro/include steps");
        check(gutter.find("10  48") != std::string::npos && gutter.find("12  96") != std::string::npos, "active column-zero tracks keep steps");
        const std::string voice = "@0={31,0,0,8,0,48,0,1,0,0,0,31,0,0,8,0,48,0,1,0,0,0,31,0,0,8,0,48,0,1,0,0,0,31,0,0,8,0,48,0,1,0,0,0,7,0,15}\n";
        const std::string song = voice + "#ex-pcm\nA @0 c4 L [[d8/e16]3 f8]4\nP @0 c4 L [@1 d8 / @2 e4]3\n";
        notemdx::Options full;
        full.editor_mapping = true;
        auto normal = notemdx::compile({"editor.mus",song},full);
        auto fast = full; fast.editor_steps = false;
        auto playback = notemdx::compile({"editor.mus",song},fast);
        check(normal.ok() && playback.ok() && normal.mdx == playback.mdx, "fast playback preserves exact MDX");
        check(normal.source_events.size() == playback.source_events.size(), "fast playback preserves navigation events");
        for (std::size_t i = 0; i < normal.source_events.size(); ++i)
            check(normal.source_events[i].offset == playback.source_events[i].offset && normal.source_events[i].line == playback.source_events[i].line, "navigation offset and line preserved");
        for (std::size_t i = 0; i < 16; ++i)
            check(normal.statistics.tracks[i].total_steps.value == playback.statistics.tracks[i].total_steps.value, "fast track totals preserved");
        check(normal.statistics.pcm_banks.size() == playback.statistics.pcm_banks.size(), "PCM bank collection retained");
        CompilationCache<int> cache;
        CompilationCache<int>::Bytes dependency{1,2,3};
        auto loader = [&](const std::string&) { return dependency; };
        cache.put("a", "text", true, {{"include",dependency}}, std::make_shared<const int>(42));
        check(*cache.get("a","text",false,loader) == 42, "full analysis serves playback cache");
        check(!cache.get("a","edited",false,loader) && !cache.get("b","text",false,loader), "cache compares document text and path");
        dependency[1] = 4;
        check(!cache.get("a","text",false,loader), "same-size changed include invalidates cache");
        cache.put("a","new",false,{},std::make_shared<const int>(7));
        check(!cache.get("a","new",true,loader) && *cache.get("a","new",false,loader) == 7, "playback cache cannot masquerade as complete line analysis");
        auto missing = [](const std::string&) -> CompilationCache<int>::Bytes { throw std::runtime_error("missing"); };
        check(!cache.get("a","text",false,missing), "missing dependency invalidates cache");
        std::cout << checks << " editor analysis checks passed\n";
        return 0;
    }
    catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
