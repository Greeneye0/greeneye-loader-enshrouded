// Offline test of the Greeneye Loader, run by tools\tests.bat in two fake game folders:
//   out\client\enshrouded.exe         imports dbgcore.dll at start-up (like the real game), so
//                                     the loader starts the mods at the entry point, before main
//   out\server\enshrouded_server.exe  (built with /DLATE) loads a renamed copy, cfgmgr32.dll,
//                                     later, so the loader starts the mods right away
// Each folder has greeneye\ with fake mods (testmod.dll) for every case in mod.json.
#include <windows.h>
#include <cstdio>
#include <string>

#ifndef LATE
// Link-time import of the loader, as dbgcore.dll (bin\dbgcore.lib), like dbghelp pulls it in.
extern "C" __declspec(dllimport) BOOL WINAPI MiniDumpWriteDump(HANDLE, DWORD, HANDLE, int, void*, void*, void*);
static void* volatile g_keepImport = reinterpret_cast<void*>(&MiniDumpWriteDump);
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

// Every export of the loader module resolves to the same address as the real System32 DLL.
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
    snprintf(what, sizeof(what), "all %lu forwarders reach the System32 DLL (%d match)", exp->NumberOfNames, matched);
    Check(what, bad == 0 && matched > 300);
}

int main()
{
#ifndef LATE
    // Read before anything else runs: the mods must have started before main.
    std::string beforeMain = Env("GL_TEST_a-both");
    std::string dir = Dir();
    printf("client folder (loader imported at start-up as dbgcore.dll)\n");
    Check("mods started before main (at the entry point)", beforeMain == "client");
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
