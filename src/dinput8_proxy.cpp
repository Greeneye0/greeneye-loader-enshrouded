// The loader as dinput8.dll: the name for Linux (Proton or Wine), where the dbgcore name never
// loads. enshrouded.exe and enshrouded_server.exe both import DINPUT8.dll directly, so Wine loads
// a dinput8.dll from the game folder at process start once the DLL override allows it:
//   WINEDLLOVERRIDES="dinput8=n,b"
//
// dinput8 has six exports. Unlike the dbgcore/umpdc/cfgmgr32 ones (exports.h), they are not
// forwarded by the Windows loader: Wine can't resolve the \\.\GLOBALROOT\ path those forwarders
// use. Each one is a small function that loads the real dinput8.dll from the system folder by its
// full path (so it never finds this file again) on first use and calls on to it. On Windows the
// same stubs reach System32\dinput8.dll; under Wine the system folder's file is Wine's own.
//
// Chain loading: only one DLL can be dinput8.dll, so the other mods that want that name are
// listed in dinput8-chain.ini next to the exe. It follows the shared spec that EMF, Kiln,
// ShroudForge, XHL-Core and Auto Loot Plus read (docs/modding/dinput8-chain.md in
// matenm/emf-enshrouded), so a player's list works whichever loader holds dinput8.dll:
//   - UTF-8 with or without a BOM, UTF-16 LE with a BOM; the ANSI code page when not valid UTF-8.
//   - Blank lines skipped; ';' '#' '//' start a comment line ("; x.dll" switches x.dll off); a
//     comment may follow a name after a space or tab; a name may stand in double quotes.
//   - [Chain] is shared; lines before any section count as [Chain]; other sections belong to other
//     tools and are ignored. One exception, as Kiln makes it: [EMF] is read too, because EMF's
//     installers renamed other tools' dinput8.dll (e.g. Creative Mode) into it, and a player who
//     moves from EMF would otherwise lose them.
//   - "key = value" counts as an entry when the value names a .dll; other keys are other tools'
//     settings (XHL's Enabled= and Forward=) and are ignored.
//   - Paths relative to the exe's folder, '/' read as '\'; absolute paths, drive letters, UNC
//     paths and any ".." part are refused. Only .dll files. Never this file itself, nor another
//     copy of the Greeneye Loader. Each DLL once, at its first place, in file order; at most 256.
//   - Started from the loader at the exe's entry point (before any game code), with LoadLibrary;
//     a missing DLL or one that fails to load is skipped and reported, and the game goes on.
//   - The game's DirectInput8Create always goes to Windows' own dinput8.dll: chained DLLs get no
//     DirectInput calls.
// One line per DLL goes to loader.log: started, skipped (switched off, refused, already loaded,
// crash guard) or failed with Windows' error number.
#include <windows.h>
#include <string>
#include <vector>
#include "loader.h"

#pragma comment(linker, "/export:DirectInput8Create=GlDirectInput8Create")
#pragma comment(linker, "/export:DllCanUnloadNow=GlDllCanUnloadNow,PRIVATE")
#pragma comment(linker, "/export:DllGetClassObject=GlDllGetClassObject,PRIVATE")
#pragma comment(linker, "/export:DllRegisterServer=GlDllRegisterServer,PRIVATE")
#pragma comment(linker, "/export:DllUnregisterServer=GlDllUnregisterServer,PRIVATE")
#pragma comment(linker, "/export:GetdfDIJoystick=GlGetdfDIJoystick")

namespace {

using CreateFn = HRESULT(WINAPI*)(HINSTANCE, DWORD, REFIID, void**, IUnknown*);

HMODULE RealDinput8()
{
    static HMODULE real = [] {
        wchar_t path[MAX_PATH];
        UINT n = GetSystemDirectoryW(path, MAX_PATH);
        if (!n || n + 13 >= MAX_PATH)
            return static_cast<HMODULE>(nullptr);
        wcscpy_s(path + n, MAX_PATH - n, L"\\dinput8.dll");
        return LoadLibraryW(path);
    }();
    return real;
}

template <typename Fn>
Fn Real(const char* name)
{
    HMODULE m = RealDinput8();
    return m ? reinterpret_cast<Fn>(GetProcAddress(m, name)) : nullptr;
}

// --- dinput8-chain.ini -----------------------------------------------------------------------

constexpr size_t kMaxIniBytes = 1024 * 1024;
constexpr size_t kMaxEntries = 256; // the spec's limit

// The file as text: UTF-16 LE with a BOM, UTF-8 with or without one, else the ANSI code page.
bool ReadIni(const std::wstring& path, std::wstring& text)
{
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) {
        DWORD e = GetLastError();
        if (e != ERROR_FILE_NOT_FOUND && e != ERROR_PATH_NOT_FOUND)
            Log("chain: dinput8-chain.ini could not be opened (error %lu); nothing chained", e);
        return false;
    }
    LARGE_INTEGER size{};
    GetFileSizeEx(f, &size);
    if (size.QuadPart > static_cast<LONGLONG>(kMaxIniBytes)) {
        CloseHandle(f);
        Log("chain: dinput8-chain.ini is larger than %zu bytes; nothing chained", kMaxIniBytes);
        return false;
    }
    std::string bytes(static_cast<size_t>(size.QuadPart), '\0');
    DWORD got = 0;
    BOOL ok = bytes.empty() || ReadFile(f, bytes.data(), static_cast<DWORD>(bytes.size()), &got, nullptr);
    CloseHandle(f);
    if (!ok || got != bytes.size()) {
        Log("chain: dinput8-chain.ini could not be read; nothing chained");
        return false;
    }
    if (bytes.size() >= 2 && static_cast<uint8_t>(bytes[0]) == 0xFF && static_cast<uint8_t>(bytes[1]) == 0xFE) {
        text.assign(reinterpret_cast<const wchar_t*>(bytes.data() + 2), (bytes.size() - 2) / 2);
        return true;
    }
    size_t skip = bytes.size() >= 3 && bytes.compare(0, 3, "\xEF\xBB\xBF") == 0 ? 3 : 0;
    const char* data = bytes.data() + skip;
    const int len = static_cast<int>(bytes.size() - skip);
    UINT page = CP_UTF8;
    int n = len ? MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, data, len, nullptr, 0) : 0;
    if (len && n <= 0) { // not valid UTF-8: a file saved by an old Notepad
        page = CP_ACP;
        n = MultiByteToWideChar(CP_ACP, 0, data, len, nullptr, 0);
    }
    text.resize(static_cast<size_t>(n > 0 ? n : 0));
    if (n > 0)
        MultiByteToWideChar(page, 0, data, len, text.data(), n);
    return true;
}

// A copy of the Greeneye Loader (any version: they all carry this ProductName), which never
// belongs in the chain: one copy runs the mods, the others would only sit idle.
// version.dll comes from the system folder by full path, only when a chain is read: a static
// import would let a version.dll proxy in the game folder (another popular mod name) load first.
bool IsGreeneyeLoader(const std::wstring& path, std::string& version)
{
    using SizeFn = DWORD(WINAPI*)(LPCWSTR, LPDWORD);
    using InfoFn = BOOL(WINAPI*)(LPCWSTR, DWORD, DWORD, LPVOID);
    using QueryFn = BOOL(WINAPI*)(LPCVOID, LPCWSTR, LPVOID*, PUINT);
    static HMODULE ver = [] {
        wchar_t p[MAX_PATH];
        UINT n = GetSystemDirectoryW(p, MAX_PATH);
        if (!n || n + 13 >= MAX_PATH)
            return static_cast<HMODULE>(nullptr);
        wcscpy_s(p + n, MAX_PATH - n, L"\\version.dll");
        return LoadLibraryW(p);
    }();
    // Without version info (a stripped copy, or a build older than 1.1.0): the loader's own log
    // text and its System32 forwarder path, both in every build. Loader files are small.
    auto bytesSay = [&path] {
        HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
        if (f == INVALID_HANDLE_VALUE)
            return false;
        LARGE_INTEGER size{};
        GetFileSizeEx(f, &size);
        std::string bytes;
        if (size.QuadPart > 0 && size.QuadPart < 4 * 1024 * 1024) {
            bytes.resize(static_cast<size_t>(size.QuadPart));
            DWORD got = 0;
            if (!ReadFile(f, bytes.data(), static_cast<DWORD>(bytes.size()), &got, nullptr))
                bytes.clear();
            bytes.resize(got);
        }
        CloseHandle(f);
        return bytes.find("Greeneye Loader ") != std::string::npos && bytes.find("GLOBALROOT\\SystemRoot\\System32\\") != std::string::npos;
    };
    auto sizeFn = ver ? reinterpret_cast<SizeFn>(GetProcAddress(ver, "GetFileVersionInfoSizeW")) : nullptr;
    auto infoFn = ver ? reinterpret_cast<InfoFn>(GetProcAddress(ver, "GetFileVersionInfoW")) : nullptr;
    auto queryFn = ver ? reinterpret_cast<QueryFn>(GetProcAddress(ver, "VerQueryValueW")) : nullptr;
    if (!sizeFn || !infoFn || !queryFn)
        return bytesSay();
    DWORD ignored = 0, size = sizeFn(path.c_str(), &ignored);
    if (!size)
        return bytesSay();
    std::vector<char> info(size);
    if (!infoFn(path.c_str(), 0, size, info.data()))
        return bytesSay();
    wchar_t* value = nullptr;
    UINT len = 0;
    if (!queryFn(info.data(), L"\\StringFileInfo\\040904b0\\ProductName", reinterpret_cast<void**>(&value), &len) || !value ||
        wcscmp(value, L"Greeneye Loader") != 0)
        return bytesSay();
    if (queryFn(info.data(), L"\\StringFileInfo\\040904b0\\ProductVersion", reinterpret_cast<void**>(&value), &len) && value)
        version = Narrow(value);
    return true;
}

std::wstring TrimBlanks(const std::wstring& s)
{
    size_t a = s.find_first_not_of(L" \t");
    if (a == std::wstring::npos)
        return std::wstring();
    size_t b = s.find_last_not_of(L" \t");
    return s.substr(a, b - a + 1);
}

bool IsComment(const std::wstring& s)
{
    return !s.empty() && (s[0] == L';' || s[0] == L'#' || (s.size() > 1 && s[0] == L'/' && s[1] == L'/'));
}

// A DLL line's name: a comment after a space or tab goes first, then "key =", then the quotes
// (in that order, as EMF and Kiln read it).
std::wstring EntryValue(std::wstring line)
{
    for (size_t i = 0; i + 1 < line.size(); ++i)
        if ((line[i] == L' ' || line[i] == L'\t') && (line[i + 1] == L';' || line[i + 1] == L'#')) {
            line.resize(i);
            break;
        }
    size_t eq = line.find(L'=');
    std::wstring v = TrimBlanks(eq == std::wstring::npos ? line : line.substr(eq + 1));
    if (v.size() >= 2 && v.front() == L'"' && v.back() == L'"')
        v = v.substr(1, v.size() - 2);
    return v;
}

bool EndsWithDll(const std::wstring& v)
{
    return v.size() > 4 && _wcsicmp(v.c_str() + v.size() - 4, L".dll") == 0;
}

// Absolute, a drive letter, UNC, or any ".." part: the file must not reach outside the game folder.
bool Outside(const std::wstring& v)
{
    if (v.empty() || v[0] == L'\\' || v.find(L':') != std::wstring::npos)
        return true;
    for (size_t i = 0; i + 1 < v.size(); ++i) {
        const bool start = i == 0 || v[i - 1] == L'\\';
        if (start && v[i] == L'.' && v[i + 1] == L'.' && (i + 2 == v.size() || v[i + 2] == L'\\'))
            return true;
    }
    return false;
}

} // namespace

// Called at the entry point (or right away on a late load) while this loader is dinput8.dll.
void LoadChain(const std::wstring& gameDir, HMODULE self)
{
    std::wstring text;
    if (!ReadIni(gameDir + L"dinput8-chain.ini", text))
        return; // no file (silent), or one that couldn't be read (logged)

    struct Entry {
        std::wstring name, path;
    };
    std::vector<Entry> entries;
    unsigned duplicate = 0, over = 0;
    bool listed = true; // lines before any section count as [Chain]
    std::wstring section = L"(before any section)";
    size_t at = 0;
    while (at <= text.size()) {
        size_t end = text.find_first_of(L"\r\n", at);
        std::wstring line = TrimBlanks(text.substr(at, end == std::wstring::npos ? std::wstring::npos : end - at));
        at = end == std::wstring::npos ? text.size() + 1 : end + 1;
        if (line.empty())
            continue;
        if (line.size() >= 2 && line.front() == L'[' && line.back() == L']') {
            section = TrimBlanks(line.substr(1, line.size() - 2));
            listed = _wcsicmp(section.c_str(), L"Chain") == 0 || _wcsicmp(section.c_str(), L"EMF") == 0;
            continue;
        }
        if (!listed)
            continue; // another tool's section
        if (IsComment(line)) {
            // "; mymod.dll": the player switched that DLL off.
            size_t from = line.find_first_not_of(L";#/ \t");
            std::wstring v = from == std::wstring::npos ? std::wstring() : EntryValue(line.substr(from));
            if (EndsWithDll(v) && v.find(L' ') == std::wstring::npos)
                Log("chain: %s skipped: switched off", Narrow(v).c_str());
            continue;
        }
        std::wstring v = EntryValue(line);
        if (v.empty())
            continue;
        if (!EndsWithDll(v)) {
            if (line.find(L'=') == std::wstring::npos) // "key = value" lines are other tools' settings
                Log("chain: %s skipped: does not name a .dll", Narrow(line).c_str());
            continue;
        }
        for (wchar_t& c : v)
            if (c == L'/')
                c = L'\\';
        if (Outside(v)) {
            Log("chain: %s refused: outside the game folder (dinput8-chain.ini takes relative paths only)", Narrow(v).c_str());
            continue;
        }
        wchar_t full[MAX_PATH];
        DWORD n = GetFullPathNameW((gameDir + v).c_str(), MAX_PATH, full, nullptr);
        if (!n || n >= MAX_PATH) {
            Log("chain: %s refused: the path is too long", Narrow(v).c_str());
            continue;
        }
        bool dup = false;
        for (const Entry& e : entries)
            dup = dup || _wcsicmp(e.path.c_str(), full) == 0;
        if (dup) {
            ++duplicate;
            continue;
        }
        if (entries.size() >= kMaxEntries) {
            ++over;
            continue;
        }
        entries.push_back({v, full});
    }
    Log("chain: dinput8-chain.ini lists %zu DLL%s (%u listed twice, %u over the limit of %zu)", entries.size(),
        entries.size() == 1 ? "" : "s", duplicate, over, kMaxEntries);

    wchar_t selfPath[MAX_PATH];
    GetModuleFileNameW(self, selfPath, MAX_PATH);
    for (const Entry& e : entries) {
        const std::string name = Narrow(e.name);
        if (_wcsicmp(e.path.c_str(), selfPath) == 0) {
            Log("chain: %s refused: that is dinput8.dll itself", name.c_str());
            continue;
        }
        if (GetFileAttributesW(e.path.c_str()) == INVALID_FILE_ATTRIBUTES) {
            Log("chain: %s skipped: file not found", name.c_str());
            continue;
        }
        std::string other;
        if (IsGreeneyeLoader(e.path, other)) {
            Log("chain: %s refused: another copy of the Greeneye Loader (%s); one copy as dinput8.dll is enough", name.c_str(),
                other.empty() ? "?" : other.c_str());
            continue;
        }
        if (GetModuleHandleW(e.path.c_str())) {
            Log("chain: %s skipped: already loaded", name.c_str());
            continue;
        }
        if (GuardSkip("chain", name, e.path)) {
            Log("chain: %s skipped: it crashed the game while loading before, and it hasn't changed since (delete "
                "greeneye\\crashguard.txt to try it again)",
                name.c_str());
            continue;
        }
        // Its own folder first for its dependencies, like a DLL loaded from the game folder.
        GuardBegin("chain", name, e.path);
        HMODULE m = LoadLibraryExW(e.path.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
        const DWORD error = GetLastError();
        GuardEnd();
        if (m)
            Log("chain: %s started", name.c_str());
        else
            Log("chain: %s failed (error %lu)", name.c_str(), error);
    }
}

extern "C" {

// Always Windows' (or Wine's) own dinput8.dll: chained DLLs get no DirectInput calls.
HRESULT WINAPI GlDirectInput8Create(HINSTANCE inst, DWORD version, REFIID iid, void** out, IUnknown* outer)
{
    static CreateFn fn = Real<CreateFn>("DirectInput8Create");
    if (!fn) {
        if (out)
            *out = nullptr;
        return E_FAIL;
    }
    return fn(inst, version, iid, out, outer);
}

HRESULT WINAPI GlDllCanUnloadNow()
{
    using Fn = HRESULT(WINAPI*)();
    static Fn fn = Real<Fn>("DllCanUnloadNow");
    return fn ? fn() : S_FALSE;
}

HRESULT WINAPI GlDllGetClassObject(REFCLSID clsid, REFIID iid, void** out)
{
    using Fn = HRESULT(WINAPI*)(REFCLSID, REFIID, void**);
    static Fn fn = Real<Fn>("DllGetClassObject");
    if (!fn) {
        if (out)
            *out = nullptr;
        return CLASS_E_CLASSNOTAVAILABLE;
    }
    return fn(clsid, iid, out);
}

HRESULT WINAPI GlDllRegisterServer()
{
    using Fn = HRESULT(WINAPI*)();
    static Fn fn = Real<Fn>("DllRegisterServer");
    return fn ? fn() : E_FAIL;
}

HRESULT WINAPI GlDllUnregisterServer()
{
    using Fn = HRESULT(WINAPI*)();
    static Fn fn = Real<Fn>("DllUnregisterServer");
    return fn ? fn() : E_FAIL;
}

const void* WINAPI GlGetdfDIJoystick()
{
    using Fn = const void*(WINAPI*)();
    static Fn fn = Real<Fn>("GetdfDIJoystick");
    return fn ? fn() : nullptr;
}

} // extern "C"
