// The part of a dinput8.dll proxy mod that is the same in every game (C++17, header-only, x86 and x64).
//
// A proxy mod is a DLL named dinput8.dll beside the game's exe. Windows loads it instead of the system one. It forwards
// every export to the system DLL and, in between, hooks what the mod needs. This header owns the parts every game
// used to carry its own copy of:
//   - start-up once, on the first export call (never in DllMain): the log, the mod's ini path, the test switches, the
//     system dinput8.dll and the module pin;
//   - the log: %LOCALAPPDATA%\dbce\<id>\dbce-<id>.log;
//   - vtable hooks with one original per (hook, vtable), and a stub for a vtable nobody recorded;
//   - the unattended-test switches (STD-033): dbce-<id>-mute.txt beside the exe, and ffb-fence.on / inject.on in the log
//     folder (dbce_test_switches.hpp);
//   - DirectInput8Create with those switches honoured, and, when asked, a direct-drive wheel base presented as a driving
//     wheel in the three places a game reads the device type;
//   - the five other exports.
// The game's own file keeps only what is the game's: its identity, what to start, and its own hooks.
//
//   #include "vendor/dinput-proxy/dbce_proxy_core.hpp"
//   static void started() { /* dbce::ffb::Init, the F6 panel, the game's own hooks */ }
//   static const dbce::proxy::Identity kMod = {"mygame", "My Game mod", "0.1.0"};
//   extern "C" HRESULT WINAPI DirectInput8Create(HINSTANCE h, DWORD v, REFIID riid, LPVOID *out, LPUNKNOWN unk) {
//       dbce::proxy::start(kMod, started);
//       dbce::proxy::CreateOptions o; o.presentAsWheel = true; o.hookDirectInput = dbce::ffb::HookDirectInput;
//       return dbce::proxy::create(h, v, riid, out, unk, o);
//   }
//   DBCE_PROXY_FORWARD_EXPORTS(kMod, started)     // the other five exports and DllMain
//
// Rules this code keeps, each paid for once already:
//   - nothing heavy under the loader lock: all work happens on the first export call, inside INIT_ONCE;
//   - the module is pinned before any hook is installed, because hooks point into it for the rest of the process;
//     without the pin the proxy is a plain pass-through, and a requested mute or fence refuses DirectInput instead;
//   - a vtable is shared by every object of its class, so a hook can be reached through a vtable it never patched:
//     that call gets E_FAIL, never another vtable's original;
//   - nothing at process detach.
#pragma once
#ifndef DIRECTINPUT_VERSION
#define DIRECTINPUT_VERSION 0x0800
#endif
#include <windows.h>
#include <dinput.h>
#include <stdarg.h>
#include <stdio.h>
#include <share.h>
#include <string.h>
#include "dbce_test_switches.hpp"

namespace dbce { namespace proxy {

struct Identity {
    const char *id;        // short lower-case name: the log folder, and the stem of the log, ini and mute file names
    const char *name;      // for the first log line, such as "Rush Rally mod"
    const char *version;
};

struct State {
    Identity identity{};
    HMODULE real = nullptr;             // the system dinput8.dll
    FILE *log = nullptr;
    bool pinned = false;                // hooks may be installed
    bool mute = false;                  // dbce-<id>-mute.txt beside the exe: no force effect ever starts
    bool fence = false;                 // ffb-fence.on: the game gets the closed wrapper; no force can reach a device
    bool inject = false;                // inject.on with the fence: test input from inject.txt
    char exeName[MAX_PATH] = "";        // "Game.exe"
    char exeDir[MAX_PATH] = "";         // with the trailing backslash
    char ini[MAX_PATH] = "";            // <exeDir>dbce-<id>.ini
    char logPath[MAX_PATH] = "";
    dbce::controls::InjectionTable injections;
};
inline State g_state;
inline CRITICAL_SECTION g_lock;
inline INIT_ONCE g_once = INIT_ONCE_STATIC_INIT;
inline void (*g_started)() = nullptr;
inline const State &state() { return g_state; }

// One line to the mod's log, time-stamped and flushed. Safe from any thread once start() has run; a no-op before.
inline void log(const char *fmt, ...)
{
    if (!g_state.log) return;
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    _vsnprintf_s(buf, sizeof buf, _TRUNCATE, fmt, ap);
    va_end(ap);
    SYSTEMTIME t;
    GetLocalTime(&t);
    EnterCriticalSection(&g_lock);
    fprintf(g_state.log, "%04d-%02d-%02d %02d:%02d:%02d %s\n", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond, buf);
    fflush(g_state.log);
    LeaveCriticalSection(&g_lock);
}
inline void logLine(const char *line) { log("%s", line); }   // for the panel's and the force layer's log callbacks

// ---- vtable hooks -------------------------------------------------------------------------------------------------
// A hook is a small integer the caller chooses; the core uses 0..7 for the wheel presentation, a game starts at
// kFirstGameHook. Each hook can be installed on up to four different vtables (A and W interfaces, device classes).
constexpr int kFirstGameHook = 8;
constexpr int kMaxHooks = 40;
struct Slot { void **vt; void *orig; };
inline Slot g_slots[kMaxHooks][4];
inline volatile LONG g_unrecorded;
// Every hooked COM method returns HRESULT; on x64 the caller cleans up its own arguments, so one stub fits them all.
// On x86 (stdcall) the stub must not be called with arguments, so an unrecorded vtable there is logged and the caller
// gets the stub only for methods without arguments; orig() returns null for the rest and the hook must check it.
inline HRESULT STDMETHODCALLTYPE unrecordedVtable() { return E_FAIL; }

// The original method for `self`, or the E_FAIL stub when this hook never patched that object's vtable.
inline void *orig(int hook, void *self)
{
    void **vt = *(void ***)self;
    void *o = nullptr;
    EnterCriticalSection(&g_lock);
    for (auto &s : g_slots[hook]) if (s.vt == vt) { o = s.orig; break; }
    LeaveCriticalSection(&g_lock);
    if (o) return o;
    if (InterlockedIncrement(&g_unrecorded) <= 5) log("hook %d reached through an unrecorded vtable %p; returned E_FAIL", hook, (void *)vt);
#ifdef _WIN64
    return (void *)&unrecordedVtable;
#else
    return nullptr;
#endif
}

// Points vtable entry `index` of `obj` at `fn`, remembering the original for orig(). Does nothing without the pin, for
// a vtable already patched, or when the four slots of this hook are taken.
inline void patch(int hook, void *obj, int index, void *fn)
{
    if (!g_state.pinned || !obj || hook < 0 || hook >= kMaxHooks) return;
    void **vt = *(void ***)obj;
    EnterCriticalSection(&g_lock);
    if (vt[index] == fn) { LeaveCriticalSection(&g_lock); return; }
    for (auto &s : g_slots[hook]) {
        if (s.vt == vt) break;
        if (!s.vt) {
            DWORD old;
            if (VirtualProtect(&vt[index], sizeof(void *), PAGE_EXECUTE_READWRITE, &old)) {
                s.vt = vt;
                s.orig = vt[index];
                vt[index] = fn;
                VirtualProtect(&vt[index], sizeof(void *), old, &old);
            }
            break;
        }
    }
    LeaveCriticalSection(&g_lock);
}

// ---- start-up -----------------------------------------------------------------------------------------------------
inline BOOL CALLBACK initOnce(PINIT_ONCE, PVOID, PVOID *)
{
    State &s = g_state;
    InitializeCriticalSection(&g_lock);
    char dir[MAX_PATH];
    const DWORD n = GetEnvironmentVariableA("LOCALAPPDATA", dir, MAX_PATH);
    if (n && n < MAX_PATH - 100 && strlen(s.identity.id) < 32) {
        strcat_s(dir, "\\dbce");
        CreateDirectoryA(dir, nullptr);
        strcat_s(dir, "\\");
        strcat_s(dir, s.identity.id);
        CreateDirectoryA(dir, nullptr);
        sprintf_s(s.logPath, "%s\\dbce-%s.log", dir, s.identity.id);
        s.log = _fsopen(s.logPath, "a", _SH_DENYWR);
    }
    char path[MAX_PATH];
    GetModuleFileNameA(nullptr, path, MAX_PATH);
    const char *exe = strrchr(path, '\\') ? strrchr(path, '\\') + 1 : path;
    strcpy_s(s.exeName, exe);
    if (char *slash = strrchr(path, '\\')) {
        slash[1] = 0;
        strcpy_s(s.exeDir, path);
        sprintf_s(s.ini, "%sdbce-%s.ini", s.exeDir, s.identity.id);
        char mute[MAX_PATH];
        sprintf_s(mute, "%sdbce-%s-mute.txt", s.exeDir, s.identity.id);
        s.mute = GetFileAttributesA(mute) != INVALID_FILE_ATTRIBUTES;
    }
    s.fence = dbce::testing::switchFile(s.identity.id, "ffb-fence.on");
    s.inject = s.fence && dbce::testing::switchFile(s.identity.id, "inject.on");
    if (s.inject) dbce::fence::settings().injections = &s.injections;
    dbce::fence::settings().log = logLine;
    char sys[MAX_PATH];
    GetSystemDirectoryA(sys, MAX_PATH);
    strcat_s(sys, "\\dinput8.dll");   // by full path: "dinput8.dll" by name would find this proxy
    s.real = LoadLibraryA(sys);
    log("dbce %s %s in %s, pid %lu, system dinput8 %p%s%s", s.identity.name, s.identity.version, s.exeName, GetCurrentProcessId(), (void *)s.real,
        s.mute ? ", force feedback MUTED (test switch present)" : "",
        s.fence ? (s.inject ? ", force feedback FENCED (ffb-fence.on): devices visible, no force can reach one; test injection allowed (inject.on)"
                            : ", force feedback FENCED (ffb-fence.on): devices visible, no force can reach one") : "");
    HMODULE self;
    s.pinned = GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN, (LPCWSTR)&initOnce, &self) != 0;
    if (!s.pinned) {
        log(s.mute || s.fence ? "could not pin the proxy (%lu): DirectInput refused (the test switch cannot be honoured)"
                              : "could not pin the proxy (%lu): plain pass-through, no hooks", GetLastError());
        return TRUE;
    }
    if (g_started) g_started();
    if (s.inject) dbce::testing::StartInjectReader(s.identity.id, s.ini, &s.injections);
    return TRUE;
}

// Call first in every export. Runs the start-up once; a concurrent first call waits until it has finished. `started`
// runs once, after the pin, on the thread of the first export call: start the force layer, the panel and the game's
// own workers there.
inline void start(const Identity &identity, void (*started)())
{
    if (!g_state.identity.id) { g_state.identity = identity; g_started = started; }
    InitOnceExecuteOnce(&g_once, initOnce, nullptr, nullptr);
}

// ---- a direct-drive base presented as a driving wheel ---------------------------------------------------------------
// DirectInput classifies a HID joystick with six or more axes as DI8DEVTYPE_1STPERSON, so a direct-drive base is never
// DI8DEVTYPE_DRIVING, and a game that asks "is this a wheel" by type offers it as neither wheel nor pad. The type is
// rewritten in the three places a game reads it: EnumDevices, GetDeviceInfo and GetCapabilities. Nothing else changes.
enum { H_CREATEDEV_W, H_CREATEDEV_A, H_ENUM_W, H_ENUM_A, H_GETCAPS, H_INFO_W, H_INFO_A };
inline volatile LONG g_retyped;
inline bool retype(DWORD &t)
{
    if ((t & 0xFF) != DI8DEVTYPE_1STPERSON) return false;
    t = (t & 0xFFFF0000) | DI8DEVTYPE_DRIVING | (DI8DEVTYPEDRIVING_THREEPEDALS << 8);
    return true;
}
namespace detail {
template <typename Fn> inline Fn original(int hook, void *self) { return (Fn)orig(hook, self); }
typedef HRESULT(STDMETHODCALLTYPE *GetCapsFn)(void *, LPDIDEVCAPS);
typedef HRESULT(STDMETHODCALLTYPE *InfoWFn)(void *, LPDIDEVICEINSTANCEW);
typedef HRESULT(STDMETHODCALLTYPE *InfoAFn)(void *, LPDIDEVICEINSTANCEA);
typedef HRESULT(STDMETHODCALLTYPE *CreateDevFn)(void *, REFGUID, void **, LPUNKNOWN);
typedef HRESULT(STDMETHODCALLTYPE *EnumWFn)(void *, DWORD, LPDIENUMDEVICESCALLBACKW, LPVOID, DWORD);
typedef HRESULT(STDMETHODCALLTYPE *EnumAFn)(void *, DWORD, LPDIENUMDEVICESCALLBACKA, LPVOID, DWORD);
inline HRESULT STDMETHODCALLTYPE hkGetCaps(void *self, LPDIDEVCAPS c)
{
    auto f = original<GetCapsFn>(H_GETCAPS, self);
    HRESULT hr = f ? f(self, c) : E_FAIL;
    if (SUCCEEDED(hr) && c) retype(c->dwDevType);
    return hr;
}
inline HRESULT STDMETHODCALLTYPE hkInfoW(void *self, LPDIDEVICEINSTANCEW d)
{
    auto f = original<InfoWFn>(H_INFO_W, self);
    HRESULT hr = f ? f(self, d) : E_FAIL;
    if (SUCCEEDED(hr) && d) retype(d->dwDevType);
    return hr;
}
inline HRESULT STDMETHODCALLTYPE hkInfoA(void *self, LPDIDEVICEINSTANCEA d)
{
    auto f = original<InfoAFn>(H_INFO_A, self);
    HRESULT hr = f ? f(self, d) : E_FAIL;
    if (SUCCEEDED(hr) && d) retype(d->dwDevType);
    return hr;
}
inline void hookDevice(void *dev, bool wide)
{
    patch(H_GETCAPS, dev, 3, (void *)&hkGetCaps);
    if (wide) patch(H_INFO_W, dev, 15, (void *)&hkInfoW);
    else patch(H_INFO_A, dev, 15, (void *)&hkInfoA);
}
inline HRESULT STDMETHODCALLTYPE hkCreateDevW(void *self, REFGUID g, void **out, LPUNKNOWN unk)
{
    auto f = original<CreateDevFn>(H_CREATEDEV_W, self);
    HRESULT hr = f ? f(self, g, out, unk) : E_FAIL;
    if (SUCCEEDED(hr) && out && *out) hookDevice(*out, true);
    return hr;
}
inline HRESULT STDMETHODCALLTYPE hkCreateDevA(void *self, REFGUID g, void **out, LPUNKNOWN unk)
{
    auto f = original<CreateDevFn>(H_CREATEDEV_A, self);
    HRESULT hr = f ? f(self, g, out, unk) : E_FAIL;
    if (SUCCEEDED(hr) && out && *out) hookDevice(*out, false);
    return hr;
}
struct CtxW { LPDIENUMDEVICESCALLBACKW cb; LPVOID ref; };
struct CtxA { LPDIENUMDEVICESCALLBACKA cb; LPVOID ref; };
inline BOOL CALLBACK enumW(LPCDIDEVICEINSTANCEW d, LPVOID ref)
{
    auto c = (CtxW *)ref;
    DIDEVICEINSTANCEW copy = *d;
    if (retype(copy.dwDevType) && InterlockedIncrement(&g_retyped) <= 4) log("presenting \"%ls\" as a driving wheel", d->tszProductName);
    return c->cb(&copy, c->ref);
}
inline BOOL CALLBACK enumA(LPCDIDEVICEINSTANCEA d, LPVOID ref)
{
    auto c = (CtxA *)ref;
    DIDEVICEINSTANCEA copy = *d;
    if (retype(copy.dwDevType) && InterlockedIncrement(&g_retyped) <= 4) log("presenting \"%s\" as a driving wheel", d->tszProductName);
    return c->cb(&copy, c->ref);
}
inline HRESULT STDMETHODCALLTYPE hkEnumW(void *self, DWORD type, LPDIENUMDEVICESCALLBACKW cb, LPVOID ref, DWORD flags)
{
    auto f = original<EnumWFn>(H_ENUM_W, self);
    if (!f) return E_FAIL;
    if (!cb) return f(self, type, cb, ref, flags);
    CtxW c = {cb, ref};
    return f(self, type, enumW, &c, flags);
}
inline HRESULT STDMETHODCALLTYPE hkEnumA(void *self, DWORD type, LPDIENUMDEVICESCALLBACKA cb, LPVOID ref, DWORD flags)
{
    auto f = original<EnumAFn>(H_ENUM_A, self);
    if (!f) return E_FAIL;
    if (!cb) return f(self, type, cb, ref, flags);
    CtxA c = {cb, ref};
    return f(self, type, enumA, &c, flags);
}
inline void presentAsWheel(void *directInput, bool wide)
{
    if (wide) { patch(H_CREATEDEV_W, directInput, 3, (void *)&hkCreateDevW); patch(H_ENUM_W, directInput, 4, (void *)&hkEnumW); }
    else { patch(H_CREATEDEV_A, directInput, 3, (void *)&hkCreateDevA); patch(H_ENUM_A, directInput, 4, (void *)&hkEnumA); }
}
inline bool g_presentInFence;
inline HRESULT WINAPI realCreate(HINSTANCE h, DWORD ver, REFIID riid, LPVOID *out, LPUNKNOWN unk)
{
    auto f = g_state.real ? (dbce::testing::DI8CreateFn)GetProcAddress(g_state.real, "DirectInput8Create") : nullptr;
    HRESULT hr = f ? f(h, ver, riid, out, unk) : E_FAIL;
    // Under the fence the wheel presentation stays on the real interface; the wrapper forwards through it.
    if (SUCCEEDED(hr) && out && *out && g_presentInFence) presentAsWheel(*out, IsEqualGUID(riid, IID_IDirectInput8W) != 0);
    return hr;
}
} // namespace detail

// ---- the exports ----------------------------------------------------------------------------------------------------
struct CreateOptions {
    bool presentAsWheel = false;                         // show a 1st-person (direct-drive) device as a driving wheel
    void (*hookDirectInput)(void *directInput8) = nullptr;   // such as dbce::ffb::HookDirectInput; not called under the fence
};

// DirectInput8Create. Call start() first.
inline HRESULT create(HINSTANCE h, DWORD ver, REFIID riid, LPVOID *out, LPUNKNOWN unk, const CreateOptions &options)
{
    const State &s = g_state;
    // The mute switch is a test guarantee: without the pin its hooks cannot be installed, so refuse rather than hand out
    // an unguarded DirectInput that could start forces.
    if (s.mute && !s.pinned) {
        if (out) *out = nullptr;
        log("DirectInput8Create refused: force feedback mute requested but the proxy could not be pinned");
        return E_FAIL;
    }
    if (s.fence) {   // the closed wrapper: the game never holds a real DirectInput or device object
        detail::g_presentInFence = options.presentAsWheel;
        return dbce::testing::CreateFencedDirectInput(detail::realCreate, h, ver, riid, out, unk, s.pinned);
    }
    auto f = s.real ? (dbce::testing::DI8CreateFn)GetProcAddress(s.real, "DirectInput8Create") : nullptr;
    HRESULT hr = f ? f(h, ver, riid, out, unk) : E_FAIL;
    if (SUCCEEDED(hr) && out && *out && !unk && s.pinned) {
        if (options.presentAsWheel) detail::presentAsWheel(*out, IsEqualGUID(riid, IID_IDirectInput8W) != 0);
        if (options.hookDirectInput) options.hookDirectInput(*out);   // after the presentation, so it wraps CreateDevice last
    }
    return hr;
}

// DllGetClassObject: the class-factory route to DirectInput gets the same guard as DirectInput8Create.
inline HRESULT getClassObject(REFCLSID c, REFIID i, LPVOID *o)
{
    const State &s = g_state;
    if ((s.mute && !s.pinned) || s.fence) {
        if (o) *o = nullptr;
        log(s.fence ? "DllGetClassObject refused: fenced (the class factory would hand out unfenced DirectInput)"
                    : "DllGetClassObject refused: force feedback mute requested but the proxy could not be pinned");
        return CLASS_E_CLASSNOTAVAILABLE;
    }
    auto f = (HRESULT(WINAPI *)(REFCLSID, REFIID, LPVOID *))(s.real ? GetProcAddress(s.real, "DllGetClassObject") : nullptr);
    return f ? f(c, i, o) : CLASS_E_CLASSNOTAVAILABLE;
}

template <typename Ret> inline Ret forward(const char *name, Ret fail)
{
    auto f = (Ret(WINAPI *)(void))(g_state.real ? GetProcAddress(g_state.real, name) : nullptr);
    return f ? f() : fail;
}

}} // namespace dbce::proxy

// The five other exports and DllMain, for the mod's one .cpp file. The .def file lists the same six names:
//   DirectInput8Create, DllCanUnloadNow PRIVATE, DllGetClassObject PRIVATE, DllRegisterServer PRIVATE,
//   DllUnregisterServer PRIVATE, GetdfDIJoystick.
#define DBCE_PROXY_FORWARD_EXPORTS(identity, started)                                                                          \
    extern "C" HRESULT WINAPI DllCanUnloadNow(void) { dbce::proxy::start(identity, started); return dbce::proxy::forward<HRESULT>("DllCanUnloadNow", S_FALSE); } \
    extern "C" HRESULT WINAPI DllGetClassObject(REFCLSID c, REFIID i, LPVOID *o) { dbce::proxy::start(identity, started); return dbce::proxy::getClassObject(c, i, o); } \
    extern "C" HRESULT WINAPI DllRegisterServer(void) { dbce::proxy::start(identity, started); return dbce::proxy::forward<HRESULT>("DllRegisterServer", E_FAIL); } \
    extern "C" HRESULT WINAPI DllUnregisterServer(void) { dbce::proxy::start(identity, started); return dbce::proxy::forward<HRESULT>("DllUnregisterServer", E_FAIL); } \
    extern "C" LPCDIDATAFORMAT WINAPI GetdfDIJoystick(void) { dbce::proxy::start(identity, started); return dbce::proxy::forward<LPCDIDATAFORMAT>("GetdfDIJoystick", nullptr); } \
    /* Nothing at detach: no lock, log or CRT flush under the loader lock (each log line is already flushed). */              \
    BOOL WINAPI DllMain(HINSTANCE self, DWORD reason, LPVOID) { if (reason == DLL_PROCESS_ATTACH) DisableThreadLibraryCalls(self); return TRUE; }
