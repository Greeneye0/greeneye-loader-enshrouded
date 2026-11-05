// A test mod for hottest.cpp, built in several variants (tools\hottests.bat):
//   ADD=100  generation 1, ADD=200 a reload: Target(x) returns x + 7 + ADD
//   /DFAIL_START   GreeneyeModStart returns false (the old generation must stay)
//   /DNEW_SITE     also hooks sites generation 1 never hooked (staged, written at the swap):
//                  Target2 twice (+50, +500), HotCaller's call twice (+70, +700), HotSpin (+1)
// Every variant hooks Target twice (as Auto Loot hooks ChatAppend from two places): the second
// hook adds 1000, so Target(x) = x + 7 + ADD + 1000.
// It reaches the test exe's exported variables to report what it saw.
#include <windows.h>
#include "../src/greeneye.h"

#ifndef ADD
#define ADD 100
#endif

using TargetFn = int (*)(int);
static TargetFn g_next = nullptr;
static volatile LONG* g_hold = nullptr;  // exe: Target(-1) spins while this is set
static volatile LONG* g_inside = nullptr; // exe: set while a call spins
static int g_prepares = 0;

static int Detour(int x)
{
    greeneye::Lease lease;
    if (!lease)
        return g_next(x); // an older generation: pass through
    if (x == -1) {
        InterlockedExchange(g_inside, 1);
        while (*g_hold)
            Sleep(1);
        InterlockedExchange(g_inside, 0);
    }
    return g_next(x) + ADD;
}

using SpinFn = int (*)(volatile LONG*);
static TargetFn g_next2 = nullptr, g_next2b = nullptr, g_nextCall = nullptr, g_nextCallB = nullptr;
static SpinFn g_nextSpin = nullptr;
static int Detour2(int x)
{
    greeneye::Lease lease;
    return lease ? g_next2(x) + 50 : g_next2(x);
}
static int Detour2b(int x)
{
    greeneye::Lease lease;
    return lease ? g_next2b(x) + 500 : g_next2b(x);
}
static int DetourCall(int x)
{
    greeneye::Lease lease;
    return lease ? g_nextCall(x) + 70 : g_nextCall(x);
}
static int DetourCallB(int x)
{
    greeneye::Lease lease;
    return lease ? g_nextCallB(x) + 700 : g_nextCallB(x);
}
static int DetourSpin(volatile LONG* flag)
{
    greeneye::Lease lease;
    return lease ? g_nextSpin(flag) + 1 : g_nextSpin(flag);
}

static TargetFn g_next3 = nullptr;
static int Detour3(int x)
{
    greeneye::Lease lease;
    return lease ? g_next3(x) + 1000 : g_next3(x);
}

static bool Prepare()
{
    ++g_prepares;
    return true;
}
static bool Ready()
{
    return true;
}
static void Resume()
{
    auto* resumed = reinterpret_cast<volatile LONG*>(GetProcAddress(GetModuleHandleW(nullptr), "g_resumed"));
    InterlockedIncrement(resumed);
}

static const GreeneyeHot kHot = {sizeof(GreeneyeHot), &Prepare, &Ready, &Resume};

GREENEYE_MOD_HOT
{
    return &kHot;
}

GREENEYE_MOD_START
{
    if (!greeneye::HostHasHooks(host))
        return false;
    greeneye::UseHost(host);
    HMODULE exe = GetModuleHandleW(nullptr);
    g_hold = reinterpret_cast<volatile LONG*>(GetProcAddress(exe, "g_hold"));
    g_inside = reinterpret_cast<volatile LONG*>(GetProcAddress(exe, "g_inside"));
    auto* kept = static_cast<int*>(host->keep("hotmod.value", sizeof(int)));
    auto* seenKept = reinterpret_cast<volatile LONG*>(GetProcAddress(exe, "g_seenKept"));
    InterlockedExchange(seenKept, *kept); // what the previous generation left
    *kept = ADD;

    // The target as the test exe defines it; found and read as before any hook.
    uint8_t* target = host->find("66 0F 1F 84 00 00 00 00 00 0F 1F 40 00 8D 41 07 C3 CC 47 4C 31", nullptr);
    if (!target)
        return false;
    g_next = reinterpret_cast<TargetFn>(host->hook(target, reinterpret_cast<void*>(&Detour), 13, nullptr, 0));
    if (!g_next)
        return false;
    g_next3 = reinterpret_cast<TargetFn>(host->hook(target, reinterpret_cast<void*>(&Detour3), 13, nullptr, 0));
    if (!g_next3)
        return false;
#ifdef NEW_SITE
    uint8_t* target2 = host->find("66 0F 1F 84 00 00 00 00 00 0F 1F 40 00 8D 41 07 C3 CC 47 4C 32", nullptr);
    if (!target2)
        return false;
    g_next2 = reinterpret_cast<TargetFn>(host->hook(target2, reinterpret_cast<void*>(&Detour2), 13, nullptr, 0));
    g_next2b = reinterpret_cast<TargetFn>(host->hook(target2, reinterpret_cast<void*>(&Detour2b), 13, nullptr, 0));
    uint8_t* caller = host->find("48 83 EC 28 E8 ?? ?? ?? ?? 48 83 C4 28 C3 CC 47 4C 34", nullptr);
    if (!caller)
        return false;
    g_nextCall = reinterpret_cast<TargetFn>(host->redirectCall(caller + 4, reinterpret_cast<void*>(&DetourCall)));
    g_nextCallB = reinterpret_cast<TargetFn>(host->redirectCall(caller + 4, reinterpret_cast<void*>(&DetourCallB)));
    // The spin loop is in the 13 bytes the hook replaces, so the trampoline skips it (no code).
    static const uint8_t kSkip = 0;
    uint8_t* spin = host->find("8B 01 85 C0 75 FA 0F 1F 80 00 00 00 00 B8 09 00 00 00 C3 CC 47 4C 33", nullptr);
    if (!spin)
        return false;
    g_nextSpin = reinterpret_cast<SpinFn>(host->hook(spin, reinterpret_cast<void*>(&DetourSpin), 13, &kSkip, 0));
    if (!g_next2 || !g_next2b || !g_nextCall || !g_nextCallB || !g_nextSpin)
        return false;
#endif
    (void)&Detour2;
    (void)&Detour2b;
    (void)&DetourCall;
    (void)&DetourCallB;
    (void)&DetourSpin;
#ifdef FAIL_START
    return false;
#else
    return true;
#endif
}

BOOL WINAPI DllMain(HINSTANCE, DWORD, LPVOID)
{
    return TRUE;
}
