#!/usr/bin/env python3
"""Exercise actual App save methods with a reentrant modal/timer test harness.
Requires Python 3 and g++; does not execute the Windows GUI.
"""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'src/App.cpp').read_text()

def method(signature):
    start = source.index(signature)
    opening = source.index('{', start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]

prefix = r'''
#include <cassert>
#include <filesystem>
#include <fstream>
#include <string>
#include <iostream>
namespace fs = std::filesystem;
using HWND = int;
constexpr int MB_YESNO=1, MB_ICONQUESTION=2, IDYES=6;
bool enabled=true, writeSuccess=true;
int prompts=0, writes=0, answer=IDYES;
unsigned long long clockMs=10000;
auto TickMs() { return clockMs; }
bool IsWindowEnabled(HWND) { return enabled; }
const wchar_t* Tr(const wchar_t*) { return L""; }
std::wstring BaseName(const std::wstring& p) { return fs::path(p).filename().wstring(); }
struct App {
    bool restoringHistory_=false, dirty_=true, saveInProgress_=false, autoSaveBlocked_=false;
    unsigned projectOperationDepth_=0;
    unsigned long long changedAt_=0;
    HWND hwnd_=1;
    int prj_=0;
    std::wstring projectPath_, lastProject_, target;
    void CaptureUiState() {}
    void SaveEditorNow() {}
    void Log(const std::wstring&) {}
    std::wstring CurrentProjectPath() const { return target; }
    bool SaveCurrentNow();
    void SaveAuto();
    void MarkDirty();
};
App* active;
int MessageBoxW(HWND, const wchar_t*, const wchar_t*, int) {
    ++prompts;
    assert(prompts < 10);
    // Simulate a modal loop delivering more idle timers after two seconds.
    clockMs += 3000;
    active->SaveAuto();
    assert(!active->SaveCurrentNow());
    return answer;
}
bool SaveProject(const std::wstring&, int, std::wstring&) {
    ++writes;
    active->SaveAuto();
    return writeSuccess;
}
'''
# Compile the exact target-selection block used by ImportPdx as well.
import_start = source.index('    auto stem = pdxAbs.stem().wstring();', source.index('void App::ImportPdx('))
import_end = source.index('    auto confirmOverwrite', import_start)
prefix += r'''
struct ImportTargetProbe {
    struct { std::wstring pdxName; } prj_;
    fs::path hostProjectDirectory_;
    std::wstring current;
    std::wstring CurrentProjectPath() const { return current; }
    fs::path choose(const fs::path& pdxAbs, bool ask) {
''' + source[import_start:import_end] + r'''
        return targetProject;
    }
};
'''
main = r'''
int main(int argc, char** argv) {
    ImportTargetProbe import;
    import.prj_.pdxName=L"original";
    import.current=L"/music/original.pdxedit";
    assert(import.choose(L"/samples/replacement.pdx",true)==fs::path(L"/music/original.pdxedit"));
    // Startup import initializes from the source filename.
    assert(import.choose(L"/samples/replacement.pdx",false)==fs::path(L"/samples/replacement.pdxedit"));
    import.hostProjectDirectory_=L"/mus";
    assert(import.choose(L"/samples/replacement.pdx",false)==fs::path(L"/mus/replacement.pdxedit"));
    import.prj_.pdxName.clear();
    assert(import.choose(L"/samples/replacement.pdx",true)==fs::path(L"/mus/replacement.pdxedit"));
    App app; active=&app;
    app.target=(fs::path(argv[1])/"existing.pdxedit").wstring();
    std::ofstream(fs::path(app.target)) << "existing";
    app.projectPath_=(fs::path(argv[1])/"other.pdxedit").wstring();
    app.SaveAuto();
    assert(prompts==1 && writes==1 && !app.dirty_ && !app.saveInProgress_);
    app.projectPath_.clear(); app.MarkDirty(); clockMs+=3000; answer=0;
    app.SaveAuto();
    assert(prompts==2 && writes==1 && app.dirty_ && app.autoSaveBlocked_);
    for(int i=0;i<20;++i) { clockMs+=3000; app.SaveAuto(); }
    assert(prompts==2 && writes==1);
    // A new edit starts a fresh idle interval; the disabled owner blocks all saves.
    app.MarkDirty(); app.SaveAuto(); assert(prompts==2);
    clockMs+=3000; enabled=false; app.SaveAuto(); assert(prompts==2);
    enabled=true; app.projectOperationDepth_=1; app.SaveAuto(); assert(prompts==2);
    app.projectOperationDepth_=0; answer=IDYES; app.SaveAuto();
    assert(prompts==3 && writes==2 && !app.dirty_);
    // A failed write remains dirty, without retrying every idle timer.
    app.MarkDirty(); clockMs+=3000; writeSuccess=false; app.SaveAuto();
    assert(writes==3 && app.dirty_ && app.autoSaveBlocked_);
    clockMs+=3000; app.SaveAuto(); assert(writes==3);
    // Explicit save can retry after a rejection/failure.
    writeSuccess=true; assert(app.SaveCurrentNow());
    assert(writes==4 && !app.dirty_ && !app.autoSaveBlocked_);
    std::cout << "actual App save methods: modal reentry, idle pause, No, failure and retry passed\n";
}
'''
code = prefix + '\n'.join(method(s) for s in (
    'void App::MarkDirty()', 'bool App::SaveCurrentNow()', 'void App::SaveAuto()')) + main
with tempfile.TemporaryDirectory() as tmp:
    path = Path(tmp)
    (path / 'check.cpp').write_text(code)
    subprocess.run(['g++', '-std=c++17', '-O2', str(path / 'check.cpp'), '-o', str(path / 'check')], check=True)
    subprocess.run([str(path / 'check'), tmp], check=True)
