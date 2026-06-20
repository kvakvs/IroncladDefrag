@echo off
setlocal

set "ROOT=%~dp0"
set "ICD_EXE=%ROOT%build\Debug\IroncladDefrag.exe"
set "ICD_WORKDIR=%ROOT%build\Debug"

if not exist "%ICD_EXE%" (
    echo Debug build not found: "%ICD_EXE%"
    echo Run cmake-build.bat first.
    exit /b 1
)

powershell.exe -NoProfile -ExecutionPolicy Bypass -Command "Start-Process -FilePath $env:ICD_EXE -WorkingDirectory $env:ICD_WORKDIR -Verb RunAs"
exit /b %ERRORLEVEL%
