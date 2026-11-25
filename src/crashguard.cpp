// Crash guard: a mod (or a chained dinput8 DLL) that took the whole game down while it started is
// skipped at the next launches, until its DLL file changes (an update) or the player deletes
// greeneye\crashguard.txt.
//
// Before each start the loader writes greeneye\loader.starting naming what starts; it deletes it
// once the start returns. A start that faults is caught already (hot.cpp SafeStart), so the file
// is left behind only when the process died inside that start: a crash the loader couldn't
// catch, or the player killing a hung game. At the next launch that name goes into
// crashguard.txt with its DLL's size and time, and it is skipped while they stay the same.
//
// Only the start itself is watched (the mod's DllMain and GreeneyeModStart, a chained DLL's
// DllMain); a crash later in the game can't be pinned on one mod this way.
#include <windows.h>
#include <cstdio>
#include <string>
#include <vector>
#include "loader.h"

namespace {

std::wstring g_dir; // greeneye\ (or the game folder when there is none)

struct Entry {
    std::string kind, key; // "mod" + id, or "chain" + the entry as written in dinput8-chain.ini
    unsigned long long size = 0, time = 0;
};

bool FileStamp(const std::wstring& path, unsigned long long& size, unsigned long long& time)
{
    WIN32_FILE_ATTRIBUTE_DATA a;
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &a))
        return false;
    size = (static_cast<unsigned long long>(a.nFileSizeHigh) << 32) | a.nFileSizeLow;
    time = (static_cast<unsigned long long>(a.ftLastWriteTime.dwHighDateTime) << 32) | a.ftLastWriteTime.dwLowDateTime;
    return true;
}

std::vector<Entry> ReadList()
{
    std::vector<Entry> list;
    FILE* f = _wfopen((g_dir + L"crashguard.txt").c_str(), L"r");
    if (!f)
        return list;
    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#' || line[0] == '\n')
            continue;
        char kind[16], key[512];
        unsigned long long size, time;
        if (sscanf_s(line, "%15[^\t]\t%511[^\t]\t%llu\t%llu", kind, static_cast<unsigned>(sizeof(kind)), key,
                     static_cast<unsigned>(sizeof(key)), &size, &time) == 4)
            list.push_back({kind, key, size, time});
    }
    fclose(f);
    return list;
}

void WriteList(const std::vector<Entry>& list)
{
    const std::wstring path = g_dir + L"crashguard.txt";
    if (list.empty()) {
        DeleteFileW(path.c_str());
        return;
    }
    FILE* f = _wfopen(path.c_str(), L"w");
    if (!f)
        return;
    fputs("# Greeneye Loader crash guard: these crashed the game while starting, so the loader skips them\n"
          "# until their DLL changes (an update). Delete this file to try them all again.\n",
          f);
    for (const Entry& e : list)
        fprintf(f, "%s\t%s\t%llu\t%llu\n", e.kind.c_str(), e.key.c_str(), e.size, e.time);
    fclose(f);
}

} // namespace

void GuardInit(const std::wstring& dir)
{
    if (!g_dir.empty())
        return; // once per copy
    g_dir = dir;
    const std::wstring marker = g_dir + L"loader.starting";
    FILE* f = _wfopen(marker.c_str(), L"r");
    if (!f)
        return;
    char kind[16] = "", key[512] = "", path[1024] = "";
    char line[1600] = "";
    if (fgets(line, sizeof(line), f))
        sscanf_s(line, "%15[^\t]\t%511[^\t]\t%1023[^\n]", kind, static_cast<unsigned>(sizeof(kind)), key,
                 static_cast<unsigned>(sizeof(key)), path, static_cast<unsigned>(sizeof(path)));
    fclose(f);
    DeleteFileW(marker.c_str());
    Entry e{kind, key};
    if (!*kind || !*key || !FileStamp(Wide(path), e.size, e.time))
        return;
    std::vector<Entry> list = ReadList();
    bool known = false;
    for (Entry& old : list)
        if (old.kind == e.kind && old.key == e.key) {
            old = e;
            known = true;
        }
    if (!known)
        list.push_back(e);
    WriteList(list);
    Log("crash guard: %s %s was starting when the game last closed without warning; it is skipped until its DLL changes "
        "(delete greeneye\\crashguard.txt to try it again)",
        kind, key);
}

bool GuardSkip(const char* kind, const std::string& key, const std::wstring& path)
{
    std::vector<Entry> list = ReadList();
    for (size_t i = 0; i < list.size(); ++i) {
        if (list[i].kind != kind || list[i].key != key)
            continue;
        unsigned long long size = 0, time = 0;
        if (FileStamp(path, size, time) && size == list[i].size && time == list[i].time)
            return true;
        // Changed (or gone): an update; try it again.
        Log("crash guard: %s %s changed since it crashed the game, so it starts again", kind, key.c_str());
        list.erase(list.begin() + static_cast<ptrdiff_t>(i));
        WriteList(list);
        return false;
    }
    return false;
}

void GuardBegin(const char* kind, const std::string& key, const std::wstring& path)
{
    if (FILE* f = _wfopen((g_dir + L"loader.starting").c_str(), L"w")) {
        fprintf(f, "%s\t%s\t%s\n", kind, key.c_str(), Narrow(path).c_str());
        fclose(f);
    }
}

void GuardEnd()
{
    DeleteFileW((g_dir + L"loader.starting").c_str());
}
