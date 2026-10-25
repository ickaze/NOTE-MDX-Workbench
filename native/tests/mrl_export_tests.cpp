#include "mrl_export.h"
#include "mrl.h"
#include <iostream>
#include <tuple>
#include <chrono>
void require(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
int main(int argc, char **argv) {
    try {
        if (argc == 3) {
            auto input = std::filesystem::path(argv[1]);
            auto output = std::filesystem::path(argv[2]);
            for (auto &entry : std::filesystem::recursive_directory_iterator(input)) {
                if (!entry.is_regular_file()) continue;
                auto extension = entry.path().extension().string();
                std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);
                if (extension != ".mdx") continue;
                auto target = output / std::filesystem::relative(entry.path(), input);
                target.replace_extension(".mrl");
                try { mdx::exportRegisterLog(entry.path(), target, entry.path(), {}, 2, [](int) {}); }
                catch (const std::exception &error) { std::cerr << entry.path() << ": " << error.what() << '\n'; }
            }
            return 0;
        }
        namespace fs = std::filesystem;
        const auto root = fs::temp_directory_path() / ("workbench-mrl-tests-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        fs::create_directory(root);
        struct Cleanup { fs::path p; ~Cleanup() { std::error_code e; fs::remove_all(p,e); } } cleanup{root};
        const auto source = fs::path(__FILE__).parent_path().parent_path()/"samples"/"DEMO.MDX";
        const auto output = root/"demo.mrl";
        mdx::exportRegisterLog(source, output, source, {}, 2, [](int) {});
        std::ifstream in(output,std::ios::binary);
        const auto header = mdx::readMrlHeader(in,fs::file_size(output));
        require(header.minor == 2 && header.hz == 4000000 && header.loops == 2, "MRL profile mismatch");
        require(header.count > 0 && header.embedded && header.pdxSize > 0, "embedded PDX missing");
        require(header.loopEnd > header.loopStart && header.loopEvent < header.loopEndEvent, "loop bounds missing");
        auto song = mdx::Song(mdx::readFile(source));
        mdx::Options options; options.loops=2; options.fadeSeconds=0; options.maxSeconds=0;
        options.sampleRate=62500; options.resampleOutput=false; options.skipFmSynthesis=true; options.skipPcmSynthesis=true;
        std::vector<mdx::MrlRecord> expected;
        options.registerLogOutput=[&](uint64_t t,int a,int v) { expected.push_back({t*64,0,uint8_t(a),uint8_t(v),0,0}); };
        options.pcmLogOutput=[&](uint64_t t,int ch,int op,uint32_t arg,int format) { expected.push_back({t*64,uint8_t(ch+1),uint8_t(op),uint8_t(format),0,arg}); };
        mdx::Player player(song,{},options); std::array<int16_t,2048> scratch{};
        while(player.render(scratch.data(),1024)) {}
        require(header.count==expected.size(),"record count mismatch");
        in.seekg(header.offset);
        for(const auto &want : expected) {
            std::array<uint8_t,16> row{}; require(bool(in.read(reinterpret_cast<char*>(row.data()),16)),"truncated log");
            const auto got=mdx::readMrlRecord(row.data(),header);
            require(std::tie(got.time,got.chip,got.op,got.value,got.argument)==std::tie(want.time,want.chip,want.op,want.value,want.argument),"logical event ordering/value mismatch");
        }
        const auto original=mdx::readFile(output);
        bool rejected=false;
        try { mdx::exportRegisterLog(source,output,source,{},2,[](int){}); } catch (...) { rejected=true; }
        require(rejected && mdx::readFile(output)==original,"existing output overwritten");
        auto cancelled=root/"cancelled.mrl";
        int calls=0;
        try { mdx::exportRegisterLog(source,cancelled,source,{},2,[&](int){ if(++calls>5) throw std::runtime_error("Cancelled"); }); } catch (...) {}
        require(!fs::exists(cancelled),"cancelled output published");
        for(auto &entry:fs::directory_iterator(root)) require(entry.path()==output,"temporary files survived cancellation");
        std::cout << "MRL 1.2, embedded PDX, logical events, loops, overwrite and cancellation passed\n";
        return 0;
    } catch(const std::exception &error) { std::cerr<<error.what()<<'\n'; return 1; }
}
