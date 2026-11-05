// End-to-end hot reload test (tools\hottests.bat): this exe pretends to be the dedicated server
// (enshrouded_server.exe, so the loader's watcher is the safe point), loads the loader as
// cfgmgr32.dll with greeneye\hotreload.enable, and the test mod greeneye\h\h.dll hooks Target
// below. Reloads replace h.dll with the variants in variants\ and write greeneye\h\reload.
#include <windows.h>
#include <cstdio>
#include <string>
#include <thread>

// Target(x) = x + 7 in the exe's .text (tools\hottarget.asm), Target2 the same, HotCaller(x)
// calls x + 7 through one call site, HotSpin(flag) spins while *flag is set and returns 9.
extern "C" int HotTarget1(int);
extern "C" int HotTarget2(int);
extern "C" int HotCaller(int);
extern "C" int HotSpin(volatile LONG*);
extern "C" int HotTarget4(int);
static volatile LONG g_spin = 0, g_spinOff = 0;

extern "C" __declspec(dllexport) volatile LONG g_hold = 0;
extern "C" __declspec(dllexport) volatile LONG g_inside = 0;
extern "C" __declspec(dllexport) volatile LONG g_seenKept = 0;
extern "C" __declspec(dllexport) volatile LONG g_resumed = 0;
extern "C" __declspec(dllexport) volatile LONG g_holdOther = 0;
extern "C" __declspec(dllexport) volatile LONG g_insideOther = 0;

using TargetFn = int (*)(int);
static const TargetFn Target = &HotTarget1;
static int g_failures = 0;
static std::string g_dir;

static void Check(const char* what, bool ok)
{
    printf("  %-62s %s\n", what, ok ? "OK" : "FAIL");
    if (!ok)
        ++g_failures;
}

static std::string Status()
{
    std::string s;
    if (FILE* f = fopen((g_dir + "\\greeneye\\h\\hotload.status").c_str(), "rb")) {
        char buf[256] = {};
        fread(buf, 1, sizeof(buf) - 1, f);
        fclose(f);
        s = buf;
    }
    return s;
}

static bool WaitStatus(const char* text, DWORD ms)
{
    for (DWORD t = 0; t < ms; t += 50) {
        if (Status().find(text) != std::string::npos)
            return true;
        Sleep(50);
    }
    return false;
}

static void Reload(const char* variant)
{
    CopyFileA((g_dir + "\\variants\\" + variant).c_str(), (g_dir + "\\greeneye\\h\\h.dll").c_str(), FALSE);
    if (FILE* f = fopen((g_dir + "\\greeneye\\h\\reload").c_str(), "w"))
        fclose(f);
}

int main()
{
    char exe[MAX_PATH];
    GetModuleFileNameA(nullptr, exe, MAX_PATH);
    *strrchr(exe, '\\') = 0;
    g_dir = exe;
    printf("hot reload (dedicated server, loader as cfgmgr32.dll)\n");

    Check("Target(1) = 8 before the loader", Target(1) == 8);
    HMODULE loader = LoadLibraryA((g_dir + "\\cfgmgr32.dll").c_str());
    Check("loader loads", loader != nullptr);
    Check("generation 1 active", WaitStatus("active generation 1", 2000));
    Check("Target(1) = 1108 (hooked twice by generation 1)", Target(1) == 1108);
    for (int t = 0; t < 40 && HotTarget4(1) != 2008; ++t)
        Sleep(50); // the second mod starts right after the first
    Check("second mod: HotTarget4(1) = 2008", HotTarget4(1) == 2008);
    if (g_failures) {
        printf("  FAILED (%d): generation 1 did not start, nothing else to test\n", g_failures);
        return 1;
    }

    Reload("hot_v2.dll"); // ADD 200
    Check("reload: generation 2 active", WaitStatus("active generation 2", 5000));
    Check("Target(1) = 1208", Target(1) == 1208);
    Check("keep(): generation 2 saw generation 1's value", g_seenKept == 100);

    // A call inside the mod (a lease) holds the swap back until it returns.
    g_hold = 1;
    std::thread caller([] { Target(-1); });
    while (!g_inside)
        Sleep(1);
    Reload("hot_v3.dll"); // ADD 300
    Sleep(1500);
    Check("lease held: generation 3 still pending", Status().find("pending: generation 3") != std::string::npos);
    Check("lease held: Target still answers from generation 2", true);
    g_hold = 0;
    caller.join();
    Check("lease released: generation 3 active", WaitStatus("active generation 3", 5000));
    Check("Target(1) = 1308", Target(1) == 1308);

    uint8_t spinBytes[13], callBytes[5];
    memcpy(spinBytes, reinterpret_cast<void*>(&HotSpin), 13);
    memcpy(callBytes, reinterpret_cast<uint8_t*>(&HotCaller) + 4, 5);
    Reload("hot_fail.dll"); // ADD 400, stages new sites, then start fails
    Check("failed start reported, generation 3 stays", WaitStatus("generation 3 stays active", 5000));
    Check("Target(1) still 1308", Target(1) == 1308);
    Check("old generation resumed", g_resumed == 1);
    Check("staged sites dropped: Target2(1) = 8, HotCaller(1) = 8", HotTarget2(1) == 8 && HotCaller(1) == 8);
    Check("staged sites dropped: their bytes unchanged",
          memcmp(spinBytes, reinterpret_cast<void*>(&HotSpin), 13) == 0 &&
              memcmp(callBytes, reinterpret_cast<uint8_t*>(&HotCaller) + 4, 5) == 0);

    // New sites on a reload, while a thread spins inside the bytes one of them overwrites.
    g_spin = 1;
    std::thread spinner([] { HotSpin(&g_spin); });
    Sleep(100);
    Reload("hot_newsite.dll"); // ADD 500
    Check("thread in a new site: generation 5 waits", WaitStatus("pending: generation 5 started", 5000));
    Sleep(1000);
    Check("still waiting, Target(1) still 1308", Status().find("pending: generation 5") != std::string::npos &&
                                                     Target(1) == 1308);
    Check("HotSpin's bytes unchanged while the thread is inside",
          memcmp(spinBytes, reinterpret_cast<void*>(&HotSpin), 13) == 0);
    g_spin = 0;
    spinner.join();
    Check("thread left: generation 5 active with its new hooks", WaitStatus("active generation 5", 5000) &&
                                                                    Status().find("5 new hooks") != std::string::npos);
    Check("Target(1) = 1508", Target(1) == 1508);
    Check("Target2(1) = 558 (new site hooked twice)", HotTarget2(1) == 558);
    Check("HotCaller(1) = 778 (new call redirected twice)", HotCaller(1) == 778);
    Check("HotSpin = 10 (new hook over the spin loop)", HotSpin(&g_spinOff) == 10);

    Reload("hot_v6.dll"); // ADD 600: drops the new sites again
    Check("next build: generation 6 active", WaitStatus("active generation 6", 5000));
    Check("Target(1) = 1608", Target(1) == 1608);
    Check("dropped hooks pass through: 8, 8, 9", HotTarget2(1) == 8 && HotCaller(1) == 8 && HotSpin(&g_spinOff) == 9);

    Reload("hot_newsite.dll"); // ADD 500 again: the same sites, reused
    Check("same new sites again: generation 7 active, no new hooks",
          WaitStatus("active generation 7", 5000) && Status().find("new hooks") == std::string::npos);
    Check("Target2(1) = 558, HotCaller(1) = 778, HotSpin = 10",
          HotTarget2(1) == 558 && HotCaller(1) == 778 && HotSpin(&g_spinOff) == 10);

    // A thread inside another mod's hook does not hold this mod's reload up.
    g_holdOther = 1;
    std::thread other([] { HotTarget4(-1); });
    while (!g_insideOther)
        Sleep(1);
    Reload("hot_v6.dll"); // ADD 600
    Check("thread inside the other mod: generation 8 active anyway", WaitStatus("active generation 8", 5000));
    Check("Target(1) = 1608 while the other mod's thread still waits", Target(1) == 1608 && g_insideOther == 1);
    g_holdOther = 0;
    other.join();

    printf(g_failures ? "  FAILED (%d)\n" : "  ALL OK\n", g_failures);
    return g_failures ? 1 : 0;
}
