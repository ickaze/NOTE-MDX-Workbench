@echo off
setlocal
cd /d "%~dp0"
where cmake >nul 2>nul
if errorlevel 1 (
    echo CMake 3.20 or later is required. Add CMake to PATH.
    exit /b 1
)
cmake -S . -B build-vs2022-cpp -G "Visual Studio 17 2022" -A x64 -DBUILD_TESTING=OFF
if errorlevel 1 exit /b 1
cmake --build build-vs2022-cpp --config Release --parallel 2
if errorlevel 1 exit /b 1
if not exist bin mkdir bin
copy /y build-vs2022-cpp\Release\NOTE-MDX-Workbench.exe bin\NOTE-MDX-Workbench.exe >nul
if errorlevel 1 exit /b 1
copy /y build-vs2022-cpp\Release\workbench_core.dll bin\workbench_core.dll >nul
if errorlevel 1 exit /b 1
if not exist bin\pdx_languages mkdir bin\pdx_languages
copy /y native\pdxeditor\languages\*.lang bin\pdx_languages\ >nul
if errorlevel 1 exit /b 1
echo Built C++ bin\NOTE-MDX-Workbench.exe and bin\workbench_core.dll
endlocal
