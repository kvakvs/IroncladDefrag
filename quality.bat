@echo off
rem Quality gate: Lizard (cyclomatic complexity) and clang-tidy over src/.
rem Usage: quality.bat [changed^|all]   (default: changed = files changed since HEAD plus includers of changed headers)
rem Creates .venv-quality from tools\requirements-quality.txt and a Ninja + MSVC build in build\quality on first use.
setlocal
set "ROOT=%~dp0"
set "SCOPE=%~1"
if "%SCOPE%"=="" set "SCOPE=changed"
set "TARGET="
if /i "%SCOPE%"=="changed" set "TARGET=check-quality"
if /i "%SCOPE%"=="all" set "TARGET=check-quality-all"
if not defined TARGET (
    echo Unknown scope "%SCOPE%". Use changed or all.
    exit /b 2
)

rem Pinned analyzers; reinstall whenever the requirements file changes.
set "VENV=%ROOT%.venv-quality"
set "REQS=%ROOT%tools\requirements-quality.txt"
if not exist "%VENV%\Scripts\python.exe" (
    python -m venv "%VENV%" || exit /b 1
)
fc /b "%REQS%" "%VENV%\icd-requirements.txt" >nul 2>nul
if errorlevel 1 (
    "%VENV%\Scripts\python.exe" -m pip install --disable-pip-version-check -q -r "%REQS%" || exit /b 1
    copy /y "%REQS%" "%VENV%\icd-requirements.txt" >nul
)

rem MSVC environment: cl for the compile database, INCLUDE for clang-tidy's STL and SDK headers.
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "VSDIR="
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%i"
if not defined VSDIR (
    echo Visual Studio with the C++ x64 tools was not found.
    exit /b 1
)
rem vcvars64 prints a harmless "'vswhere.exe' is not recognized" line; its exit code still counts.
call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul 2>nul || exit /b 1

powershell -NoProfile -ExecutionPolicy Bypass -File "%ROOT%tools\fetch-wx.ps1" >nul || exit /b 1

rem No precompiled headers here: clang-tidy cannot read MSVC .pch files.
if not exist "%ROOT%build\quality\CMakeCache.txt" (
    cmake -S "%ROOT%." -B "%ROOT%build\quality" -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=cl ^
        -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DCMAKE_DISABLE_PRECOMPILE_HEADERS=ON || exit /b 1
)
rem -k 0: run every check even when one fails, so all findings show at once.
cmake --build "%ROOT%build\quality" --target %TARGET% -- -k 0 || exit /b 1
