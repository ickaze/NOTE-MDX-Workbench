#include "notemdx/compiler.hpp"
#include "player.h"
#include <iostream>
#include <fstream>
#include <map>
#include <filesystem>
#include <stdexcept>

namespace
{
int checks = 0;
void check(bool condition, const char* message)
{
    ++checks;
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}
notemdx::Result mapped(const std::string& text)
{
    notemdx::Options options;
    options.editor_mapping = true;
    auto result = notemdx::compile({"test.mus", text}, options);
    check(result.ok(), "compile mapping");
    for (const auto& event : result.source_events)
    {
        check(event.offset < result.mdx.size(), "source offset in MDX");
        check(result.mdx.at(event.offset) < 224, "source offset is timed opcode");
    }
    return result;
}
}
int main()
{
    try
    {
        {
            auto file=std::filesystem::path(__FILE__).parent_path().parent_path()/"samples/STAGE1.mus";
            std::ifstream input(file,std::ios::binary);std::string text((std::istreambuf_iterator<char>(input)),{});
            auto compiled=notemdx::compile({"STAGE1.mus",text});
            check(compiled.ok(),"STAGE1 compiles");
            mdx::Options options;options.loops=1;options.maxSeconds=0;
            mdx::Player stage(mdx::Song(compiled.mdx),{},options);
            auto score=stage.analyze({},true);
            std::array<double,16> previous{};
            for(const auto& note:score.notes){
                check(note.begin>=previous[note.channel],"STAGE1 predicted notes must not overlap");
                check(note.end>note.begin,"STAGE1 predicted duration positive");
                if(score.trackLoopEnd[note.channel]>0)
                    check(note.end<=score.trackLoopEnd[note.channel]+1e-6,"STAGE1 must not collect duplicate cycles");
                previous[note.channel]=note.end;
            }
            std::cout<<"STAGE1: "<<score.notes.size()<<" first-cycle note segments, "<<score.duration<<" seconds\n";
        }
        {
            auto compiled=notemdx::compile({"loops.mus","A L c8 r8\nB r1 L d8 r8\n"});
            check(compiled.ok(),"different track loop lengths compile");
            mdx::Player song(mdx::Song(compiled.mdx),{},{});auto score=song.analyze({},true);
            int first=0;for(const auto& note:score.notes)if(note.channel==0)++first;
            check(first==1,"short loop must not be collected repeatedly while another track rests");
            check(score.trackLoopEnd[0]>score.trackLoopStart[0],"track loop metadata");
        }
        auto result = mapped("A [c4/d8]3\nA L {efg}%10 r4\n");
        std::map<int, uint64_t> lines;
        for (const auto& event : result.source_events)
        {
            lines[event.line] += event.steps;
        }
        check(lines[1] == 192, "repeat escape row total");
        check(lines[2] == 58, "tuplet row total");
        check(result.statistics.tracks[0].total_steps.value == 250, "whole track total");
        check(result.statistics.tracks[0].loop_steps->value == 58, "loop total");
        auto macro = mapped("i=\"c8d8\"\nA i e4\n");
        check(macro.source_events.size() == 3, "macro events");
        check(macro.source_events[0].line == 2, "macro call line");
        check(macro.source_events[0].column == 3, "macro call column");
        check(macro.source_events[1].column == 3, "macro second note at call");
        auto nested = mapped("A [[{cd}%9]2/e4]3\n");
        uint64_t sum = 0;
        for (const auto& event : nested.source_events)
        {
            sum += event.steps;
        }
        check(sum == nested.statistics.tracks[0].total_steps.value, "nested mapping total");
        const auto escaped_once = mapped("A [c4/d4]1\n");
        check(escaped_once.source_events[1].steps == 0, "skipped final escape has zero row contribution");
        notemdx::Options selected;
        selected.editor_mapping = true;
        selected.selection = notemdx::SelectionRange{1, 6, 1, 12};
        auto selection = notemdx::compile({"test.mus", "A c4 d8 e8 L f4\n"}, selected);
        check(selection.ok(), "selection mapping compile");
        check(selection.source_events.size() == 2, "selection mapping excludes prefix and suffix");
        check(selection.source_events[0].column == 6, "selection retains original column");
        auto monitor = std::make_shared<mdx::Monitor>();
        mdx::Options playback;
        playback.monitor = monitor;
        playback.maxSeconds = 0;
        playback.loops = 1;
        mdx::Player player(mdx::Song(selection.mdx), {}, playback);
        const auto score = player.analyze({}, true);
        check(score.notes.size() == 2, "score contains two selected notes without rendering");
        check(score.notes[0].begin == 0, "score begins at transport zero");
        check(score.notes[0].end > score.notes[0].begin, "score note duration");
        check(score.notes[1].begin >= score.notes[0].end, "score note chronology");
        check(score.notes[0].channel == 0, "score channel mapping");
        check(player.seconds() == 0, "score analysis does not advance playback");
        int16_t audio[2048]{};
        player.render(audio, 1024);
        check(monitor->frame.sourceOffset[0] == selection.source_events[0].offset, "playback offset maps to first note");
        check(player.seek(.25), "seek for mapping");
        player.render(audio, 1024);
        bool known = false;
        for (const auto& event : selection.source_events)
        {
            known |= (event.offset == monitor->frame.sourceOffset[0]);
        }
        check(known, "seek preserves a known source event");
        notemdx::Options included;
        included.editor_mapping = true;
        included.include_loader = [](const std::string&, const std::string&, notemdx::Source& source, std::string&)
        {
            source = {"part.mus", "A c4 d8\n"};
            return true;
        };
        auto include = notemdx::compile({"test.mus", "#include part.mus\nA e8\n"}, included);
        check(include.ok(), "include mapping");
        check(include.source_events[0].file == "test.mus", "include maps to parent source");
        check(include.source_events[0].line == 1, "include maps to include directive");
        check(include.source_events[0].column == 1, "include directive column");
        const auto wav = std::filesystem::temp_directory_path() / "workbench-loop-test.wav";
        {
            std::ofstream output(wav, std::ios::binary);
            const std::string placeholder(84, '\0');
            output.write(placeholder.data(), placeholder.size());
        }
        mdx::appendWavLoop(wav, 48000, 2, 8);
        std::ifstream input(wav, std::ios::binary);
        const std::string bytes((std::istreambuf_iterator<char>(input)), {});
        check(bytes.size() == 152, "WAV loop chunk size");
        check(bytes.substr(84, 4) == "smpl", "WAV sampler chunk");
        check(static_cast<unsigned char>(bytes[4]) == 144, "WAV RIFF size updated");
        check(static_cast<unsigned char>(bytes[136]) == 2, "WAV loop begin");
        check(static_cast<unsigned char>(bytes[140]) == 7, "WAV inclusive loop end");
        input.close();
        std::filesystem::remove(wav);
        std::cout << checks << " mapping assertions passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
