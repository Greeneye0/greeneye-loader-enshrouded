#pragma once
// Shared between the loader's start-up (main.cpp) and its hook owner / hot reload (hot.cpp).
#include <string>
#include "greeneye.h"

#define GL_VERSION "1.2.2"

void Log(const char* fmt, ...);
std::string Narrow(const std::wstring& w);
std::wstring Wide(const std::string& s);

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

} // namespace hot
