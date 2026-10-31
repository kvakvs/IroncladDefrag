@echo off
rem Builds IroncladDefrag, icd-cli and the tests.  Usage: build.bat [Debug^|Release^|All]   (default: Debug)
rem Default compiler: clang-cl with Ninja Multi-Config in build\. set ICD_TOOLCHAIN=msvc for MSVC in build-msvc\.
rem Fetches wxWidgets first (no-op when present) and configures the build directory on first use.
rem Note: no "exit /b" inside ( ) blocks in these scripts; cmd loses the exit code there.
setlocal
set "ROOT=%~dp0"
set "CONFIG=%~1"
if "%CONFIG%"=="" set "CONFIG=Debug"
if /i "%CONFIG%"=="All" goto all
if /i "%CONFIG%"=="Debug" goto setup
if /i "%CONFIG%"=="Release" goto setup
echo Unknown configuration "%CONFIG%". Use Debug, Release or All.
exit /b 2

:all
call "%~f0" Debug
if errorlevel 1 exit /b 1
call "%~f0" Release
if errorlevel 1 exit /b 1
exit /b 0

:setup
call "%ROOT%tools\dev-env.cmd"
if errorlevel 1 exit /b 1
powershell -NoProfile -ExecutionPolicy Bypass -File "%ROOT%tools\fetch-wx.ps1" >nul
if errorlevel 1 exit /b 1

if exist "%ICD_BUILD_DIR%\CMakeCache.txt" goto build
if /i "%ICD_TOOLCHAIN%"=="msvc" goto configure_msvc
cmake -S "%ROOT%." -B "%ICD_BUILD_DIR%" -G "Ninja Multi-Config" -DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl ^
    -DCMAKE_LINKER_TYPE=LLD -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
if errorlevel 1 exit /b 1
goto build

:configure_msvc
cmake -S "%ROOT%." -B "%ICD_BUILD_DIR%" -G "Visual Studio 18 2026" -A x64
if errorlevel 1 exit /b 1

:build
cmake --build "%ICD_BUILD_DIR%" --config %CONFIG% --parallel
if errorlevel 1 exit /b 1
echo Built %CONFIG%: %ICD_BUILD_DIR%\bin\%CONFIG%
exit /b 0
