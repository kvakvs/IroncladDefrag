@echo off
rem Builds and runs the unit tests. Usage: test.bat [Debug^|Release^|All]   (default: All)
rem Uses the same toolchain as build.bat (clang-cl, or MSVC with ICD_TOOLCHAIN=msvc).
setlocal
set "ROOT=%~dp0"
set "CONFIG=%~1"
if "%CONFIG%"=="" set "CONFIG=All"
if /i not "%CONFIG%"=="All" goto run
call "%~f0" Debug
if errorlevel 1 exit /b 1
call "%~f0" Release
if errorlevel 1 exit /b 1
exit /b 0

:run
call "%ROOT%build.bat" %CONFIG%
if errorlevel 1 exit /b 1
call "%ROOT%tools\dev-env.cmd"
if errorlevel 1 exit /b 1
ctest --test-dir "%ICD_BUILD_DIR%" -C %CONFIG% --output-on-failure --no-tests=error --parallel 8
if errorlevel 1 exit /b 1
echo Tests passed: %CONFIG%
exit /b 0
