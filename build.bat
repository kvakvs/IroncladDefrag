@echo off
rem Builds IroncladDefrag and icd-cli.  Usage: build.bat [Debug^|Release^|All]   (default: Debug)
rem Fetches wxWidgets first (no-op when present) and configures build\ on first use.
setlocal
set "ROOT=%~dp0"
set "CONFIG=%~1"
if "%CONFIG%"=="" set "CONFIG=Debug"
if /i "%CONFIG%"=="All" (
    call "%~f0" Debug || exit /b 1
    call "%~f0" Release || exit /b 1
    exit /b 0
)
if /i not "%CONFIG%"=="Debug" if /i not "%CONFIG%"=="Release" (
    echo Unknown configuration "%CONFIG%". Use Debug, Release or All.
    exit /b 2
)

set "CMAKE=cmake"
where cmake >nul 2>nul || set "CMAKE=%ProgramFiles%\CMake\bin\cmake.exe"

powershell -NoProfile -ExecutionPolicy Bypass -File "%ROOT%tools\fetch-wx.ps1" || exit /b 1

if not exist "%ROOT%build\CMakeCache.txt" (
    "%CMAKE%" -S "%ROOT%." -B "%ROOT%build" -G "Visual Studio 18 2026" -A x64 || exit /b 1
)
"%CMAKE%" --build "%ROOT%build" --config %CONFIG% --parallel || exit /b 1
echo Built %CONFIG%: %ROOT%build\bin\%CONFIG%
