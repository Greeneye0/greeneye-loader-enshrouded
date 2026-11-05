@echo off
rem Builds bin\dbgcore.dll (Greeneye Loader) with the VS 2022 Build Tools.
rem tools\gen_exports.py regenerates src\exports.h (only needed after a Windows update changes
rem the exports of dbgcore / umpdc / cfgmgr32).
setlocal
cd /d "%~dp0"
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
if not exist build mkdir build
if not exist bin mkdir bin
rc /nologo /fo build\version.res src\version.rc || exit /b 1
cl /nologo /std:c++20 /O2 /MT /W4 /EHsc /DUNICODE /D_CRT_SECURE_NO_WARNINGS /Fobuild\ /c src\main.cpp src\hot.cpp || exit /b 1
link /nologo /DLL /OUT:bin\dbgcore.dll build\main.obj build\hot.obj build\version.res kernel32.lib || exit /b 1
echo Built bin\dbgcore.dll
