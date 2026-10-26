@echo off
rem tools.bat VCVARS PYTHON_FOLDER: the versions of the Windows tools a build uses, one "name=value" a line
rem (port/build/manifest.py reads it). The same environment as build-module.bat: vcvars, then Python and its Scripts
rem in front of PATH.
call "%~1" >nul
set PATH=%~2\Scripts;%~2;%PATH%
echo msvc=%VCToolsVersion%
echo windows_sdk=%WindowsSDKVersion%
for /f "tokens=3" %%v in ('cmake --version') do if not defined CMAKEV set CMAKEV=%%v
echo cmake=%CMAKEV%
for /f %%v in ('ninja --version') do echo ninja=%%v
for /f "tokens=*" %%v in ('python --version') do echo python_windows=%%v
