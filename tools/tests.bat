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
cl %FLAGS% tools\loadertest.cpp bin\dbgcore.lib /Fe:%OUT%\enshrouded.exe >nul || exit /b 1
cl %FLAGS% /DLATE tools\loadertest.cpp /Fe:%OUT%\enshrouded_server.exe >nul || exit /b 1

for %%s in (client server) do (
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
copy /y %OUT%\enshrouded_server.exe %OUT%\server\ >nul
copy /y bin\dbgcore.dll %OUT%\server\cfgmgr32.dll >nul

%OUT%\client\enshrouded.exe || exit /b 1
%OUT%\server\enshrouded_server.exe || exit /b 1
echo ALL TESTS PASSED
