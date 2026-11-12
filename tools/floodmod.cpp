// A fake Greeneye mod for the log rollover test (tools\rolltest.bat): writes 25 MB into
// loader.log through host->log, as a mod that logs every tick would over a long session.
#include <windows.h>
#include <cstdio>
#include "../src/greeneye.h"

GREENEYE_MOD_START
{
    if (!greeneye::HostOk(host))
        return false;
    char line[128];
    for (int i = 0; i < 300000; ++i) {
        std::snprintf(line, sizeof(line), "flood line %06d: the area 3072, 1792 stays loaded for the portal end at 3224, 2054", i);
        host->log(line);
    }
    host->log("flood done");
    return true;
}

BOOL WINAPI DllMain(HINSTANCE, DWORD, LPVOID)
{
    return TRUE;
}
