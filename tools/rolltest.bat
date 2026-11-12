@echo off
rem Offline test of loader.log rollover (10 MB -> loader.old.log). Needs build.bat first.
setlocal
cd /d "%~dp0.."
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
set OUT=build\rolltest
if exist %OUT% rmdir /s /q %OUT%
mkdir %OUT%\obj
set FLAGS=/nologo /std:c++20 /O2 /MT /EHsc /D_CRT_SECURE_NO_WARNINGS /Fo%OUT%\obj\
cl %FLAGS% /LD tools\floodmod.cpp /Fe:%OUT%\floodmod.dll >nul || exit /b 1
cl %FLAGS% tools\loadertest.cpp bin\dbgcore.lib /Fe:%OUT%\enshrouded.exe >nul || exit /b 1
copy /y bin\dbgcore.dll %OUT%\ >nul
mkdir %OUT%\greeneye\flood
copy /y %OUT%\floodmod.dll %OUT%\greeneye\flood\flood.dll >nul
>%OUT%\greeneye\flood\mod.json echo {"id": "flood", "name": "Flood", "version": "1.0", "side": "both"}

%OUT%\enshrouded.exe >nul 2>&1

rem the writer thread may still be flushing; give it a moment
ping -n 3 127.0.0.1 >nul
for %%f in (%OUT%\greeneye\loader.log %OUT%\greeneye\loader.old.log) do echo %%~nxf %%~zf
