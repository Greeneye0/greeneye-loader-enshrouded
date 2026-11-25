// A fake Greeneye mod for loadertest: records in an environment variable of the process that it
// was started, with which side, so the test can see it (GL_TEST_<folder> = client|server).
// Built twice: testmod.dll, and testmod_noentry.dll (no GreeneyeModStart) with /DNO_ENTRY.
#include <windows.h>
#include <cstdio>
#include <cwchar>
#include "../src/greeneye.h"

#ifndef NO_ENTRY
GREENEYE_MOD_START
{
    if (!greeneye::HostOk(host))
        return false;
    // modDir ends in "\greeneye\<folder>\".
    wchar_t dir[MAX_PATH];
    wcsncpy_s(dir, host->modDir, _TRUNCATE);
    size_t n = wcslen(dir);
    if (n && dir[n - 1] == L'\\')
        dir[--n] = 0;
    const wchar_t* folder = wcsrchr(dir, L'\\');
    wchar_t name[MAX_PATH];
    swprintf_s(name, L"GL_TEST_%s", folder ? folder + 1 : dir);
    SetEnvironmentVariableW(name, host->side == kGreeneyeServer ? L"server" : L"client");
#ifdef CRASH_START
    TerminateProcess(GetCurrentProcess(), 99); // a crash no exception handler can catch
#endif
    host->log("testmod says hello");
    char line[256];
    snprintf(line, sizeof(line), "testmod runs under %s", greeneye::LoaderName(host));
    host->log(line);
    return true;
}
#endif

// /DCRASH_LOAD: a chained DLL that takes the process down while it loads (crash guard test).
BOOL WINAPI DllMain(HINSTANCE, DWORD reason, LPVOID)
{
#ifdef CRASH_LOAD
    if (reason == DLL_PROCESS_ATTACH)
        TerminateProcess(GetCurrentProcess(), 98);
#else
    (void)reason;
#endif
    return TRUE;
}
