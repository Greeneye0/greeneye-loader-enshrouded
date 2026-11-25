// Greeneye Loader: loads the mods in greeneye\<id>\ into Enshrouded and its dedicated server.
//
// Installed as dbgcore.dll next to enshrouded.exe / enshrouded_server.exe (or renamed to
// umpdc.dll or cfgmgr32.dll if another mod already uses that name: tools/proxyprobe/RESULTS.md).
// On Linux (Proton or Wine) it is renamed to dinput8.dll, the one name Wine loads from the game
// folder: dinput8_proxy.cpp.
// Every export of those three System32 DLLs is forwarded to the real one by the Windows loader
// itself (exports.h), so the loader has no code in any call the game makes.
//
// When it is loaded while the process starts (dbgcore: pulled in by dbghelp, a static import),
// the mods start at the exe's entry point, before any game code and outside the loader lock.
// Loaded later by name (umpdc, cfgmgr32), they start right away instead.
//
// For each greeneye\<id>\mod.json: skip it if disabled, for the other side (client/server) or
// for a newer API; otherwise load its DLL and call GreeneyeModStart (common/greeneye.h).
#include <windows.h>
#include <cstdarg>
#include <cstdio>
#include <string>
#include <vector>
#include "exports.h"
#include "loader.h"
#include "async_log.h"

namespace {
async_log::Writer g_log;
std::wstring g_logPath, g_oldLogPath;

// loader.log rolls over at 10 MB: the full one becomes loader.old.log (the one before goes), so a
// long session keeps its last 10-20 MB. A server ran 13 h with a portal mod logging every tick and
// left a 503 MB loader.log (2026-11-12). Called by the writer after each flush, under its lock.
constexpr long long kRollBytes = 10LL * 1024 * 1024;
bool g_rollFailed = false; // the file can't be renamed (open elsewhere): left to grow until the next start

void RollIfFull(FILE*& file)
{
    if (!file || g_rollFailed || _ftelli64(file) <= kRollBytes)
        return;
    fclose(file);
    if (!MoveFileExW(g_logPath.c_str(), g_oldLogPath.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        g_rollFailed = true;
        file = _wfsopen(g_logPath.c_str(), L"a", _SH_DENYWR);
        return;
    }
    file = _wfsopen(g_logPath.c_str(), L"w", _SH_DENYWR);
    if (file)
        std::fputs("[log continued: the earlier part is in loader.old.log]\n", file);
}
}

void Log(const char* fmt, ...)
{
    if (!g_log.File())
        return;
    char line[2048];
    SYSTEMTIME t;
    GetLocalTime(&t);
    int n = snprintf(line, sizeof(line) - 1, "[%02d:%02d:%02d.%03d] ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    va_list args;
    va_start(args, fmt);
    const int body = vsnprintf(line + n, sizeof(line) - 1 - n, fmt, args);
    va_end(args);
    n = body < 0 || n + body >= static_cast<int>(sizeof(line)) - 1 ? static_cast<int>(sizeof(line)) - 2 : n + body;
    line[n++] = '\n';
    g_log.Write(line, static_cast<size_t>(n));
}

std::string Narrow(const std::wstring& w)
{
    char buf[MAX_PATH * 3];
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, buf, sizeof(buf), nullptr, nullptr);
    return buf;
}

std::wstring Wide(const std::string& s)
{
    wchar_t buf[MAX_PATH];
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, buf, MAX_PATH);
    return buf;
}

namespace {

int g_side = 0;
std::wstring g_gameDir, g_modsDir;
std::string g_selfName;
bool g_started = false;

// "Wine 9.0" under Wine or Proton (ntdll exports wine_get_version there), else empty.
std::string WineVersion()
{
    using Fn = const char*(__cdecl*)();
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    auto fn = ntdll ? reinterpret_cast<Fn>(GetProcAddress(ntdll, "wine_get_version")) : nullptr;
    const char* v = fn ? fn() : nullptr;
    return fn ? std::string("Wine ") + (v ? v : "?") : std::string();
}

long long MsSinceStart()
{
    FILETIME created, exited, kernel, user, now;
    if (!GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user))
        return -1;
    GetSystemTimeAsFileTime(&now);
    ULARGE_INTEGER a{{created.dwLowDateTime, created.dwHighDateTime}}, b{{now.dwLowDateTime, now.dwHighDateTime}};
    return static_cast<long long>((b.QuadPart - a.QuadPart) / 10000);
}

// --- mod.json: just the few flat fields the loader needs ------------------------------------

const char* FindKey(const std::string& json, const char* key)
{
    std::string quoted = std::string("\"") + key + "\"";
    size_t at = json.find(quoted);
    if (at == std::string::npos)
        return nullptr;
    at = json.find(':', at + quoted.size());
    if (at == std::string::npos)
        return nullptr;
    const char* p = json.c_str() + at + 1;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')
        ++p;
    return p;
}

std::string JsonString(const std::string& json, const char* key, const char* fallback)
{
    const char* p = FindKey(json, key);
    if (!p || *p != '"')
        return fallback;
    std::string out;
    for (++p; *p && *p != '"'; ++p) {
        if (*p == '\\' && p[1])
            ++p;
        out += *p;
    }
    return out;
}

bool JsonBool(const std::string& json, const char* key, bool fallback)
{
    const char* p = FindKey(json, key);
    if (!p)
        return fallback;
    if (strncmp(p, "true", 4) == 0)
        return true;
    if (strncmp(p, "false", 5) == 0)
        return false;
    return fallback;
}

int JsonInt(const std::string& json, const char* key, int fallback)
{
    const char* p = FindKey(json, key);
    return p && (*p == '-' || (*p >= '0' && *p <= '9')) ? atoi(p) : fallback;
}

bool ReadText(const std::wstring& path, std::string& out)
{
    FILE* f = _wfopen(path.c_str(), L"rb");
    if (!f)
        return false;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
        out.append(buf, n);
    fclose(f);
    return true;
}

// --- starting the mods -----------------------------------------------------------------------

void StartMods()
{
    if (g_started)
        return;
    g_started = true;
    const char* sideName = g_side == kGreeneyeServer ? "server" : "client";
    Log("starting mods (%lld ms after the process started)", MsSinceStart());

    // Hot reload only on machines that ask for it (development).
    bool hotReload = GetFileAttributesW((g_modsDir + L"hotreload.enable").c_str()) != INVALID_FILE_ATTRIBUTES;
    hot::Init(g_side, g_gameDir, g_modsDir, hotReload);
    if (hotReload)
        Log("hot reload on (hotreload.enable)");

    std::vector<hot::ModInfo> mods;
    WIN32_FIND_DATAW fd;
    HANDLE find = FindFirstFileW((g_modsDir + L"*").c_str(), &fd);
    if (find == INVALID_HANDLE_VALUE) {
        Log("no greeneye folder content");
        return;
    }
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || fd.cFileName[0] == L'.')
            continue;
        std::wstring dir = g_modsDir + fd.cFileName + L"\\";
        std::string json;
        if (!ReadText(dir + L"mod.json", json))
            continue;
        hot::ModInfo m;
        m.dir = dir;
        m.id = JsonString(json, "id", Narrow(fd.cFileName).c_str());
        m.name = JsonString(json, "name", m.id.c_str());
        m.version = JsonString(json, "version", "?");
        std::string side = JsonString(json, "side", "client");
        int api = JsonInt(json, "api", 1);
        if (!JsonBool(json, "enabled", true)) {
            Log("%s %s: disabled in mod.json", m.name.c_str(), m.version.c_str());
            continue;
        }
        if (side != "both" && side != sideName) {
            Log("%s %s: %s only, skipped", m.name.c_str(), m.version.c_str(), side.c_str());
            continue;
        }
        if (api > static_cast<int>(kGreeneyeApiVersion)) {
            Log("%s %s: needs a newer Greeneye Loader (API %d, this is %u)", m.name.c_str(), m.version.c_str(), api,
                kGreeneyeApiVersion);
            continue;
        }
        m.dll = dir + Wide(JsonString(json, "dll", (m.id + ".dll").c_str()));
        mods.push_back(std::move(m));
    } while (FindNextFileW(find, &fd));
    FindClose(find);

    // A DLL that fails to start stays loaded: it may already have done something.
    for (const hot::ModInfo& m : mods) {
        if (GuardSkip("mod", m.id, m.dll)) {
            Log("%s %s: skipped: it crashed the game while starting before, and its DLL hasn't changed since "
                "(delete greeneye\\crashguard.txt to try it again)",
                m.name.c_str(), m.version.c_str());
            continue;
        }
        GuardBegin("mod", m.id, m.dll);
        const std::string outcome = hot::StartMod(m);
        GuardEnd();
        Log("%s %s: %s", m.name.c_str(), m.version.c_str(), outcome.c_str());
    }
    // Last, so it runs first in the frame, before any mod's hook of the same function.
    hot::InstallSafePoint();
    hot::WriteHookList();
    hot::StartWatcher();
}

// --- the exe's entry point: start the mods there when loaded during process start ----------

// "1.2.3 as dbgcore.dll" < "1.2.4": compares the leading dotted numbers.
bool VersionLess(const char* a, const char* b)
{
    for (;;) {
        char* endA;
        char* endB;
        long x = strtol(a, &endA, 10), y = strtol(b, &endB, 10);
        if (x != y)
            return x < y;
        if (*endA != '.' || *endB != '.')
            return *endA != '.' && *endB == '.';
        a = endA + 1;
        b = endB + 1;
    }
}

HMODULE g_self = nullptr;
std::string g_loaderName = "Greeneye Loader " GL_VERSION;
bool g_runMods = false, g_chainHost = false, g_hasMods = false, g_startupLoad = false;
long long g_loadMs = 0;
std::wstring g_exeName;

// --- several copies of the loader in one process: the newest runs the mods -------------------
// The per-process claim (greeneye::ClaimProcess "Loader", Local\Greeneye-Loader-<pid>) is still
// taken in DllMain, as every loader since 1.1 does: other hosts read it before the entry point
// (Kiln's kiln-greeneye steps aside when it is held). Which of OUR copies then runs the mods is
// decided at the exe's entry point, when every copy loaded at process start (e.g. dbgcore.dll from
// a mod zip and dinput8.dll from the Linux guide) has written its version here: the newest one.
// A claim already held by a copy before 1.2.4 (claimedByNew 0) stays theirs.

struct Copies {
    volatile LONG best;         // highest version, major*1000000 + minor*1000 + patch
    char bestName[64];          // "1.2.4 as dbgcore.dll"
    volatile LONG claimedByNew; // the claim was taken by a 1.2.4+ copy (one that writes here)
    volatile LONG running;      // a copy has taken the mods at the entry point
};
Copies* g_copies = nullptr;
bool g_claimHeld = false;   // this copy took the per-process claim in DllMain
bool g_oldOwnsClaim = false; // a copy before 1.2.4 holds it: it runs the mods

LONG VersionNumber(const char* v)
{
    long major = strtol(v, const_cast<char**>(&v), 10);
    long minor = *v == '.' ? strtol(v + 1, const_cast<char**>(&v), 10) : 0;
    long patch = *v == '.' ? strtol(v + 1, nullptr, 10) : 0;
    return major * 1000000 + minor * 1000 + patch;
}

void RegisterCopy()
{
    wchar_t name[96];
    swprintf_s(name, L"Local\\Greeneye-LoaderCopies-%lu", GetCurrentProcessId());
    // Never closed: it lives as long as the process.
    HANDLE map = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(Copies), name);
    if (!map)
        return;
    g_copies = static_cast<Copies*>(MapViewOfFile(map, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(Copies)));
    if (!g_copies)
        return;
    const LONG mine = VersionNumber(GL_VERSION);
    // DllMains run one at a time (loader lock), so name and number stay together.
    if (mine > g_copies->best) {
        g_copies->best = mine;
        snprintf(g_copies->bestName, sizeof(g_copies->bestName), "%s as %s", GL_VERSION, g_selfName.c_str());
    }
}

// In DllMain, start-up loads with a greeneye folder: the per-process claim, for other hosts to see.
void TakeClaim()
{
    if (greeneye::ClaimProcess(L"Loader")) { // kept open for the process life
        g_claimHeld = true;
        if (g_copies)
            g_copies->claimedByNew = 1;
    } else {
        g_oldOwnsClaim = !g_copies || !g_copies->claimedByNew;
    }
}

// At the entry point (or a late load): does this copy run the mods?
bool RunsTheMods()
{
    if (!g_startupLoad) // late (or the entry point couldn't be hooked): the first claim wins, as always
        return g_claimHeld || greeneye::ClaimProcess(L"Loader");
    if (g_oldOwnsClaim || !g_copies)
        return g_claimHeld; // no registry: the claim decides
    if (g_copies->best > VersionNumber(GL_VERSION))
        return false; // a newer copy takes them
    return InterlockedCompareExchange(&g_copies->running, 1, 0) == 0; // equal versions: the first here
}

// Runs the mods, or leaves them to another copy. Then the chain and the mods start.
void StartAll()
{
    if (g_hasMods) {
        const bool newest = !g_startupLoad || !g_copies || g_copies->best <= VersionNumber(GL_VERSION);
        if (RunsTheMods()) {
            g_runMods = true;
            g_loaderName = "Greeneye Loader " GL_VERSION " as " + g_selfName;
            SetEnvironmentVariableA("GREENEYE_LOADER", (GL_VERSION " as " + g_selfName).c_str());
        } else {
            char owner[96] = "";
            GetEnvironmentVariableA("GREENEYE_LOADER", owner, sizeof(owner));
            if (!*owner && !newest && g_copies)
                snprintf(owner, sizeof(owner), "%s", g_copies->bestName);
            if (FILE* f = _wfopen((g_modsDir + L"loader.second-copy.log").c_str(), L"w")) {
                fprintf(f, "Greeneye Loader " GL_VERSION " as %s: another copy (%s) runs the mods in this process, so this one "
                           "leaves them to it%s\n",
                        g_selfName.c_str(), *owner ? owner : "1.2.3 or older",
                        g_chainHost ? "; it still loads dinput8-chain.ini" : "");
                if (!*owner || VersionLess(owner, GL_VERSION))
                    fprintf(f, "This copy is newer: replace the other one with it, or delete the other one.\n");
                fclose(f);
            }
        }
    }
    if (!g_runMods && !g_chainHost)
        return;

    // The chain host that leaves the mods to another copy writes its chain lines to a log of its
    // own, so they aren't lost.
    const std::wstring logDir = g_hasMods ? g_modsDir : g_gameDir;
    g_logPath = logDir + (g_runMods ? L"loader.log" : L"loader.chain.log");
    g_oldLogPath = logDir + (g_runMods ? L"loader.old.log" : L"loader.chain.old.log");
    g_log.Open(_wfsopen(g_logPath.c_str(), L"w", _SH_DENYWR), &RollIfFull);
    const std::string wine = WineVersion();
    Log("Greeneye Loader " GL_VERSION " loaded as %s in %s (%s), %lld ms after start, %s load%s%s", g_selfName.c_str(),
        Narrow(g_exeName).c_str(), g_side == kGreeneyeServer ? "server" : "game", g_loadMs,
        g_startupLoad ? "start-up" : "late", wine.empty() ? "" : ", on ", wine.c_str());
    if (g_startupLoad)
        Log(g_runMods ? "mods start at the entry point" : "the chain loads at the entry point");
    GuardInit(logDir);

    // The chained dinput8 mods first (they would have loaded before the exe's entry point as
    // dinput8.dll themselves), then the Greeneye mods.
    if (g_chainHost)
        LoadChain(g_gameDir, g_self);
    if (g_runMods)
        StartMods();
}

using EntryFn = DWORD(WINAPI*)(void* peb);
uint8_t* g_entry = nullptr;
uint8_t g_savedEntry[12];

DWORD WINAPI EntryHook(void* peb)
{
    // Put the entry point back exactly as it was, start the mods, then run the game.
    DWORD old;
    if (VirtualProtect(g_entry, sizeof(g_savedEntry), PAGE_EXECUTE_READWRITE, &old)) {
        memcpy(g_entry, g_savedEntry, sizeof(g_savedEntry));
        VirtualProtect(g_entry, sizeof(g_savedEntry), old, &old);
        FlushInstructionCache(GetCurrentProcess(), g_entry, sizeof(g_savedEntry));
    }
    StartAll();
    return reinterpret_cast<EntryFn>(g_entry)(peb);
}

bool HookEntryPoint()
{
    auto base = reinterpret_cast<uint8_t*>(GetModuleHandleW(nullptr));
    auto nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + reinterpret_cast<IMAGE_DOS_HEADER*>(base)->e_lfanew);
    if (!nt->OptionalHeader.AddressOfEntryPoint)
        return false;
    g_entry = base + nt->OptionalHeader.AddressOfEntryPoint;
    memcpy(g_savedEntry, g_entry, sizeof(g_savedEntry));
    uint8_t jump[12] = {0x48, 0xB8}; // mov rax, imm64; jmp rax
    void* to = reinterpret_cast<void*>(&EntryHook);
    memcpy(jump + 2, &to, 8);
    jump[10] = 0xFF;
    jump[11] = 0xE0;
    DWORD old;
    if (!VirtualProtect(g_entry, sizeof(jump), PAGE_EXECUTE_READWRITE, &old))
        return false;
    memcpy(g_entry, jump, sizeof(jump));
    VirtualProtect(g_entry, sizeof(jump), old, &old);
    FlushInstructionCache(GetCurrentProcess(), g_entry, sizeof(jump));
    return true;
}

} // namespace

const char* GlLoaderName()
{
    return g_loaderName.c_str();
}

BOOL WINAPI DllMain(HINSTANCE self, DWORD reason, LPVOID reserved)
{
    if (reason != DLL_PROCESS_ATTACH)
        return TRUE;
    DisableThreadLibraryCalls(self);

    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    const wchar_t* exeName = wcsrchr(exe, L'\\');
    exeName = exeName ? exeName + 1 : exe;
    if (_wcsicmp(exeName, L"enshrouded.exe") == 0)
        g_side = kGreeneyeClient;
    else if (_wcsicmp(exeName, L"enshrouded_server.exe") == 0)
        g_side = kGreeneyeServer;
    else
        return TRUE; // some other program: only the forwarders

    wchar_t self_[MAX_PATH];
    GetModuleFileNameW(self, self_, MAX_PATH);
    const wchar_t* selfName = wcsrchr(self_, L'\\');
    selfName = selfName ? selfName + 1 : self_;
    g_selfName = Narrow(selfName);
    wchar_t game[MAX_PATH];
    greeneye::GameDir(game, MAX_PATH);
    g_gameDir = game;
    g_modsDir = g_gameDir + L"greeneye\\";
    g_self = self;

    // Two jobs: the Greeneye mods (one copy per process: ClaimProcess) and, as dinput8.dll, the
    // other dinput8 mods in dinput8-chain.ini. Only one module can be dinput8.dll, so the chain
    // needs no claim, and it loads even when another copy (e.g. dbgcore.dll) runs the mods.
    // Chain host only as the game's own dinput8.dll: in the game folder and the process's module
    // of that name. A copy named dinput8.dll that someone else's chain loader loads from another
    // folder leaves dinput8-chain.ini to that loader (it is usually theirs).
    const std::wstring selfDir(self_, static_cast<size_t>(selfName - self_));
    g_chainHost = _wcsicmp(selfName, L"dinput8.dll") == 0 && _wcsicmp(selfDir.c_str(), g_gameDir.c_str()) == 0 &&
                  GetModuleHandleW(L"dinput8.dll") == self &&
                  GetFileAttributesW((g_gameDir + L"dinput8-chain.ini").c_str()) != INVALID_FILE_ATTRIBUTES;
    g_hasMods = GetFileAttributesW(g_modsDir.c_str()) != INVALID_FILE_ATTRIBUTES;
    if (!g_hasMods && !g_chainHost)
        return TRUE; // nothing to load
    g_exeName = exeName;
    g_loadMs = MsSinceStart();

    // Start-up load: wait for the entry point (single-threaded, no loader lock, before any game
    // code; every copy loaded at start-up has registered by then). Late load: the game already
    // runs, so start now.
    g_startupLoad = reserved != nullptr;
    if (g_startupLoad) {
        RegisterCopy();
        if (g_hasMods)
            TakeClaim();
    }
    if (!g_startupLoad || !HookEntryPoint()) {
        g_startupLoad = false;
        StartAll();
    }
    return TRUE;
}
