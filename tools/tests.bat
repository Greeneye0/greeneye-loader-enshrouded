@echo off
rem Offline tests of the Greeneye Loader (see tools\loadertest.cpp). Needs build.bat first.
setlocal
cd /d "%~dp0.."
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
set OUT=build\tests
if exist %OUT% rmdir /s /q %OUT%
mkdir %OUT%\obj
set FLAGS=/nologo /std:c++20 /O2 /MT /EHsc /D_CRT_SECURE_NO_WARNINGS /Fo%OUT%\obj\
cl %FLAGS% /LD tools\testmod.cpp /Fe:%OUT%\testmod.dll >nul || exit /b 1
cl %FLAGS% /LD /DNO_ENTRY tools\testmod.cpp /Fe:%OUT%\testmod_noentry.dll >nul || exit /b 1
cl %FLAGS% /LD tools\chaintest.cpp user32.lib /Fe:%OUT%\chained.dll >nul || exit /b 1
cl %FLAGS% /LD tools\kilnstub.cpp /Fe:%OUT%\kilnstub.dll >nul || exit /b 1
cl %FLAGS% tools\loadertest.cpp bin\dbgcore.lib %OUT%\kilnstub.lib /Fe:%OUT%\enshrouded.exe >nul || exit /b 1
cl %FLAGS% /DLATE tools\loadertest.cpp /Fe:%OUT%\enshrouded_server.exe >nul || exit /b 1
mkdir %OUT%\linuxexe
cl %FLAGS:obj\=linuxexe\% /DDINPUT8 tools\loadertest.cpp dinput8.lib dxguid.lib /Fe:%OUT%\linuxexe\enshrouded_server.exe >nul || exit /b 1

mkdir %OUT%\bothexe
cl %FLAGS:obj\=bothexe\% /DDINPUT8 /DBOTH tools\loadertest.cpp dinput8.lib dxguid.lib bin\dbgcore.lib /Fe:%OUT%\bothexe\enshrouded_server.exe >nul || exit /b 1
rem The same with dbgcore.dll first in the import table, so it runs the mods and the dinput8 copy only loads the chain.
mkdir %OUT%\both2exe
rem (dbgcore-only.lib: just MiniDumpWriteDump. bin\dbgcore.lib also has the loader's DirectInput8Create, which
rem would bind the exe's DirectInput8Create to dbgcore.dll; the game takes it from DINPUT8.dll.)
lib /nologo /machine:x64 /def:tools\dbgcore-only.def /out:%OUT%\both2exe\dbgcore-only.lib >nul || exit /b 1
cl %FLAGS:obj\=both2exe\% /DDINPUT8 /DBOTH tools\loadertest.cpp %OUT%\both2exe\dbgcore-only.lib dinput8.lib dxguid.lib /Fe:%OUT%\both2exe\enshrouded_server.exe >nul || exit /b 1
rem Newer: the same order, but dinput8.dll is a "9.9.9" build of the loader. It must run the mods.
mkdir %OUT%\both3exe
cl %FLAGS:obj\=both3exe\% /DDINPUT8 /DBOTH /DNEWER tools\loadertest.cpp %OUT%\both2exe\dbgcore-only.lib dinput8.lib dxguid.lib /Fe:%OUT%\both3exe\enshrouded_server.exe >nul || exit /b 1
cl %FLAGS:obj\=both3exe\% /LD /DUNICODE /DGL_VERSION=\"9.9.9\" src\main.cpp src\hot.cpp src\dinput8_proxy.cpp src\crashguard.cpp kernel32.lib /Fe:%OUT%\both3exe\dinput8.dll >nul || exit /b 1

for %%s in (client server linux both both2 both3) do (
  mkdir %OUT%\%%s\greeneye
  for %%m in (a-both b-server c-client d-disabled e-future f-noentry) do mkdir %OUT%\%%s\greeneye\%%m
  copy /y %OUT%\testmod.dll %OUT%\%%s\greeneye\a-both\a-both.dll >nul
  copy /y %OUT%\testmod.dll %OUT%\%%s\greeneye\b-server\b-server.dll >nul
  copy /y %OUT%\testmod.dll %OUT%\%%s\greeneye\c-client\c-client.dll >nul
  copy /y %OUT%\testmod.dll %OUT%\%%s\greeneye\d-disabled\d-disabled.dll >nul
  copy /y %OUT%\testmod.dll %OUT%\%%s\greeneye\e-future\e-future.dll >nul
  copy /y %OUT%\testmod_noentry.dll %OUT%\%%s\greeneye\f-noentry\f-noentry.dll >nul
  >%OUT%\%%s\greeneye\a-both\mod.json echo {"id": "a-both", "name": "A Both", "version": "1.0", "side": "both"}
  >%OUT%\%%s\greeneye\b-server\mod.json echo {"id": "b-server", "name": "B Server", "version": "1.0", "side": "server"}
  >%OUT%\%%s\greeneye\c-client\mod.json echo {"id": "c-client", "name": "C Client", "version": "1.0", "side": "client"}
  >%OUT%\%%s\greeneye\d-disabled\mod.json echo {"id": "d-disabled", "name": "D Disabled", "version": "1.0", "side": "both", "enabled": false}
  >%OUT%\%%s\greeneye\e-future\mod.json echo {"id": "e-future", "name": "E Future", "version": "1.0", "side": "both", "api": 99}
  >%OUT%\%%s\greeneye\f-noentry\mod.json echo {"id": "f-noentry", "name": "F No Entry", "version": "1.0", "side": "both"}
)
copy /y %OUT%\enshrouded.exe %OUT%\client\ >nul
copy /y bin\dbgcore.dll %OUT%\client\dbgcore.dll >nul
copy /y bin\dbgcore.dll %OUT%\client\umpdc.dll >nul
copy /y %OUT%\kilnstub.dll %OUT%\client\ >nul
copy /y %OUT%\enshrouded_server.exe %OUT%\server\ >nul
copy /y bin\dbgcore.dll %OUT%\server\cfgmgr32.dll >nul

copy /y %OUT%\linuxexe\enshrouded_server.exe %OUT%\linux\ >nul
copy /y bin\dinput8.dll %OUT%\linux\dinput8.dll >nul
rem Chain loading by EMF's shared spec (src\dinput8_proxy.cpp): chained.dll stands in for another
rem dinput8 mod (it exports DirectInput8Create, which must NOT get the game's calls). The lines
rem cover: before any section, quotes + '/' + a comment after the name, a duplicate, itself,
rem missing, a Greeneye copy, switched off, not a .dll, outside the folder, another tool's setting,
rem another tool's section (ignored) and [EMF] (read, as Kiln does).
copy /y %OUT%\chained.dll %OUT%\linux\chained.dll >nul
mkdir %OUT%\linux\subdir
copy /y %OUT%\testmod_noentry.dll %OUT%\linux\subdir\plain.dll >nul
>%OUT%\linux\dinput8-chain.ini echo ; test chain
>>%OUT%\linux\dinput8-chain.ini echo chained.dll
>>%OUT%\linux\dinput8-chain.ini echo [Chain]
>>%OUT%\linux\dinput8-chain.ini echo   "subdir/plain.dll"   ; quoted, a slash, a comment after it
>>%OUT%\linux\dinput8-chain.ini echo CHAINED.DLL
>>%OUT%\linux\dinput8-chain.ini echo dinput8.dll
>>%OUT%\linux\dinput8-chain.ini echo missing.dll
>>%OUT%\linux\dinput8-chain.ini echo greeneye-copy.dll
>>%OUT%\linux\dinput8-chain.ini echo ; switched.dll
>>%OUT%\linux\dinput8-chain.ini echo readme.txt
>>%OUT%\linux\dinput8-chain.ini echo ..\outside.dll
>>%OUT%\linux\dinput8-chain.ini echo C:\abs.dll
>>%OUT%\linux\dinput8-chain.ini echo Forward=auto
>>%OUT%\linux\dinput8-chain.ini echo [Kiln]
>>%OUT%\linux\dinput8-chain.ini echo kilnonly.dll
>>%OUT%\linux\dinput8-chain.ini echo [EMF]
>>%OUT%\linux\dinput8-chain.ini echo emfmoved.dll
copy /y bin\dbgcore.dll %OUT%\linux\greeneye-copy.dll >nul
rem A copy named dinput8.dll that another chain loader loads from its own folder (late, by main).
mkdir %OUT%\linux\otherloader
copy /y bin\dinput8.dll %OUT%\linux\otherloader\dinput8.dll >nul

rem Both: the loader as dbgcore.dll and as dinput8.dll in one folder, with the same chain.
copy /y %OUT%\bothexe\enshrouded_server.exe %OUT%\both\ >nul
copy /y bin\dbgcore.dll %OUT%\both\dbgcore.dll >nul
copy /y bin\dinput8.dll %OUT%\both\dinput8.dll >nul
copy /y %OUT%\chained.dll %OUT%\both\chained.dll >nul
mkdir %OUT%\both\subdir
copy /y %OUT%\testmod_noentry.dll %OUT%\both\subdir\plain.dll >nul
copy /y %OUT%\linux\dinput8-chain.ini %OUT%\both\ >nul
copy /y bin\dbgcore.dll %OUT%\both\greeneye-copy.dll >nul
xcopy /e /i /q /y %OUT%\both\*.dll %OUT%\both2\ >nul
copy /y %OUT%\both\dinput8-chain.ini %OUT%\both2\ >nul
copy /y %OUT%\both2exe\enshrouded_server.exe %OUT%\both2\ >nul
xcopy /e /i /q /y %OUT%\both\*.dll %OUT%\both3\ >nul
copy /y %OUT%\both\dinput8-chain.ini %OUT%\both3\ >nul
copy /y %OUT%\both3exe\enshrouded_server.exe %OUT%\both3\ >nul
copy /y %OUT%\both3exe\dinput8.dll %OUT%\both3\dinput8.dll >nul

%OUT%\client\enshrouded.exe || exit /b 1
%OUT%\server\enshrouded_server.exe || exit /b 1
%OUT%\linux\enshrouded_server.exe || exit /b 1
%OUT%\both\enshrouded_server.exe || exit /b 1
%OUT%\both2\enshrouded_server.exe || exit /b 1
%OUT%\both3\enshrouded_server.exe || exit /b 1

rem Crash guard: a chained DLL (exit 98) and a mod (exit 99) each kill a start; the third start
rem skips both. After the mod's DLL changes (an update), it is tried again.
mkdir %OUT%\guardexe
cl %FLAGS:obj\=guardexe\% /DDINPUT8 /DGUARD tools\loadertest.cpp dinput8.lib dxguid.lib /Fe:%OUT%\guardexe\enshrouded_server.exe >nul || exit /b 1
cl %FLAGS:obj\=guardexe\% /LD /DCRASH_START tools\testmod.cpp /Fe:%OUT%\guardexe\g-crash.dll >nul || exit /b 1
cl %FLAGS:obj\=guardexe\% /LD /DNO_ENTRY /DCRASH_LOAD tools\testmod.cpp /Fe:%OUT%\guardexe\crash.dll >nul || exit /b 1
set G=%OUT%\guard
mkdir %G%\greeneye\a-both %G%\greeneye\g-crash
copy /y %OUT%\guardexe\enshrouded_server.exe %G%\ >nul
copy /y bin\dinput8.dll %G%\ >nul
copy /y %OUT%\guardexe\crash.dll %G%\ >nul
copy /y %OUT%\testmod.dll %G%\greeneye\a-both\a-both.dll >nul
copy /y %OUT%\guardexe\g-crash.dll %G%\greeneye\g-crash\g-crash.dll >nul
>%G%\greeneye\a-both\mod.json echo {"id": "a-both", "name": "A Both", "version": "1.0", "side": "both"}
>%G%\greeneye\g-crash\mod.json echo {"id": "g-crash", "name": "G Crash", "version": "1.0", "side": "both"}
rem UTF-16 LE with a BOM (the spec's other encoding): the guard test only passes if it is read.
powershell -NoProfile -Command "Set-Content -Encoding Unicode -Path '%G%\dinput8-chain.ini' -Value '[Chain]','crash.dll'" || exit /b 1
%G%\enshrouded_server.exe
if not errorlevel 98 (echo guard: first start should have died in crash.dll & exit /b 1)
%G%\enshrouded_server.exe
if not errorlevel 99 (echo guard: second start should have died in g-crash & exit /b 1)
%G%\enshrouded_server.exe || exit /b 1
rem An update of the mod (its DLL changes): the next start tries it again, and it dies again.
copy /b %G%\greeneye\g-crash\g-crash.dll+,, %G%\greeneye\g-crash\g-crash.dll >nul
powershell -NoProfile -Command "(Get-Item '%G%\greeneye\g-crash\g-crash.dll').LastWriteTime = (Get-Date).AddMinutes(1)"
%G%\enshrouded_server.exe
if not errorlevel 99 (echo guard: the updated mod should have been tried again & exit /b 1)
findstr /c:"changed since it crashed the game, so it starts again" %G%\greeneye\loader.log >nul || (echo guard: no "changed" line & exit /b 1)
echo guard: an updated mod is tried again                         OK
echo ALL TESTS PASSED
