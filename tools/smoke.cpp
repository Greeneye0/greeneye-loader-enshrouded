// Smoke test with the real mods (tools\smoke.bat): a fake game folder (enshrouded.exe) and a fake
// server folder (enshrouded_server.exe), each with the loader as dbgcore.dll and the real mod
// DLLs in greeneye\. Outside the real game the mods find no game code, so each must start, log
// "inactive" into its own folder, and leave the process running. Prints what each wrote.
#include <windows.h>
#include <cstdio>
#include <string>

extern "C" __declspec(dllimport) BOOL WINAPI MiniDumpWriteDump(HANDLE, DWORD, HANDLE, int, void*, void*, void*);
static void* volatile g_keepImport = reinterpret_cast<void*>(&MiniDumpWriteDump);

static void Show(const std::string& path)
{
    printf("--- %s\n", path.c_str());
    if (FILE* f = fopen(path.c_str(), "rb")) {
        char line[512];
        while (fgets(line, sizeof(line), f))
            printf("    %s", line);
        fclose(f);
    } else {
        printf("    (missing)\n");
    }
}

int main(int argc, char** argv)
{
    char exe[MAX_PATH];
    GetModuleFileNameA(nullptr, exe, MAX_PATH);
    *strrchr(exe, '\\') = 0;
    std::string dir = exe;
    Show(dir + "\\greeneye\\loader.log");
    for (int i = 1; i < argc; ++i)
        Show(dir + "\\greeneye\\" + argv[i]);
    return 0;
}
