@echo off
rem Quality gate: Lizard (cyclomatic complexity) and clang-tidy over src/.
rem Usage: quality.bat [changed^|all]   (default: changed = files changed since HEAD plus includers of changed headers)
rem Creates .venv-quality from tools\requirements-quality.txt and a Ninja + clang-cl build in build\quality on first use.
setlocal
set "ROOT=%~dp0"
set "SCOPE=%~1"
if "%SCOPE%"=="" set "SCOPE=changed"
set "TARGET="
if /i "%SCOPE%"=="changed" set "TARGET=check-quality"
if /i "%SCOPE%"=="all" set "TARGET=check-quality-all"
if defined TARGET goto venv
echo Unknown scope "%SCOPE%". Use changed or all.
exit /b 2

:venv
rem Pinned analyzers; reinstall whenever the requirements file changes.
set "VENV=%ROOT%.venv-quality"
set "REQS=%ROOT%tools\requirements-quality.txt"
if exist "%VENV%\Scripts\python.exe" goto requirements
python -m venv "%VENV%"
if errorlevel 1 exit /b 1
:requirements
fc /b "%REQS%" "%VENV%\icd-requirements.txt" >nul 2>nul
if not errorlevel 1 goto environment
"%VENV%\Scripts\python.exe" -m pip install --disable-pip-version-check -q -r "%REQS%"
if errorlevel 1 exit /b 1
copy /y "%REQS%" "%VENV%\icd-requirements.txt" >nul

:environment
rem The gate always analyzes clang-cl compile commands, whatever toolchain build.bat uses.
set "ICD_TOOLCHAIN="
call "%ROOT%tools\dev-env.cmd"
if errorlevel 1 exit /b 1
powershell -NoProfile -ExecutionPolicy Bypass -File "%ROOT%tools\fetch-wx.ps1" >nul
if errorlevel 1 exit /b 1

rem No precompiled headers here: clang-tidy cannot read .pch files made by another compiler version.
if exist "%ROOT%build\quality\CMakeCache.txt" goto check
cmake -S "%ROOT%." -B "%ROOT%build\quality" -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_C_COMPILER=clang-cl ^
    -DCMAKE_CXX_COMPILER=clang-cl -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DCMAKE_DISABLE_PRECOMPILE_HEADERS=ON
if errorlevel 1 exit /b 1

:check
rem -k 0: run every check even when one fails, so all findings show at once.
cmake --build "%ROOT%build\quality" --target %TARGET% -- -k 0
if errorlevel 1 exit /b 1
exit /b 0
