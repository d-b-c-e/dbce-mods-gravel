// The unattended-test switches of a DirectInput proxy (STD-033), in one place for every proxy game:
//  - %LOCALAPPDATA%\dbce\<game>\ffb-fence.on at start: the game gets the closed wrapper (dbce_fence.hpp), so every
//    device stays visible and readable and no force can reach one;
//  - %LOCALAPPDATA%\dbce\<game>\inject.on at start, with the fence: inject.txt in the same folder carries
//    "inject raw ..." / "inject action ..." lines (dbce_inject.hpp), read when the file is newer than the process.
//    Action ids resolve through a [Controls] section in the mod's ini, which the test runner writes for the run.
// A proxy also refuses DllGetClassObject while fenced. Header-only; x86 and x64.
#pragma once
#include "dbce_fence.hpp"
#include "../controls/dbce_inject.hpp"
#include "../controls/dbce_inject_table.hpp"
#include <string>
#include <vector>

namespace dbce { namespace testing {

inline bool switchFile(const char *game, const char *name)
{
    char path[MAX_PATH];
    DWORD n = GetEnvironmentVariableA("LOCALAPPDATA", path, MAX_PATH);
    if (!n || n > MAX_PATH - 80) return false;
    strcat_s(path, "\\dbce\\");
    strcat_s(path, game);
    strcat_s(path, "\\");
    strcat_s(path, name);
    return GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES;
}

typedef HRESULT(WINAPI *DI8CreateFn)(HINSTANCE, DWORD, REFIID, LPVOID *, LPUNKNOWN);

// DirectInput8Create while fenced: the real interface (A or W) wrapped. Refused unpinned, aggregated or for another IID.
inline HRESULT CreateFencedDirectInput(DI8CreateFn create, HINSTANCE h, DWORD ver, REFIID riid, LPVOID *out, LPUNKNOWN unk, bool pinned)
{
    if (!out) return E_POINTER;
    *out = nullptr;
    const bool w = IsEqualGUID(riid, IID_IDirectInput8W) != 0;
    if (!create || !pinned || unk || (!w && !IsEqualGUID(riid, IID_IDirectInput8A))) {
        fence::logf("ffb fence: DirectInput8Create refused (fenced, and %s)", !pinned ? "the proxy is not pinned" : unk ? "aggregated" : !create ? "no system DirectInput" : "another interface");
        return E_FAIL;
    }
    void *real = nullptr;
    HRESULT hr = create(h, ver, riid, &real, nullptr);
    if (FAILED(hr) || !real) return FAILED(hr) ? hr : E_FAIL;
    void *fenced = w ? (void *)static_cast<IDirectInput8W *>(new (std::nothrow) fence::FencedDirectInput<IDirectInput8W>((IDirectInput8W *)real))
                     : (void *)static_cast<IDirectInput8A *>(new (std::nothrow) fence::FencedDirectInput<IDirectInput8A>((IDirectInput8A *)real));
    if (!fenced) { ((IUnknown *)real)->Release(); return E_OUTOFMEMORY; }
    *out = fenced;
    static volatile LONG n;
    if (InterlockedIncrement(&n) <= 3) fence::logf("ffb fence: DirectInput8Create (%s) gave the game the fenced interface (devices visible, no force can reach one)", w ? "W" : "A");
    return hr;
}

// The inject.txt reader thread. The [Controls] section (if any) is read once from `ini` for "inject action".
struct InjectReader {
    std::string path, ini;
    dbce::controls::Section controls;
    bool haveControls = false;
    dbce::controls::InjectionTable *table = nullptr;
};

inline DWORD WINAPI injectReaderThread(LPVOID p)
{
    auto *r = (InjectReader *)p;
    FILETIME created, x1, x2, x3, last{};
    GetProcessTimes(GetCurrentProcess(), &created, &x1, &x2, &x3);
    fence::logf("inject: reading %s (commands newer than this process)%s", r->path.c_str(), r->haveControls ? "; action ids from [Controls]" : "; no [Controls]: raw commands only");
    for (;;) {
        Sleep(40);
        WIN32_FILE_ATTRIBUTE_DATA a;
        if (!GetFileAttributesExA(r->path.c_str(), GetFileExInfoStandard, &a)) continue;
        if (CompareFileTime(&a.ftLastWriteTime, &created) <= 0 || CompareFileTime(&a.ftLastWriteTime, &last) == 0) continue;
        last = a.ftLastWriteTime;
        HANDLE f = CreateFileA(r->path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
        if (f == INVALID_HANDLE_VALUE) continue;
        char buf[4096];
        DWORD got = 0;
        BOOL ok = ReadFile(f, buf, sizeof buf - 1, &got, nullptr);
        CloseHandle(f);
        if (!ok) continue;
        buf[got] = 0;
        std::string text(buf);
        size_t p0 = 0;
        while (p0 <= text.size()) {
            size_t q = text.find('\n', p0);
            std::string line = dbce::controls::trim(text.substr(p0, q == std::string::npos ? std::string::npos : q - p0));
            p0 = q == std::string::npos ? text.size() + 1 : q + 1;
            if (line.empty() || line[0] == '#') continue;
            dbce::controls::InjectCommand c;
            std::string why;
            if (!dbce::controls::parseInject(line, r->haveControls ? &r->controls : nullptr, c, why)) { fence::logf("inject: refused \"%s\": %s", line.c_str(), why.c_str()); continue; }
            why = r->table->add(c, GetTickCount64());
            if (!why.empty()) { fence::logf("inject: refused \"%s\": %s", line.c_str(), why.c_str()); continue; }
            fence::logf("inject: \"%s\" -> %s %d sample %ld on %s for %d ms", line.c_str(), dbce::controls::kindName(c.binding.kind), c.binding.index, c.raw, c.binding.dev.c_str(), c.ms);
        }
    }
}

// Starts the reader: <game> names the %LOCALAPPDATA%\dbce folder; `ini` is the mod's settings file.
inline bool StartInjectReader(const char *game, const char *ini, dbce::controls::InjectionTable *table)
{
    char dir[MAX_PATH];
    DWORD n = GetEnvironmentVariableA("LOCALAPPDATA", dir, MAX_PATH);
    if (!n || n > MAX_PATH - 80 || !table) return false;
    auto *r = new (std::nothrow) InjectReader;
    if (!r) return false;
    r->path = std::string(dir) + "\\dbce\\" + game + "\\inject.txt";
    r->ini = ini ? ini : "";
    r->table = table;
    if (!r->ini.empty()) {
        static char sec[32768];
        DWORD k = GetPrivateProfileSectionA("Controls", sec, sizeof sec, r->ini.c_str());
        std::vector<std::string> lines;
        for (const char *p = sec; k && *p; p += strlen(p) + 1) lines.push_back(p);
        if (!lines.empty()) {
            r->controls = dbce::controls::parseSection(lines);
            r->haveControls = r->controls.error.empty();
            if (!r->haveControls) fence::logf("inject: [Controls] in %s not used: %s", r->ini.c_str(), r->controls.error.c_str());
        }
    }
    HANDLE th = CreateThread(nullptr, 0, injectReaderThread, r, 0, nullptr);
    if (!th) { delete r; return false; }
    CloseHandle(th);
    return true;
}

}} // namespace dbce::testing
