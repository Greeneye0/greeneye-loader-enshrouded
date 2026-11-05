@echo off
rem Smoke test of the loader with the four real mods (tools\smoke.cpp). Needs every build.bat first.
setlocal
cd /d "%~dp0.."
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
set OUT=build\smoke
if exist %OUT% rmdir /s /q %OUT%
mkdir %OUT%\obj
cl /nologo /O2 /MT /EHsc /D_CRT_SECURE_NO_WARNINGS /Fo%OUT%\obj\ tools\smoke.cpp bin\dbgcore.lib /Fe:%OUT%\smoke.exe >nul || exit /b 1

rem Game folder: all four mods, plus a find.ini in the game folder to be migrated.
set G=%OUT%\game
for %%m in (find continue quick-deposit auto-loot) do mkdir %G%\greeneye\%%m
copy /y %OUT%\smoke.exe %G%\enshrouded.exe >nul
copy /y bin\dbgcore.dll %G%\ >nul
copy /y ..\FindMod\bin\loader\* %G%\greeneye\find\ >nul
copy /y ..\ContinueMod\bin\loader\* %G%\greeneye\continue\ >nul
copy /y ..\QuickDeposit\bin\loader\* %G%\greeneye\quick-deposit\ >nul
copy /y ..\AutoLoot\bin\loader\* %G%\greeneye\auto-loot\ >nul
>%G%\find.ini echo hotkey = Ctrl+F

rem Server folder: Auto Loot only.
set S=%OUT%\server
mkdir %S%\greeneye\auto-loot
copy /y %OUT%\smoke.exe %S%\enshrouded_server.exe >nul
copy /y bin\dbgcore.dll %S%\ >nul
copy /y ..\AutoLoot\bin\loader\* %S%\greeneye\auto-loot\ >nul

echo ===== game
%G%\enshrouded.exe find\find.log find\find.ini continue\continue.log quick-deposit\quick-deposit.log auto-loot\autoloot.log
if exist %G%\find.ini (echo find.ini still in the game folder) else (echo find.ini moved out of the game folder)
echo ===== server
%S%\enshrouded_server.exe auto-loot\autoloot.log
