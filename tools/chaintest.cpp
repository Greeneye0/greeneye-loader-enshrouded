// A stand-in for another mod that wants the dinput8.dll name (Creative Mode, XHL, ...), chained by
// the Greeneye Loader through dinput8-chain.ini (tools\tests.bat, linux folder). Like a real
// dinput8 proxy it exports DirectInput8Create. By EMF's chain spec the game's DirectInput calls go
// to Windows' dinput8.dll, never to a chained DLL, so the test expects this one never to be called.
#include <windows.h>

extern "C" __declspec(dllexport) HRESULT WINAPI DirectInput8Create(HINSTANCE inst, DWORD version, REFIID iid, void** out,
                                                                  IUnknown* outer)
{
    char count[16] = "0";
    GetEnvironmentVariableA("GL_TEST_chain_create", count, sizeof(count));
    wsprintfA(count, "%d", atoi(count) + 1);
    SetEnvironmentVariableA("GL_TEST_chain_create", count);
    using Fn = HRESULT(WINAPI*)(HINSTANCE, DWORD, REFIID, void**, IUnknown*);
    HMODULE next = GetModuleHandleA("dinput8.dll"); // the Greeneye Loader, already loaded
    auto fn = next ? reinterpret_cast<Fn>(GetProcAddress(next, "DirectInput8Create")) : nullptr;
    return fn ? fn(inst, version, iid, out, outer) : E_FAIL;
}

BOOL WINAPI DllMain(HINSTANCE, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
        SetEnvironmentVariableA("GL_TEST_chain_loaded", "1");
    return TRUE;
}
