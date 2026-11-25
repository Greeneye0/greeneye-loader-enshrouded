#pragma once
// Shared between the loader's start-up (main.cpp) and its hook owner / hot reload (hot.cpp).
#include <windows.h>
#include <string>
#include "greeneye.h"

#ifndef GL_VERSION // tests build a "newer" copy with /DGL_VERSION
#define GL_VERSION "1.2.4"
#endif

void Log(const char* fmt, ...);
// "Greeneye Loader 1.2.4 as dbgcore.dll" (GreeneyeHost::loader, GREENEYE_LOADER).
const char* GlLoaderName();
std::string Narrow(const std::wstring& w);
std::wstring Wide(const std::string& s);

// dinput8_proxy.cpp: loads the DLLs listed in <game>\dinput8-chain.ini (only while the loader is
// dinput8.dll). gameDir has a trailing backslash.
void LoadChain(const std::wstring& gameDir, HMODULE self);

// crashguard.cpp: skip what crashed the game while it started (kind "mod" or "chain").
void GuardInit(const std::wstring& dir); // once, after the log is open: picks up the last run's crash
bool GuardSkip(const char* kind, const std::string& key, const std::wstring& path);
void GuardBegin(const char* kind, const std::string& key, const std::wstring& path);
void GuardEnd();

namespace hot {

// One mod from greeneye\<id>\mod.json.
struct ModInfo {
    std::string id, name, version;
    std::wstring dir, dll; // dir has a trailing backslash; dll is the full path
};

// Before any mod starts. hotReload: greeneye\hotreload.enable exists (load from shadow copies,
// watch for reload requests, swap generations).
void Init(int side, const std::wstring& gameDir, const std::wstring& modsDir, bool hotReload);

// The loader's own safe point for reloads in the game (top of the UI frame). After the mods
// started, so it is the outermost hook of that function and runs before any mod's.
void InstallSafePoint();

// Loads and starts the first generation of a mod. Returns a line for loader.log.
std::string StartMod(const ModInfo& mod);

// After every mod started: the reload watcher (hot reload only).
void StartWatcher();

// After the mods started: greeneye\hooks.txt (every patched place, its owner, and whether
// something else had patched it first), and a summary line in loader.log.
void WriteHookList();

} // namespace hot
