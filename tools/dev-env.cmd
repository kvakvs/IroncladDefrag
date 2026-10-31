@echo off
rem Shared build environment for build.bat, test.bat and quality.bat (call it; it has no setlocal on purpose).
rem Sets ICD_BUILD_DIR for the chosen toolchain: clang-cl (default, build\) or MSVC (ICD_TOOLCHAIN=msvc, build-msvc\).
rem Enters the vcvars64 environment once (Windows SDK, MSVC STL, rc, ninja) and puts LLVM first on PATH.
for %%i in ("%~dp0..") do set "ICD_ROOT=%%~fi"
set "ICD_BUILD_DIR=%ICD_ROOT%\build"
if /i "%ICD_TOOLCHAIN%"=="msvc" set "ICD_BUILD_DIR=%ICD_ROOT%\build-msvc"
if defined ICD_DEV_ENV exit /b 0

rem When started from PowerShell 7, PSModulePath lists its modules first and Windows PowerShell (used for
rem tools\fetch-wx.ps1) then fails to load cmdlets such as Get-FileHash. Empty, it rebuilds its defaults.
set "PSModulePath="

set "ICD_VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "ICD_VSDIR="
for /f "usebackq delims=" %%i in (`"%ICD_VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "ICD_VSDIR=%%i"
if defined ICD_VSDIR goto vcvars
echo Visual Studio with the C++ x64 tools was not found.
exit /b 1

:vcvars
rem vcvars64 prints a harmless "'vswhere.exe' is not recognized" line; its exit code still counts.
call "%ICD_VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul 2>nul
if errorlevel 1 exit /b 1

rem PATH holds "(x86)", so it is changed outside any parenthesized block.
if /i "%ICD_TOOLCHAIN%"=="msvc" goto done
if exist "%ProgramFiles%\LLVM\bin\clang-cl.exe" goto llvm
echo clang-cl not found in "%ProgramFiles%\LLVM\bin". Install LLVM or set ICD_TOOLCHAIN=msvc.
exit /b 1
:llvm
set "PATH=%ProgramFiles%\LLVM\bin;%PATH%"
:done
set "ICD_DEV_ENV=1"
exit /b 0
