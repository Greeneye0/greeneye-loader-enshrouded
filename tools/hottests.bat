@echo off
rem Hot reload end-to-end test (tools\hottest.cpp, tools\hotmod.cpp). Needs build.bat first.
setlocal
cd /d "%~dp0.."
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
set OUT=build\hot
if exist %OUT% rmdir /s /q %OUT%
mkdir %OUT%\obj
mkdir %OUT%\run\variants
mkdir %OUT%\run\greeneye\h
set FLAGS=/nologo /std:c++20 /O2 /MT /EHsc /D_CRT_SECURE_NO_WARNINGS /Fo%OUT%\obj\
cl %FLAGS% /LD /DADD=100 tools\hotmod.cpp /Fe:%OUT%\run\variants\hot_v1.dll >nul || exit /b 1
cl %FLAGS% /LD /DADD=200 tools\hotmod.cpp /Fe:%OUT%\run\variants\hot_v2.dll >nul || exit /b 1
cl %FLAGS% /LD /DADD=300 tools\hotmod.cpp /Fe:%OUT%\run\variants\hot_v3.dll >nul || exit /b 1
cl %FLAGS% /LD /DADD=400 /DFAIL_START /DNEW_SITE tools\hotmod.cpp /Fe:%OUT%\run\variants\hot_fail.dll >nul || exit /b 1
cl %FLAGS% /LD /DADD=500 /DNEW_SITE tools\hotmod.cpp /Fe:%OUT%\run\variants\hot_newsite.dll >nul || exit /b 1
cl %FLAGS% /LD /DADD=600 tools\hotmod.cpp /Fe:%OUT%\run\variants\hot_v6.dll >nul || exit /b 1
ml64 /nologo /c /Fo%OUT%\obj\hottarget.obj tools\hottarget.asm >nul || exit /b 1
cl %FLAGS% tools\hottest.cpp %OUT%\obj\hottarget.obj /Fe:%OUT%\run\enshrouded_server.exe >nul || exit /b 1
copy /y bin\dbgcore.dll %OUT%\run\cfgmgr32.dll >nul
copy /y %OUT%\run\variants\hot_v1.dll %OUT%\run\greeneye\h\h.dll >nul
>%OUT%\run\greeneye\h\mod.json echo {"id": "h", "name": "Hot Test", "version": "1", "dll": "h.dll", "side": "both", "api": 2}
rem A second mod (tools\hotother.cpp): a thread inside it must not hold up the first one's reload.
mkdir %OUT%\run\greeneye\o
cl %FLAGS% /LD tools\hotother.cpp /Fe:%OUT%\run\greeneye\o\o.dll >nul || exit /b 1
>%OUT%\run\greeneye\o\mod.json echo {"id": "o", "name": "Hot Test Other", "version": "1", "dll": "o.dll", "side": "both", "api": 2}
>%OUT%\run\greeneye\hotreload.enable echo on
%OUT%\run\enshrouded_server.exe || (type %OUT%\run\greeneye\loader.log & exit /b 1)
echo ALL TESTS PASSED
