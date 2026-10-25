# C ABI 1 validation (0.11b)

- Linux x86-64: g++ 13, C++17 shared library; C11 client compilation and execution passed.
- C ABI: partitioned buffers, nonzero output, time remainder, clone, reset, s16 silence, event scheduling, invalid input before mutation tested.
- 4096 stereo float frames with master-clock events match direct C++ core + resampler exactly; status/IRQ agree.
- 14 public C function names checked against exports in the Linux shared library.
- C client example rendered successfully. Wrapper compiled with -Wall -Wextra -Wpedantic.
- Original ym2151.cpp, ym2151.hpp, measured_lfo.hpp and license/notices remain byte-identical to the input archive.
- Windows x86/x64 binaries NOT built in this environment. VS2022 batch builds both and runs C ABI and direct-core-equivalence tests on Windows. CMake integration supplied but not executed here (CMake unavailable).
- No Windows DLL or import library is represented as prebuilt in this package.

## Build discovery correction

Removed the mandatory ProgramFiles(x86)/Installer/vswhere.exe dependency. Explicit root/vcvarsall paths,
developer environment variables, and both Program Files layouts are supported. Batch syntax and control flow
were inspected; execution on Windows remains untested in this environment. Synthesis and DLL API code unchanged.

## MSVC import-library command-line correction

The supplied Windows log confirms VS2022 x86 environment initialization and successful DLL/import-library creation. The subsequent C test compilation failed because global /TC caused the .lib to be interpreted as C source. Removed global /TC (the .c extension selects C), and moved import-library inputs after /link in both C clients and the C++ equivalence test. All three command lines were inspected to ensure no .lib reaches the compiler input list. Windows execution after this correction is not yet verified.
