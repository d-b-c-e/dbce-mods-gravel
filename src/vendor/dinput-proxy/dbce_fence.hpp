// The force fence for unattended control tests (STD-033): the game sees every device, including the wheel, and reads
// it normally, but no force can reach a device. Header-only, for IDirectInput8A and W (x64 and x86).
//
// The game never holds a real DirectInput or device object. FencedDirectInput wraps the interface, and every device it
// creates comes back as a FencedDevice:
//  - forwarded unchanged: capabilities, objects, the readable properties, acquire, state, data, formats, cooperative
//    level, object and device information, effect enumeration and information, poll, action maps;
//  - never forwarded: CreateEffect returns a software sink effect that keeps the game's parameters and never touches the
//    device (Start, Stop, SetParameters, Download, Unload, Escape all stay in the sink); SendForceFeedbackCommand,
//    Escape, SendDeviceData, effect files, RunControlPanel and Initialize; the force gain property;
//  - auto-centring is forwarded only when the game turns it OFF (less force), never ON;
//  - other writable properties are refused unless whitelisted (range, dead zone, saturation, buffer size, axis mode),
//    so calibration and device names in the registry cannot change;
//  - QueryInterface hands out only the wrapper; aggregation is refused; EnumDevicesBySemantics (which would give the
//    game DirectInput's own device objects) answers none; ConfigureDevices is refused.
// A proxy using this must also refuse DllGetClassObject while fenced. COM creation through the registry loads the system
// dinput8.dll and bypasses any proxy, so it is only safe for games that import DirectInput8Create (check each game).
//
// Test injection (dbce_inject_table.hpp) is delivered in FencedDevice::GetDeviceState, keyed by the instance GUID that
// DirectInput reports for the created device. Buffered GetDeviceData reads are forwarded without injection.
// Pedal rest shaping (dbce_shape_state.hpp), when a proxy sets one, runs in the same place after injection. An observer
// (Settings::observe), when a proxy sets one, then sees the final standard-format state: what the game receives.
#pragma once
#ifndef DIRECTINPUT_VERSION
#define DIRECTINPUT_VERSION 0x0800
#endif
#include <windows.h>
#include <dinput.h>
#include <stdarg.h>
#include <stdio.h>
#include <new>
#include <string>
#include <vector>
#include "../controls/dbce_inject_table.hpp"
#include "dbce_joyformat.hpp"
#include "dbce_shape_state.hpp"

#pragma comment(lib, "dxguid.lib")

namespace dbce { namespace fence {

// Process-wide settings, set once before DirectInput8Create is answered.
struct Settings {
    void (*log)(const char *line) = nullptr;
    dbce::controls::InjectionTable *injections = nullptr;   // nullptr: no injection
    const dbce::controls::ShapeTable *shapes = nullptr;     // nullptr: no pedal shaping (set once, complete, never freed)
    // What the game receives, for a proxy that observes its own reads (Gravel's telemetry, sana 107988): called on the
    // reading thread after a successful GetDeviceState in a standard format (format 1 with a DIJOYSTATE, 2 with a
    // DIJOYSTATE2), once injection and shaping are applied. `device` is the wrapped device, for identity only; `state` is
    // borrowed and read-only for the call. Never called for a failed, null, custom-format or mis-sized read. nullptr: none.
    void (*observe)(const void *device, int format, DWORD cb, const void *state) = nullptr;
};
inline Settings &settings() { static Settings s; return s; }

inline void logf(const char *fmt, ...)
{
    if (!settings().log) return;
    char b[600];
    va_list ap;
    va_start(ap, fmt);
    _vsnprintf_s(b, sizeof b, _TRUNCATE, fmt, ap);
    va_end(ap);
    settings().log(b);
}
// Bounded per call site: the first 20, then every 1000th.
inline bool sample(volatile LONG &n) { LONG k = InterlockedIncrement(&n); return k <= 20 || k % 1000 == 0; }

inline std::string guidText(const GUID &g)
{
    char b[40];
    sprintf_s(b, "{%08lx-%04hx-%04hx-%02x%02x-%02x%02x%02x%02x%02x%02x}", g.Data1, g.Data2, g.Data3, g.Data4[0], g.Data4[1],
              g.Data4[2], g.Data4[3], g.Data4[4], g.Data4[5], g.Data4[6], g.Data4[7]);
    return b;
}

// ---- the sink effect: the game's effect object, kept entirely in software -------------------------------------------
struct SinkEffect final : IDirectInputEffect {
    volatile LONG refs = 1;
    GUID type;
    DIEFFECT kept{};   // the scalar fields the game last set (no pointers kept)
    explicit SinkEffect(REFGUID g) : type(g) { kept.dwSize = sizeof kept; }
    void keep(LPCDIEFFECT e, DWORD flags)
    {
        if (!e) return;
        if (flags & DIEP_DURATION) kept.dwDuration = e->dwDuration;
        if (flags & DIEP_SAMPLEPERIOD) kept.dwSamplePeriod = e->dwSamplePeriod;
        if (flags & DIEP_GAIN) kept.dwGain = e->dwGain;
        if (flags & DIEP_TRIGGERBUTTON) kept.dwTriggerButton = e->dwTriggerButton;
        if (flags & DIEP_TRIGGERREPEATINTERVAL) kept.dwTriggerRepeatInterval = e->dwTriggerRepeatInterval;
        if ((flags & DIEP_STARTDELAY) && e->dwSize >= sizeof(DIEFFECT)) kept.dwStartDelay = e->dwStartDelay;
        kept.dwFlags = e->dwFlags;
    }
    STDMETHODIMP QueryInterface(REFIID iid, LPVOID *out) override
    {
        if (!out) return E_POINTER;
        if (IsEqualGUID(iid, IID_IUnknown) || IsEqualGUID(iid, IID_IDirectInputEffect)) { AddRef(); *out = this; return S_OK; }
        *out = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return (ULONG)InterlockedIncrement(&refs); }
    STDMETHODIMP_(ULONG) Release() override { LONG n = InterlockedDecrement(&refs); if (n == 0) delete this; return (ULONG)n; }
    STDMETHODIMP Initialize(HINSTANCE, DWORD, REFGUID) override { return DI_OK; }
    STDMETHODIMP GetEffectGuid(LPGUID g) override { if (!g) return E_POINTER; *g = type; return DI_OK; }
    STDMETHODIMP GetParameters(LPDIEFFECT e, DWORD flags) override
    {
        if (!e) return E_POINTER;
        if (flags & DIEP_DURATION) e->dwDuration = kept.dwDuration;
        if (flags & DIEP_SAMPLEPERIOD) e->dwSamplePeriod = kept.dwSamplePeriod;
        if (flags & DIEP_GAIN) e->dwGain = kept.dwGain;
        if (flags & DIEP_TRIGGERBUTTON) e->dwTriggerButton = kept.dwTriggerButton;
        if (flags & DIEP_TRIGGERREPEATINTERVAL) e->dwTriggerRepeatInterval = kept.dwTriggerRepeatInterval;
        if ((flags & DIEP_STARTDELAY) && e->dwSize >= sizeof(DIEFFECT)) e->dwStartDelay = kept.dwStartDelay;
        return DI_OK;
    }
    STDMETHODIMP SetParameters(LPCDIEFFECT e, DWORD flags) override
    {
        static volatile LONG n;
        keep(e, flags);
        if (sample(n)) logf("fence: effect SetParameters flags=0x%lx kept in software, not sent to the device", flags);
        return DI_OK;
    }
    STDMETHODIMP Start(DWORD iterations, DWORD) override
    {
        static volatile LONG n;
        if (sample(n)) logf("fence: effect Start (%lu) not sent to the device", iterations);
        return DI_OK;
    }
    STDMETHODIMP Stop() override { return DI_OK; }
    STDMETHODIMP GetEffectStatus(LPDWORD s) override { if (!s) return E_POINTER; *s = 0; return DI_OK; }   // not playing
    STDMETHODIMP Download() override { return DI_OK; }
    STDMETHODIMP Unload() override { return DI_OK; }
    STDMETHODIMP Escape(LPDIEFFESCAPE) override { logf("fence: effect Escape refused"); return DIERR_UNSUPPORTED; }
};

// ---- the device wrapper ------------------------------------------------------------------------------------------
template <class I> struct DeviceTraits;
template <> struct DeviceTraits<IDirectInputDevice8A> {
    using ObjCb = LPDIENUMDEVICEOBJECTSCALLBACKA; using ObjInst = LPDIDEVICEOBJECTINSTANCEA; using DevInst = LPDIDEVICEINSTANCEA;
    using DevInstT = DIDEVICEINSTANCEA; using EffCb = LPDIENUMEFFECTSCALLBACKA; using EffInfo = LPDIEFFECTINFOA; using Str = LPCSTR;
    using ActFmt = LPDIACTIONFORMATA; using Image = LPDIDEVICEIMAGEINFOHEADERA;
    static const IID &iid() { return IID_IDirectInputDevice8A; }
};
template <> struct DeviceTraits<IDirectInputDevice8W> {
    using ObjCb = LPDIENUMDEVICEOBJECTSCALLBACKW; using ObjInst = LPDIDEVICEOBJECTINSTANCEW; using DevInst = LPDIDEVICEINSTANCEW;
    using DevInstT = DIDEVICEINSTANCEW; using EffCb = LPDIENUMEFFECTSCALLBACKW; using EffInfo = LPDIEFFECTINFOW; using Str = LPCWSTR;
    using ActFmt = LPDIACTIONFORMATW; using Image = LPDIDEVICEIMAGEINFOHEADERW;
    static const IID &iid() { return IID_IDirectInputDevice8W; }
};

template <class I>
struct FencedDevice final : I {
    using T = DeviceTraits<I>;
    I *real;
    volatile LONG refs = 1;
    std::string instance;   // "{...}" lowercase, from GetDeviceInfo
    volatile LONG format = 0;   // 1 c_dfDIJoystick, 2 c_dfDIJoystick2, 0 any other (injection is delivered only into 1 and 2)
    bool formatWarned = false;
    FencedDevice(I *r, const std::string &inst) : real(r), instance(inst) {}
    static HRESULT refuse(const char *what) { static volatile LONG n; if (sample(n)) logf("fence: %s refused", what); return DIERR_UNSUPPORTED; }

    STDMETHODIMP QueryInterface(REFIID iid, LPVOID *out) override
    {
        if (!out) return E_POINTER;
        if (IsEqualGUID(iid, IID_IUnknown) || IsEqualGUID(iid, T::iid())) { AddRef(); *out = this; return S_OK; }
        *out = nullptr;
        refuse("device QueryInterface for another interface");
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return (ULONG)InterlockedIncrement(&refs); }
    STDMETHODIMP_(ULONG) Release() override { LONG n = InterlockedDecrement(&refs); if (n == 0) { real->Release(); delete this; } return (ULONG)n; }
    STDMETHODIMP GetCapabilities(LPDIDEVCAPS c) override { return real->GetCapabilities(c); }
    STDMETHODIMP EnumObjects(typename T::ObjCb cb, LPVOID ref, DWORD flags) override { return real->EnumObjects(cb, ref, flags); }
    STDMETHODIMP GetProperty(REFGUID p, LPDIPROPHEADER h) override { return real->GetProperty(p, h); }
    STDMETHODIMP SetProperty(REFGUID p, LPCDIPROPHEADER h) override
    {
        static volatile LONG n;
        const void *id = &p;
        if (id == &DIPROP_AUTOCENTER) {
            if (h && h->dwSize >= sizeof(DIPROPDWORD) && ((LPCDIPROPDWORD)h)->dwData == DIPROPAUTOCENTER_OFF) return real->SetProperty(p, h);
            if (sample(n)) logf("fence: SetProperty AUTOCENTER on not sent to the device");
            return DI_OK;
        }
        if (id == &DIPROP_FFGAIN) { if (sample(n)) logf("fence: SetProperty FFGAIN not sent to the device"); return DI_OK; }
        if (id == &DIPROP_RANGE || id == &DIPROP_DEADZONE || id == &DIPROP_SATURATION || id == &DIPROP_BUFFERSIZE || id == &DIPROP_AXISMODE)
            return real->SetProperty(p, h);
        return refuse("SetProperty (a property outside the whitelist)");
    }
    STDMETHODIMP Acquire() override { return real->Acquire(); }
    STDMETHODIMP Unacquire() override { return real->Unacquire(); }
    bool axisRange(int axis, long &mn, long &mx)
    {
        static const DWORD ofs[8] = {DIJOFS_X, DIJOFS_Y, DIJOFS_Z, DIJOFS_RX, DIJOFS_RY, DIJOFS_RZ, DIJOFS_SLIDER(0), DIJOFS_SLIDER(1)};
        DIPROPRANGE r{};
        r.diph.dwSize = sizeof r; r.diph.dwHeaderSize = sizeof r.diph; r.diph.dwObj = ofs[axis]; r.diph.dwHow = DIPH_BYOFFSET;
        if (FAILED(real->GetProperty(DIPROP_RANGE, &r.diph))) return false;
        mn = r.lMin; mx = r.lMax;
        return true;
    }
    void inject(dbce::controls::InjectionTable &inj, DWORD cb, LPVOID data)
    {
        const bool standard = (format == 1 && cb == sizeof(DIJOYSTATE)) || (format == 2 && cb == sizeof(DIJOYSTATE2));
        if (!standard) {
            if (!formatWarned && inj.pendingFor(instance)) {
                formatWarned = true;
                logf("inject: device %s reads with a data format that is not c_dfDIJoystick/2 (%lu bytes); not delivered", instance.c_str(), cb);
            }
            return;
        }
        auto *s = (DIJOYSTATE *)data;
        dbce::controls::StateView v;
        v.axes = (long *)&s->lX;
        v.buttons = cb == sizeof(DIJOYSTATE2) ? ((DIJOYSTATE2 *)data)->rgbButtons : s->rgbButtons;
        v.buttonCount = cb == sizeof(DIJOYSTATE2) ? 128 : 32;
        v.povs = cb == sizeof(DIJOYSTATE2) ? (unsigned long *)((DIJOYSTATE2 *)data)->rgdwPOV : (unsigned long *)s->rgdwPOV;
        std::vector<std::string> lines;
        inj.apply(instance, v, GetTickCount64(), [this](int axis, long &mn, long &mx) { return axisRange(axis, mn, mx); }, lines);
        for (auto &l : lines) logf("%s", l.c_str());
    }
    STDMETHODIMP GetDeviceState(DWORD cb, LPVOID data) override
    {
        HRESULT hr = real->GetDeviceState(cb, data);
        if (!SUCCEEDED(hr) || !data) return hr;
        auto *inj = settings().injections;
        if (inj && inj->any()) inject(*inj, cb, data);
        if (const auto *shapes = settings().shapes)
            dbce::shape::applyToState(*shapes, instance, format, cb, data, [this](int axis, long &mn, long &mx) { return axisRange(axis, mn, mx); },
                [](const dbce::controls::PedalShape &s, dbce::controls::ShapeMiss m, long mn, long mx) { logf("%s", dbce::shape::firstLine(s, m, mn, mx).c_str()); });
        if (const auto observe = settings().observe) {
            const LONG f = format;
            if ((f == 1 && cb == sizeof(DIJOYSTATE)) || (f == 2 && cb == sizeof(DIJOYSTATE2))) observe(real, (int)f, cb, data);
        }
        return hr;
    }
    STDMETHODIMP GetDeviceData(DWORD n, LPDIDEVICEOBJECTDATA d, LPDWORD k, DWORD f) override { return real->GetDeviceData(n, d, k, f); }
    STDMETHODIMP SetDataFormat(LPCDIDATAFORMAT f) override
    {
        HRESULT hr = real->SetDataFormat(f);
        if (SUCCEEDED(hr)) InterlockedExchange(&format, standardFormat(f));
        return hr;
    }
    STDMETHODIMP SetEventNotification(HANDLE e) override { return real->SetEventNotification(e); }
    STDMETHODIMP SetCooperativeLevel(HWND w, DWORD f) override { return real->SetCooperativeLevel(w, f); }
    STDMETHODIMP GetObjectInfo(typename T::ObjInst o, DWORD obj, DWORD how) override { return real->GetObjectInfo(o, obj, how); }
    STDMETHODIMP GetDeviceInfo(typename T::DevInst d) override { return real->GetDeviceInfo(d); }
    STDMETHODIMP RunControlPanel(HWND, DWORD) override { return refuse("device RunControlPanel"); }
    STDMETHODIMP Initialize(HINSTANCE, DWORD, REFGUID) override { return refuse("device Initialize (re-targeting the device)"); }
    STDMETHODIMP CreateEffect(REFGUID g, LPCDIEFFECT e, LPDIRECTINPUTEFFECT *out, LPUNKNOWN outer) override
    {
        static volatile LONG n;
        if (!out) return E_POINTER;
        *out = nullptr;
        if (outer) return refuse("CreateEffect with aggregation");
        auto *sink = new (std::nothrow) SinkEffect(g);
        if (!sink) return E_OUTOFMEMORY;
        sink->keep(e, e ? DIEP_ALLPARAMS : 0);
        *out = sink;
        if (sample(n)) logf("fence: CreateEffect answered with a software effect; nothing created on the device");
        return DI_OK;
    }
    STDMETHODIMP EnumEffects(typename T::EffCb cb, LPVOID ref, DWORD type) override { return real->EnumEffects(cb, ref, type); }
    STDMETHODIMP GetEffectInfo(typename T::EffInfo i, REFGUID g) override { return real->GetEffectInfo(i, g); }
    STDMETHODIMP GetForceFeedbackState(LPDWORD s) override
    {
        if (!s) return E_POINTER;
        *s = DIGFFS_STOPPED | DIGFFS_ACTUATORSON | DIGFFS_POWERON | DIGFFS_EMPTY;   // answered, not asked of the device
        return DI_OK;
    }
    STDMETHODIMP SendForceFeedbackCommand(DWORD cmd) override
    {
        static volatile LONG n;
        if (sample(n)) logf("fence: SendForceFeedbackCommand 0x%lx not sent to the device", cmd);
        return DI_OK;
    }
    STDMETHODIMP EnumCreatedEffectObjects(LPDIENUMCREATEDEFFECTOBJECTSCALLBACK, LPVOID, DWORD) override { return DI_OK; }   // none on the device
    STDMETHODIMP Escape(LPDIEFFESCAPE) override { return refuse("device Escape"); }
    STDMETHODIMP Poll() override { return real->Poll(); }
    STDMETHODIMP SendDeviceData(DWORD, LPCDIDEVICEOBJECTDATA, LPDWORD, DWORD) override { return refuse("SendDeviceData"); }
    STDMETHODIMP EnumEffectsInFile(typename T::Str, LPDIENUMEFFECTSINFILECALLBACK, LPVOID, DWORD) override { return refuse("EnumEffectsInFile"); }
    STDMETHODIMP WriteEffectToFile(typename T::Str, DWORD, LPDIFILEEFFECT, DWORD) override { return refuse("WriteEffectToFile"); }
    STDMETHODIMP BuildActionMap(typename T::ActFmt f, typename T::Str u, DWORD fl) override { return real->BuildActionMap(f, u, fl); }
    STDMETHODIMP SetActionMap(typename T::ActFmt f, typename T::Str u, DWORD fl) override
    {
        HRESULT hr = real->SetActionMap(f, u, fl);
        if (SUCCEEDED(hr)) InterlockedExchange(&format, 0);   // an action map sets its own data format
        return hr;
    }
    STDMETHODIMP GetImageInfo(typename T::Image h) override { return real->GetImageInfo(h); }
};

// ---- the DirectInput wrapper ---------------------------------------------------------------------------------------
template <class I> struct InputTraits;
template <> struct InputTraits<IDirectInput8A> {
    using Dev = IDirectInputDevice8A; using EnumCb = LPDIENUMDEVICESCALLBACKA; using Str = LPCSTR; using ActFmt = LPDIACTIONFORMATA;
    using SemCb = LPDIENUMDEVICESBYSEMANTICSCBA; using CfgParams = LPDICONFIGUREDEVICESPARAMSA;
    static const IID &iid() { return IID_IDirectInput8A; }
};
template <> struct InputTraits<IDirectInput8W> {
    using Dev = IDirectInputDevice8W; using EnumCb = LPDIENUMDEVICESCALLBACKW; using Str = LPCWSTR; using ActFmt = LPDIACTIONFORMATW;
    using SemCb = LPDIENUMDEVICESBYSEMANTICSCBW; using CfgParams = LPDICONFIGUREDEVICESPARAMSW;
    static const IID &iid() { return IID_IDirectInput8W; }
};

template <class I>
struct FencedDirectInput final : I {
    using T = InputTraits<I>;
    using Dev = typename T::Dev;
    I *real;
    volatile LONG refs = 1;
    explicit FencedDirectInput(I *r) : real(r) {}
    STDMETHODIMP QueryInterface(REFIID iid, LPVOID *out) override
    {
        if (!out) return E_POINTER;
        if (IsEqualGUID(iid, IID_IUnknown) || IsEqualGUID(iid, T::iid())) { AddRef(); *out = this; return S_OK; }
        *out = nullptr;
        logf("fence: DirectInput QueryInterface for another interface refused");
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return (ULONG)InterlockedIncrement(&refs); }
    STDMETHODIMP_(ULONG) Release() override { LONG n = InterlockedDecrement(&refs); if (n == 0) { real->Release(); delete this; } return (ULONG)n; }
    STDMETHODIMP CreateDevice(REFGUID g, Dev **out, LPUNKNOWN outer) override
    {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (outer) { logf("fence: CreateDevice with aggregation refused"); return DIERR_NOAGGREGATION; }
        Dev *dev = nullptr;
        HRESULT hr = real->CreateDevice(g, &dev, nullptr);
        if (FAILED(hr) || !dev) return FAILED(hr) ? hr : E_FAIL;
        typename DeviceTraits<Dev>::DevInstT info{};
        info.dwSize = sizeof info;
        const std::string inst = SUCCEEDED(dev->GetDeviceInfo(&info)) ? guidText(info.guidInstance) : guidText(g);
        auto *w = new (std::nothrow) FencedDevice<Dev>(dev, inst);
        if (!w) { dev->Release(); return E_OUTOFMEMORY; }
        *out = w;
        static volatile LONG n;
        if (sample(n)) logf("fence: device %s created behind the fence", inst.c_str());
        return hr;
    }
    STDMETHODIMP EnumDevices(DWORD type, typename T::EnumCb cb, LPVOID ref, DWORD flags) override { return real->EnumDevices(type, cb, ref, flags); }
    STDMETHODIMP GetDeviceStatus(REFGUID g) override { return real->GetDeviceStatus(g); }
    STDMETHODIMP RunControlPanel(HWND, DWORD) override { logf("fence: RunControlPanel refused"); return DIERR_UNSUPPORTED; }
    STDMETHODIMP Initialize(HINSTANCE h, DWORD v) override { return real->Initialize(h, v); }
    STDMETHODIMP FindDevice(REFGUID g, typename T::Str name, LPGUID out) override { return real->FindDevice(g, name, out); }
    STDMETHODIMP EnumDevicesBySemantics(typename T::Str, typename T::ActFmt, typename T::SemCb cb, LPVOID, DWORD) override
    {
        logf("fence: EnumDevicesBySemantics answered with no devices (it would hand out unfenced devices)");
        return cb ? DI_OK : E_INVALIDARG;
    }
    STDMETHODIMP ConfigureDevices(LPDICONFIGUREDEVICESCALLBACK, typename T::CfgParams, DWORD, LPVOID) override
    {
        logf("fence: ConfigureDevices refused");
        return DIERR_UNSUPPORTED;
    }
};

}} // namespace dbce::fence
