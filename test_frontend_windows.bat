@echo off
setlocal
cd /d "%~dp0"
where pwsh >nul 2>nul
if errorlevel 1 (
    echo PowerShell 7 is required for the isolated test harness.
    exit /b 1
)
set WB_SOURCE_DIR=%CD%
pwsh -NoProfile -File scripts\cpp_gui_smoke.ps1
exit /b %ERRORLEVEL%
