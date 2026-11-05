// The second test mod for hottest.cpp (greeneye\o): hooks HotTarget4 only. HotTarget4(-1) stays
// inside its lease while the exe's g_holdOther is set, so the test can show that a thread inside
// one mod does not hold up another mod's reload.
#include <windows.h>
#include "../src/greeneye.h"

using TargetFn = int (*)(int);
static TargetFn g_next = nullptr;
static volatile LONG* g_hold = nullptr;
static volatile LONG* g_inside = nullptr;

static int Detour(int x)
{
    greeneye::Lease lease;
    if (!lease)
        return g_next(x);
    if (x == -1) {
        InterlockedExchange(g_inside, 1);
        while (*g_hold)
            Sleep(1);
        InterlockedExchange(g_inside, 0);
    }
    return g_next(x) + 2000;
}

GREENEYE_MOD_START
{
    if (!greeneye::HostHasHooks(host))
        return false;
    greeneye::UseHost(host);
    HMODULE exe = GetModuleHandleW(nullptr);
    g_hold = reinterpret_cast<volatile LONG*>(GetProcAddress(exe, "g_holdOther"));
    g_inside = reinterpret_cast<volatile LONG*>(GetProcAddress(exe, "g_insideOther"));
    uint8_t* target = host->find("66 0F 1F 84 00 00 00 00 00 0F 1F 40 00 8D 41 07 C3 CC 47 4C 35", nullptr);
    if (!target || !g_hold || !g_inside)
        return false;
    g_next = reinterpret_cast<TargetFn>(host->hook(target, reinterpret_cast<void*>(&Detour), 13, nullptr, 0));
    return g_next != nullptr;
}

BOOL WINAPI DllMain(HINSTANCE, DWORD, LPVOID)
{
    return TRUE;
}
