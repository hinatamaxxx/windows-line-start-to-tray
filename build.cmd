@echo off
setlocal
cd /d "%~dp0"
if not defined VSROOT set "VSROOT=C:\Program Files\Microsoft Visual Studio\2022\Community"
if not exist "%VSROOT%\VC\Auxiliary\Build\vcvarsall.bat" exit /b 1
call "%VSROOT%\VC\Auxiliary\Build\vcvarsall.bat" x86 >nul
if errorlevel 1 exit /b 1
pushd third_party\Detours\src
nmake /nologo
if errorlevel 1 exit /b 1
popd
if not exist dist mkdir dist
if not exist build mkdir build
cl /nologo /std:c++17 /utf-8 /W4 /O2 /MT /EHsc /LD /I third_party\Detours\include native\Hook.cpp /Fobuild\Hook32.obj /Fedist\LineTrayHook32.dll /link /DEF:native\Hook.def third_party\Detours\lib.X86\detours.lib user32.lib shell32.lib comctl32.lib gdi32.lib
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /utf-8 /W4 /O2 /MT /EHsc /I third_party\Detours\include native\Launcher.cpp /Fobuild\Launcher.obj /Fedist\LineTrayStart.exe /link /SUBSYSTEM:WINDOWS third_party\Detours\lib.X86\detours.lib user32.lib
if errorlevel 1 exit /b 1
call "%VSROOT%\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul
if errorlevel 1 exit /b 1
pushd third_party\Detours\src
nmake /nologo
if errorlevel 1 exit /b 1
popd
cl /nologo /std:c++17 /utf-8 /W4 /O2 /MT /EHsc /LD /I third_party\Detours\include native\Hook.cpp /Fobuild\Hook64.obj /Fedist\LineTrayHook64.dll /link /DEF:native\Hook.def third_party\Detours\lib.X64\detours.lib user32.lib shell32.lib comctl32.lib gdi32.lib
exit /b %errorlevel%
