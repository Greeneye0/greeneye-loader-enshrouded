// Offline test of the Greeneye Loader, run by tools\tests.bat in two fake game folders:
//   out\client\enshrouded.exe         imports dbgcore.dll at start-up (like the real game), so
//                                     the loader starts the mods at the entry point, before main
//   out\server\enshrouded_server.exe  (built with /DLATE) loads a renamed copy, cfgmgr32.dll,
//                                     later, so the loader starts the mods right away
//   out\linux\enshrouded_server.exe   (built with /DDINPUT8) imports DINPUT8.dll at start-up,
//                                     like both real exes, with the loader named dinput8.dll: the
//                                     Linux install, tried here on Windows
// Each folder has greeneye\ with fake mods (testmod.dll) for every case in mod.json.
#include <windows.h>
#include <cstdio>
#include <algorithm>
#include <string>
#include "../src/loader.h" // GL_VERSION
#define GL_TEST_VERSION GL_VERSION

#if defined(DINPUT8)
// Link-time import of DINPUT8.dll (the SDK's dinput8.lib); the folder's copy wins over System32.
#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>
static void* volatile g_keepImport = reinterpret_cast<void*>(&DirectInput8Create);
#if defined(BOTH)
// Also the loader as dbgcore.dll: two copies of the loader in one process.
extern "C" __declspec(dllimport) BOOL WINAPI MiniDumpWriteDump(HANDLE, DWORD, HANDLE, int, void*, void*, void*);
static void* volatile g_keepImport2 = reinterpret_cast<void*>(&MiniDumpWriteDump);
#endif
#elif !defined(LATE)
// Link-time import of the loader, as dbgcore.dll (bin\dbgcore.lib), like dbghelp pulls it in.
extern "C" __declspec(dllimport) BOOL WINAPI MiniDumpWriteDump(HANDLE, DWORD, HANDLE, int, void*, void*, void*);
static void* volatile g_keepImport = reinterpret_cast<void*>(&MiniDumpWriteDump);
// After the loader: another host checking the loader's claim before the entry point (kilnstub.cpp).
extern "C" __declspec(dllimport) int KilnStub();
static void* volatile g_keepKiln = reinterpret_cast<void*>(&KilnStub);
#endif

static int g_failures = 0;

static void Check(const char* what, bool ok)
{
    printf("  %-58s %s\n", what, ok ? "OK" : "FAIL");
    if (!ok)
        ++g_failures;
}

static std::string Env(const char* name)
{
    char buf[64] = "";
    GetEnvironmentVariableA(name, buf, sizeof(buf));
    return buf;
}

static std::string Dir()
{
    char exe[MAX_PATH];
    GetModuleFileNameA(nullptr, exe, MAX_PATH);
    *strrchr(exe, '\\') = 0;
    return exe;
}

static std::string ReadAll(const std::string& path)
{
    std::string s;
    if (FILE* f = fopen(path.c_str(), "rb")) {
        char buf[4096];
        size_t n;
        while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
            s.append(buf, n);
        fclose(f);
    }
    return s;
}

// The dinput8 exports are code in the loader (dinput8_proxy.cpp), not forwarders.
static bool IsDinput8Stub(const char* sym)
{
    for (const char* s : {"DirectInput8Create", "DllCanUnloadNow", "DllGetClassObject", "DllRegisterServer",
                          "DllUnregisterServer", "GetdfDIJoystick"})
        if (strcmp(sym, s) == 0)
            return true;
    return false;
}

// Every other export of the loader module resolves to the same address as the real System32 DLL.
static void CheckForwarders(HMODULE loader)
{
    auto base = reinterpret_cast<uint8_t*>(loader);
    auto nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + reinterpret_cast<IMAGE_DOS_HEADER*>(base)->e_lfanew);
    auto exp = reinterpret_cast<IMAGE_EXPORT_DIRECTORY*>(base + nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress);
    auto names = reinterpret_cast<DWORD*>(base + exp->AddressOfNames);
    HMODULE real[3] = {LoadLibraryA("C:\\Windows\\System32\\dbgcore.dll"), LoadLibraryA("C:\\Windows\\System32\\umpdc.dll"),
                       LoadLibraryA("C:\\Windows\\System32\\cfgmgr32.dll")};
    int matched = 0, bad = 0;
    for (DWORD i = 0; i < exp->NumberOfNames; ++i) {
        const char* sym = reinterpret_cast<const char*>(base + names[i]);
        if (IsDinput8Stub(sym))
            continue;
        FARPROC mine = GetProcAddress(loader, sym), want = nullptr;
        for (HMODULE r : real)
            if (r && (want = GetProcAddress(r, sym)) != nullptr)
                break;
        if (mine && mine == want)
            ++matched;
        else if (bad++ < 3)
            printf("    %s: loader %p, System32 %p\n", sym, reinterpret_cast<void*>(mine), reinterpret_cast<void*>(want));
    }
    char what[96];
    snprintf(what, sizeof(what), "all forwarders reach the System32 DLL (%d match)", matched);
    Check(what, bad == 0 && matched > 300);
}

int main()
{
#if defined(GUARD)
    // tests.bat ran this folder twice already: the chained crash.dll took it down (98), then the
    // g-crash mod (99). Now both must be skipped and the rest start.
    std::string dir = Dir();
    printf("guard folder (third start, after a chained DLL and a mod each crashed a start)\n");
    Check("DINPUT8.dll imported (the loader as dinput8.dll)", g_keepImport != nullptr); // keeps the import
    std::string log = ReadAll(dir + "\\greeneye\\loader.log");
    std::string list = ReadAll(dir + "\\greeneye\\crashguard.txt");
    Check("crashguard.txt lists the chain entry and the mod", list.find("chain\tcrash.dll\t") != std::string::npos &&
                                                                 list.find("mod\tg-crash\t") != std::string::npos);
    Check("crashed chain entry skipped", log.find("chain: crash.dll skipped: it crashed the game while loading before") != std::string::npos);
    Check("crashed mod skipped", log.find("G Crash 1.0: skipped: it crashed the game while starting before") != std::string::npos);
    Check("the other mods still start", Env("GL_TEST_a-both") == "server" && Env("GL_TEST_g-crash").empty());
    Check("no start left half done", GetFileAttributesA((dir + "\\greeneye\\loader.starting").c_str()) == INVALID_FILE_ATTRIBUTES);
#elif defined(DINPUT8)
    std::string beforeMain = Env("GL_TEST_a-both");
    std::string chainBeforeMain = Env("GL_TEST_chain_loaded");
    std::string dir = Dir();
    Check("mods started before main (at the entry point)", beforeMain == "server");
    Check("chained dinput8 mod loaded before main", chainBeforeMain == "1");
#if defined(BOTH)
    printf("both folder (loader imported at start-up as dbgcore.dll AND dinput8.dll, with a chain)\n");
    // One copy runs the mods (loader.log); if that is dbgcore.dll, the dinput8 copy still loads
    // the chain and writes it to loader.chain.log. Either way the chain loads exactly once.
    std::string modsLog = ReadAll(dir + "\\greeneye\\loader.log");
    std::string chainLog = ReadAll(dir + "\\greeneye\\loader.chain.log");
    std::string second = ReadAll(dir + "\\greeneye\\loader.second-copy.log");
    std::string log = modsLog + chainLog;
    printf("  (mods run by: %s)\n", modsLog.find("loaded as dbgcore") != std::string::npos ? "dbgcore.dll" : "dinput8.dll");
    Check("the idle copy names the one that runs the mods", second.find("another copy (") != std::string::npos &&
                                                                second.find(") runs the mods") != std::string::npos);
#if defined(NEWER)
    // dinput8.dll here is a "9.9.9" build and loads after dbgcore.dll 1.2.4: the newer one runs
    // the mods all the same, and the older one says so.
    Check("the newer copy runs the mods (GREENEYE_LOADER)", _strnicmp(Env("GREENEYE_LOADER").c_str(), "9.9.9 as dinput8.dll", 20) == 0);
    Check("the older copy names the newer one", second.find("1.2.4 as dbgcore.dll: another copy (9.9.9 as") != std::string::npos);
#else
    Check("same version: the copy that runs the mods is named", Env("GREENEYE_LOADER").rfind("1.2.4 as ", 0) == 0);
#endif
    size_t first = log.find("dinput8-chain.ini lists 6 DLLs");
    Check("chain read exactly once", first != std::string::npos && log.find("dinput8-chain.ini lists 6 DLLs", first + 1) == std::string::npos);
    if (modsLog.find("loaded as dbgcore") != std::string::npos)
        Check("idle dinput8 copy logs its chain to loader.chain.log", chainLog.find("chain: chained.dll started") != std::string::npos &&
                                                                         second.find("still loads dinput8-chain.ini") != std::string::npos);
#else
    printf("linux folder (loader imported at start-up as dinput8.dll)\n");
    std::string log = ReadAll(dir + "\\greeneye\\loader.log");
    // Named as the exe imports it: "DINPUT8.dll".
    Check("loader.log: loaded as dinput8.dll, start-up load", _strnicmp(log.c_str() + (std::min)(log.find("loaded as "), log.size()),
                                                                        "loaded as dinput8.dll", 21) == 0 &&
                                                                 log.find("start-up load") != std::string::npos);
#endif
    Check("loader.log: mods start at the entry point", log.find("mods start at the entry point") != std::string::npos);
    Check("side server: started", Env("GL_TEST_b-server") == "server");
    HMODULE self = GetModuleHandleA("dinput8.dll");
    char path[MAX_PATH] = "";
    GetModuleFileNameA(self, path, MAX_PATH);
    Check("DINPUT8.dll import is the loader in the game folder", _stricmp(path, (dir + "\\dinput8.dll").c_str()) == 0);
    IDirectInput8W* di = nullptr;
    HRESULT hr = DirectInput8Create(GetModuleHandleW(nullptr), DIRECTINPUT_VERSION, IID_IDirectInput8W,
                                    reinterpret_cast<void**>(&di), nullptr);
    Check("DirectInput8Create reaches the real dinput8", SUCCEEDED(hr) && di != nullptr);
    if (di)
        di->Release();
    // dinput8-chain.ini (tests.bat), read by EMF's shared spec: chained DLLs get no DirectInput
    // calls, even one that exports DirectInput8Create.
    auto has = [&log](const char* s) { return log.find(s) != std::string::npos; };
    Check("chained DLL gets no DirectInput calls (spec)", Env("GL_TEST_chain_create").empty());
    Check("chain: 6 DLLs listed, 1 twice", has("dinput8-chain.ini lists 6 DLLs (1 listed twice, 0 over the limit of 256)"));
    Check("chain: line before any section counts as [Chain]", has("chain: chained.dll started"));
    Check("chain: quoted name, '/', comment after it", has("chain: subdir\\plain.dll started"));
    Check("chain: itself refused", has("chain: dinput8.dll refused: that is dinput8.dll itself"));
    Check("chain: missing file reported", has("chain: missing.dll skipped: file not found"));
    Check("chain: Greeneye Loader copy refused", has("chain: greeneye-copy.dll refused: another copy of the Greeneye Loader (1."));
    Check("chain: '; x.dll' is switched off", has("chain: switched.dll skipped: switched off"));
    Check("chain: not a .dll reported", has("chain: readme.txt skipped: does not name a .dll"));
    Check("chain: '..' and absolute paths refused", has("chain: ..\\outside.dll refused: outside the game folder") &&
                                                       has("chain: C:\\abs.dll refused: outside the game folder"));
    Check("chain: another tool's setting and section ignored", !has("Forward") && !has("kilnonly"));
    Check("chain: [EMF] read too (as Kiln does)", has("chain: emfmoved.dll skipped: file not found"));
    Check("chained DLLs loaded", GetModuleHandleA((dir + "\\chained.dll").c_str()) != nullptr &&
                                     GetModuleHandleA((dir + "\\subdir\\plain.dll").c_str()) != nullptr);
    char sys[MAX_PATH];
    GetSystemDirectoryA(sys, MAX_PATH);
    HMODULE real = GetModuleHandleA((std::string(sys) + "\\dinput8.dll").c_str());
    Check("real System32 dinput8 is loaded beside it", real != nullptr && real != self);
    using Df = const void*(WINAPI*)();
    auto mineDf = reinterpret_cast<Df>(GetProcAddress(self, "GetdfDIJoystick"));
    auto realDf = real ? reinterpret_cast<Df>(GetProcAddress(real, "GetdfDIJoystick")) : nullptr;
    Check("GetdfDIJoystick returns the real data format", mineDf && realDf && mineDf() == realDf());
    CheckForwarders(self);
#if !defined(BOTH)
    // Someone else's chain loader loads a copy named dinput8.dll from its own folder: it must
    // leave the mods to the running copy and dinput8-chain.ini to the game's dinput8.dll.
    HMODULE other = LoadLibraryA((dir + "\\otherloader\\dinput8.dll").c_str());
    Check("copy loaded by another chain loader loads", other != nullptr && other != self);
    std::string second = ReadAll(dir + "\\greeneye\\loader.second-copy.log");
    Check("  it leaves the mods to the running copy", second.find("as dinput8.dll: another copy (1.") != std::string::npos);
    Check("  it doesn't take dinput8-chain.ini", second.find("dinput8-chain.ini") == std::string::npos &&
                                                     GetFileAttributesA((dir + "\\greeneye\\loader.chain.log").c_str()) ==
                                                         INVALID_FILE_ATTRIBUTES);
    Check("  chained.dll still not called", Env("GL_TEST_chain_create").empty());
#endif
#elif !defined(LATE)
    // Read before anything else runs: the mods must have started before main.
    std::string beforeMain = Env("GL_TEST_a-both");
    std::string dir = Dir();
    printf("client folder (loader imported at start-up as dbgcore.dll)\n");
    Check("mods started before main (at the entry point)", beforeMain == "client");
    Check("claim held before the entry point (Kiln steps aside)", Env("GL_TEST_claim_before_entry") == "1" && g_keepKiln);
    std::string log = ReadAll(dir + "\\greeneye\\loader.log");
    Check("loader.log: loaded as dbgcore.dll, start-up load", log.find("loaded as dbgcore.dll") != std::string::npos &&
                                                                 log.find("start-up load") != std::string::npos);
    Check("loader.log: mods start at the entry point", log.find("mods start at the entry point") != std::string::npos);
    Check("side both: started", Env("GL_TEST_a-both") == "client" && log.find("A Both 1.0: started") != std::string::npos);
    Check("side client: started", Env("GL_TEST_c-client") == "client");
    Check("side server: skipped on the game", Env("GL_TEST_b-server").empty() && log.find("server only, skipped") != std::string::npos);
    Check("disabled: skipped", Env("GL_TEST_d-disabled").empty() && log.find("disabled in mod.json") != std::string::npos);
    Check("newer API: refused", Env("GL_TEST_e-future").empty() && log.find("needs a newer Greeneye Loader") != std::string::npos);
    Check("no GreeneyeModStart: reported", log.find("no GreeneyeModStart") != std::string::npos);
    Check("mod log line reaches loader.log", log.find("testmod says hello") != std::string::npos);
    Check("mod knows its loader (GreeneyeHost::loader)",
          log.find("testmod runs under Greeneye Loader " GL_TEST_VERSION " as dbgcore.dll") != std::string::npos);
    Check("GREENEYE_LOADER set for the process", Env("GREENEYE_LOADER") == GL_TEST_VERSION " as dbgcore.dll");
    CheckForwarders(GetModuleHandleA("dbgcore.dll"));
    // A second, renamed copy in the same process stays idle.
    HMODULE second = LoadLibraryA((dir + "\\umpdc.dll").c_str());
    Check("renamed second copy loads", second != nullptr);
    std::string idle = ReadAll(dir + "\\greeneye\\loader.second-copy.log");
    Check("renamed second copy stays idle", idle.find("as umpdc.dll") != std::string::npos);
#else
    std::string dir = Dir();
    printf("server folder (loader loaded late as cfgmgr32.dll)\n");
    Check("nothing started before the loader is loaded", Env("GL_TEST_a-both").empty());
    HMODULE loader = LoadLibraryA((dir + "\\cfgmgr32.dll").c_str());
    Check("loader loads as cfgmgr32.dll", loader != nullptr);
    std::string log = ReadAll(dir + "\\greeneye\\loader.log");
    Check("loader.log: late load in the server", log.find("loaded as cfgmgr32.dll") != std::string::npos &&
                                                    log.find("(server)") != std::string::npos &&
                                                    log.find("late load") != std::string::npos);
    Check("side both: started on the server", Env("GL_TEST_a-both") == "server");
    Check("side server: started", Env("GL_TEST_b-server") == "server");
    Check("side client: skipped on the server", Env("GL_TEST_c-client").empty());
    if (loader)
        CheckForwarders(loader);
#endif
    printf(g_failures ? "  FAILED (%d)\n" : "  ALL OK\n", g_failures);
    return g_failures ? 1 : 0;
}
