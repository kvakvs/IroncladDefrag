@echo off
rem Builds the Release programs and the NSIS installer (T140): dist\IroncladDefrag-<version>-<commit>-setup.exe.
rem Usage: make-installer.bat   Needs NSIS 3 (makensis) in "%ProgramFiles(x86)%\NSIS" or on PATH.
setlocal
call "%~dp0tools\dev-env.cmd"
if errorlevel 1 exit /b 1
call "%~dp0build.bat" Release
if errorlevel 1 exit /b 1

set "BIN=%ICD_BUILD_DIR%\bin\Release"
set "GEN=%ICD_BUILD_DIR%\generated\core"
set "VERSION="
set "COMMIT="
for /f "tokens=3" %%v in ('findstr /c:"#define ICD_VERSION_STRING" "%GEN%\Version.h"') do set "VERSION=%%~v"
for /f "tokens=3" %%v in ('findstr /c:"#define ICD_BUILD_COMMIT" "%GEN%\BuildCommit.h"') do set "COMMIT=%%~v"
if defined VERSION goto version_ok
echo The version was not found in "%GEN%\Version.h".
exit /b 1
:version_ok

rem The Visual C++ runtime DLLs to install next to the programs (vcvars64 sets VCToolsRedistDir).
set "CRT="
for /d %%d in ("%VCToolsRedistDir%x64\Microsoft.VC*.CRT") do set "CRT=%%~fd"
if defined CRT goto crt_ok
echo The Visual C++ runtime folder was not found under "%VCToolsRedistDir%x64".
exit /b 1
:crt_ok

set "MAKENSIS=%ProgramFiles(x86)%\NSIS\makensis.exe"
if exist "%MAKENSIS%" goto nsis_ok
set "MAKENSIS=makensis.exe"
where makensis.exe >nul 2>nul
if not errorlevel 1 goto nsis_ok
echo NSIS (makensis.exe) was not found. Install NSIS 3.
exit /b 1
:nsis_ok

set "NAME=IroncladDefrag-%VERSION%"
if defined COMMIT set "NAME=%NAME%-%COMMIT%"
if not exist "%~dp0dist" mkdir "%~dp0dist"
set "OUT=%~dp0dist\%NAME%-setup.exe"
echo Building %OUT%
"%MAKENSIS%" /V2 /DVERSION=%VERSION% "/DCOMMIT=%COMMIT%" "/DBIN=%BIN%" "/DCRT=%CRT%" "/DSRC=%~dp0." "/DOUTFILE=%OUT%" "%~dp0installer\IroncladDefrag.nsi"
if errorlevel 1 exit /b 1
echo Installer: %OUT%
exit /b 0
