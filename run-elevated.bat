@echo off
rem Starts the built GUI. Usage: run-elevated.bat [Debug^|Release]   (default: Debug)
rem The exe's manifest requires administrator rights, so Windows shows the UAC prompt.
setlocal
set "CONFIG=%~1"
if "%CONFIG%"=="" set "CONFIG=Debug"
set "EXE=%~dp0build\bin\%CONFIG%\IroncladDefrag.exe"
if not exist "%EXE%" (
    echo %EXE% not found. Run build.bat %CONFIG% first.
    exit /b 1
)
start "" "%EXE%"
