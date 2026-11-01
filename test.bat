@echo off
rem Builds and runs the unit tests. Usage: test.bat [Debug^|Release^|All] [fast^|slow^|all]   (default: All fast)
rem   fast  tests without real volumes: run on every change (a few seconds)
rem   slow  tests that read real volumes and disks: on demand (administrator rights, mounted ICD-TEST volumes)
rem   all   both
rem Uses the same toolchain as build.bat (clang-cl, or MSVC with ICD_TOOLCHAIN=msvc).
rem Note: no "exit /b" inside ( ) blocks; cmd loses the exit code there.
setlocal
set "ROOT=%~dp0"
set "CONFIG=%~1"
set "KIND=%~2"
if "%CONFIG%"=="" set "CONFIG=All"
if "%KIND%"=="" set "KIND=fast"
set "LABEL="
if /i "%KIND%"=="fast" set "LABEL=-L fast"
if /i "%KIND%"=="slow" set "LABEL=-L slow"
if /i "%KIND%"=="all" goto config
if defined LABEL goto config
echo Unknown test kind "%KIND%". Use fast, slow or all.
exit /b 2

:config
if /i not "%CONFIG%"=="All" goto run
call "%~f0" Debug %KIND%
if errorlevel 1 exit /b 1
call "%~f0" Release %KIND%
if errorlevel 1 exit /b 1
exit /b 0

:run
call "%ROOT%build.bat" %CONFIG%
if errorlevel 1 exit /b 1
call "%ROOT%tools\dev-env.cmd"
if errorlevel 1 exit /b 1
ctest --test-dir "%ICD_BUILD_DIR%" -C %CONFIG% %LABEL% --output-on-failure --no-tests=error --parallel 8
if errorlevel 1 exit /b 1
echo Tests passed: %CONFIG% (%KIND%)
exit /b 0
