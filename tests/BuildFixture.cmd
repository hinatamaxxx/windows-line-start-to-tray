@echo off
setlocal
cd /d "%~dp0.."
if not defined VSROOT set "VSROOT=C:\Program Files\Microsoft Visual Studio\2022\Community"
if not exist build\fixture\LINE\bin\current mkdir build\fixture\LINE\bin\current
if not exist build\fixture\LineTrayStartup mkdir build\fixture\LineTrayStartup
call "%VSROOT%\VC\Auxiliary\Build\vcvarsall.bat" x86 >nul
cl /nologo /std:c++17 /W4 /O2 /MT /DTEST_LAUNCHER tests\Fixture.cpp /Fobuild\Fixture32.obj /Febuild\fixture\LINE\bin\LineLauncher.exe /link /MANIFEST:EMBED /MANIFESTUAC:"level='asInvoker' uiAccess='false'" /SUBSYSTEM:WINDOWS user32.lib shell32.lib
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /W4 /O2 /MT /DTEST_UPDATER tests\Fixture.cpp /Fobuild\FixtureUpdater32.obj /Febuild\fixture\LINE\bin\current\LineUpdater.exe /link /MANIFEST:EMBED /MANIFESTUAC:"level='asInvoker' uiAccess='false'" /SUBSYSTEM:WINDOWS user32.lib shell32.lib
if errorlevel 1 exit /b 1
call "%VSROOT%\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul
cl /nologo /std:c++17 /W4 /O2 /MT /DTEST_APP_MANAGER tests\Fixture.cpp /Fobuild\FixtureManager64.obj /Febuild\fixture\LINE\bin\current\LineAppMgr.exe /link /MANIFEST:EMBED /MANIFESTUAC:"level='asInvoker' uiAccess='false'" /SUBSYSTEM:WINDOWS user32.lib shell32.lib
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /W4 /O2 /MT tests\Fixture.cpp /Fobuild\Fixture64.obj /Febuild\fixture\LINE\bin\current\LINE.exe /link /MANIFEST:EMBED /MANIFESTUAC:"level='asInvoker' uiAccess='false'" /SUBSYSTEM:WINDOWS user32.lib shell32.lib
exit /b %errorlevel%
