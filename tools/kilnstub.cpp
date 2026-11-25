// Stands in for another host that steps aside while the Greeneye Loader runs (Kiln's kiln-greeneye
// checks the loader's per-process claim before the game's entry point). Imported by the client test
// exe after the loader, it records in its DllMain whether the claim is already held.
#include <windows.h>
#include <cwchar>

extern "C" __declspec(dllexport) int KilnStub()
{
    return 1;
}

BOOL WINAPI DllMain(HINSTANCE, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        wchar_t name[96];
        swprintf_s(name, L"Local\\Greeneye-Loader-%lu", GetCurrentProcessId());
        HANDLE claim = OpenMutexW(SYNCHRONIZE, FALSE, name);
        SetEnvironmentVariableW(L"GL_TEST_claim_before_entry", claim ? L"1" : L"0");
        if (claim)
            CloseHandle(claim);
    }
    return TRUE;
}
