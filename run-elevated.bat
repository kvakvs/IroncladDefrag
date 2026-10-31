@echo off
rem Starts the built GUI. Usage: run-elevated.bat [Debug^|Release]   (default: Debug)
rem The exe's manifest requires administrator rights, so Windows shows the UAC prompt.
setlocal
set "CONFIG=%~1"
if "%CONFIG%"=="" set "CONFIG=Debug"
call "%~dp0tools\dev-env.cmd"
if errorlevel 1 exit /b 1
set "EXE=%ICD_BUILD_DIR%\bin\%CONFIG%\IroncladDefrag.exe"
if exist "%EXE%" goto start
echo %EXE% not found. Run build.bat %CONFIG% first.
exit /b 1
:start
start "" "%EXE%"
