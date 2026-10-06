// dinput8.dll proxy: forwards to the real DirectInput and patches four
// vtables so the game sees the wheel the way it wants to, while we watch
// what the game reads from it and pushes to it.
//
// WHY THE RE-TYPE
// DirectInput classifies an HID joystick with six or more axes as
// DI8DEVTYPE_1STPERSON subtype SIXDOF (0x18). A direct-drive base (MOZA R12:
// 8 axes, 128 buttons) is therefore never DI8DEVTYPE_DRIVING, and a game that
// decides "is this a wheel" from dwDevType offers it as neither wheel nor
// gamepad. Gravel shows no wheel option and flickers between keyboard and
// controller. Rewriting dwDevType in the two places a game reads it fixes
// that; nothing else is altered so FFB passes through intact.
//
// THE TAPS
// The game polls the wheel with IDirectInputDevice8::GetDeviceState and
// drives it with IDirectInputEffect::SetParameters. Both are hooked
// observe-only: steering/pedals become InputState, effect magnitudes become
// FfbState. That is real, per-frame data with no reverse engineering.
//
// Vtables are shared by every instance of a class, so each hook checks that
// `self` is one of the devices/effects we care about and otherwise forwards
// untouched.
#include "common.h"
#include <cmath>

static HMODULE g_real = nullptr;
typedef HRESULT (WINAPI *PFN_Create)(HINSTANCE, DWORD, REFIID, LPVOID *, LPUNKNOWN);

static bool patchVtable(void *obj, int slot, void *hook, void **orig)
{
    void **vt = *(void ***)obj;
    if (*orig)
        return true;
    DWORD old;
    if (!VirtualProtect(&vt[slot], sizeof(void *), PAGE_EXECUTE_READWRITE, &old))
        return false;
    *orig = vt[slot];
    vt[slot] = hook;
    VirtualProtect(&vt[slot], sizeof(void *), old, &old);
    return true;
}

static bool retype(const GUID &product, DWORD &devType)
{
    if (!g_cfg.retype || product.Data1 != g_cfg.product)
        return false;
    if ((devType & 0xFF) == DI8DEVTYPE_DRIVING)
        return false;
    DWORD before = devType;
    devType = (devType & 0xFFFF0000) | DI8DEVTYPE_DRIVING | (DI8DEVTYPEDRIVING_THREEPEDALS << 8);
    logf("[proxy] retyped %08lx: 0x%08lx -> 0x%08lx", (unsigned long)product.Data1,
         (unsigned long)before, (unsigned long)devType);
    return true;
}

// ------------------------------------------------------- tracked devices
// Devices whose product matches the target. Small fixed table; games create
// a handful of device objects at most.
struct TrackedDev {
    void *dev = nullptr;
    bool  unicode = false;
    DWORD dataSize = 0;
    bool  rangesKnown = false;
    LONG  lo[8] = {0}, hi[8] = {0};
    bool  everLive = false;   // has this object ever returned a non-blank state?
};
// The game creates several device objects for the same wheel; some are
// only ever enumerated or held unacquired and read back all zeros, which
// would overwrite the real reads at poll rate. Once any object has produced
// a non-blank state, only objects that have done so may update the state.
static void *g_liveDev = nullptr;
static TrackedDev g_devs[16];
static CRITICAL_SECTION g_devLock;

static TrackedDev *findDev(void *self)
{
    for (auto &d : g_devs)
        if (d.dev == self) return &d;
    return nullptr;
}
static TrackedDev *trackDev(void *self, bool unicode)
{
    EnterCriticalSection(&g_devLock);
    TrackedDev *t = findDev(self);
    if (!t)
        for (auto &d : g_devs)
            if (!d.dev) { d.dev = self; d.unicode = unicode; t = &d; break; }
    LeaveCriticalSection(&g_devLock);
    return t;
}

// ----------------------------------------------------------- device hooks
typedef HRESULT (STDMETHODCALLTYPE *PFN_GetCaps)(void *, LPDIDEVCAPS);
typedef HRESULT (STDMETHODCALLTYPE *PFN_GetInfoW)(void *, LPDIDEVICEINSTANCEW);
typedef HRESULT (STDMETHODCALLTYPE *PFN_GetInfoA)(void *, LPDIDEVICEINSTANCEA);
typedef HRESULT (STDMETHODCALLTYPE *PFN_GetProp)(void *, REFGUID, LPDIPROPHEADER);
typedef HRESULT (STDMETHODCALLTYPE *PFN_SetProp)(void *, REFGUID, LPCDIPROPHEADER);
typedef HRESULT (STDMETHODCALLTYPE *PFN_GetState)(void *, DWORD, LPVOID);
typedef HRESULT (STDMETHODCALLTYPE *PFN_SetFmt)(void *, LPCDIDATAFORMAT);
typedef HRESULT (STDMETHODCALLTYPE *PFN_CreateEffect)(void *, REFGUID, LPCDIEFFECT, LPDIRECTINPUTEFFECT *, LPUNKNOWN);

static void *g_oGetCapsW, *g_oGetInfoW, *g_oGetStateW, *g_oSetFmtW, *g_oCreateEffW, *g_oGetPropW, *g_oSetPropW;
static void *g_oGetCapsA, *g_oGetInfoA, *g_oGetStateA, *g_oSetFmtA, *g_oCreateEffA, *g_oGetPropA, *g_oSetPropA;

static bool productOf(void *self, bool unicode, GUID &out)
{
    if (unicode) {
        DIDEVICEINSTANCEW di; ZeroMemory(&di, sizeof di); di.dwSize = sizeof di;
        if (!g_oGetInfoW || FAILED(((PFN_GetInfoW)g_oGetInfoW)(self, &di))) return false;
        out = di.guidProduct; return true;
    }
    DIDEVICEINSTANCEA di; ZeroMemory(&di, sizeof di); di.dwSize = sizeof di;
    if (!g_oGetInfoA || FAILED(((PFN_GetInfoA)g_oGetInfoA)(self, &di))) return false;
    out = di.guidProduct; return true;
}

static HRESULT STDMETHODCALLTYPE HookGetCaps(void *self, LPDIDEVCAPS caps, bool uni)
{
    HRESULT hr = ((PFN_GetCaps)(uni ? g_oGetCapsW : g_oGetCapsA))(self, caps);
    GUID p;
    if (SUCCEEDED(hr) && caps && productOf(self, uni, p))
        retype(p, caps->dwDevType);
    return hr;
}
static HRESULT STDMETHODCALLTYPE HookGetCapsW(void *s, LPDIDEVCAPS c) { return HookGetCaps(s, c, true); }
static HRESULT STDMETHODCALLTYPE HookGetCapsA(void *s, LPDIDEVCAPS c) { return HookGetCaps(s, c, false); }

static HRESULT STDMETHODCALLTYPE HookGetInfoW(void *self, LPDIDEVICEINSTANCEW di)
{
    HRESULT hr = ((PFN_GetInfoW)g_oGetInfoW)(self, di);
    if (SUCCEEDED(hr) && di) retype(di->guidProduct, di->dwDevType);
    return hr;
}
static HRESULT STDMETHODCALLTYPE HookGetInfoA(void *self, LPDIDEVICEINSTANCEA di)
{
    HRESULT hr = ((PFN_GetInfoA)g_oGetInfoA)(self, di);
    if (SUCCEEDED(hr) && di) retype(di->guidProduct, di->dwDevType);
    return hr;
}

// SetDataFormat tells us whether the game reads DIJOYSTATE (80 B, 32
// buttons) or DIJOYSTATE2 (272 B, 128 buttons). Logged, never altered - it
// decides whether buttons above 32 can ever reach the game.
static HRESULT STDMETHODCALLTYPE HookSetFmt(void *self, LPCDIDATAFORMAT df, bool uni)
{
    if (df) {
        if (TrackedDev *t = findDev(self)) t->dataSize = df->dwDataSize;
        logf("[proxy] SetDataFormat dwDataSize=%lu dwNumObjs=%lu -> %s", (unsigned long)df->dwDataSize,
             (unsigned long)df->dwNumObjs,
             df->dwDataSize >= 272 ? "DIJOYSTATE2 (128 buttons)"
             : df->dwDataSize >= 80 ? "DIJOYSTATE (32 buttons)" : "custom");
    }
    return ((PFN_SetFmt)(uni ? g_oSetFmtW : g_oSetFmtA))(self, df);
}
static HRESULT STDMETHODCALLTYPE HookSetFmtW(void *s, LPCDIDATAFORMAT d) { return HookSetFmt(s, d, true); }
static HRESULT STDMETHODCALLTYPE HookSetFmtA(void *s, LPCDIDATAFORMAT d) { return HookSetFmt(s, d, false); }

// The game may narrow an axis range with SetProperty(DIPROP_RANGE) - possibly
// after our first read. Watch for it and adopt it, so normalisation tracks
// whatever range the game actually asked for.
static HRESULT STDMETHODCALLTYPE HookSetProp(void *self, REFGUID g, LPCDIPROPHEADER h, bool uni)
{
    HRESULT hr = ((PFN_SetProp)(uni ? g_oSetPropW : g_oSetPropA))(self, g, h);
    TrackedDev *t = findDev(self);
    if (t && h && &g == &DIPROP_RANGE) {
        const DIPROPRANGE *r = (const DIPROPRANGE *)h;
        logf("[input] game SetProperty(RANGE) how=%lu obj=0x%lx -> %ld..%ld", (unsigned long)h->dwHow,
             (unsigned long)h->dwObj, r->lMin, r->lMax);
        if (h->dwHow == DIPH_DEVICE) { for (int i = 0; i < 8; ++i) { t->lo[i] = r->lMin; t->hi[i] = r->lMax; } t->rangesKnown = true; }
        else if (h->dwHow == DIPH_BYOFFSET && h->dwObj < 32) { int i = (int)h->dwObj / 4; t->lo[i] = r->lMin; t->hi[i] = r->lMax; }
    }
    return hr;
}
static HRESULT STDMETHODCALLTYPE HookSetPropW(void *s, REFGUID g, LPCDIPROPHEADER h) { return HookSetProp(s, g, h, true); }
static HRESULT STDMETHODCALLTYPE HookSetPropA(void *s, REFGUID g, LPCDIPROPHEADER h) { return HookSetProp(s, g, h, false); }

// DIJOYSTATE axis slots by name, in struct order.
static int axisIndex(const char *name)
{
    static const char *names[8] = {"lX", "lY", "lZ", "lRx", "lRy", "lRz", "slider0", "slider1"};
    for (int i = 0; i < 8; ++i)
        if (_stricmp(name, names[i]) == 0) return i;
    return -1;
}

static void learnRanges(TrackedDev *t)
{
    static const DWORD offs[8] = {DIJOFS_X, DIJOFS_Y, DIJOFS_Z, DIJOFS_RX, DIJOFS_RY, DIJOFS_RZ,
                                  DIJOFS_SLIDER(0), DIJOFS_SLIDER(1)};
    PFN_GetProp gp = (PFN_GetProp)(t->unicode ? g_oGetPropW : g_oGetPropA);
    for (int i = 0; i < 8; ++i) {
        t->lo[i] = 0; t->hi[i] = 65535;               // DirectInput default
        if (!gp) continue;
        DIPROPRANGE r; ZeroMemory(&r, sizeof r);
        r.diph.dwSize = sizeof r; r.diph.dwHeaderSize = sizeof(DIPROPHEADER);
        r.diph.dwObj = offs[i]; r.diph.dwHow = DIPH_BYOFFSET;
        if (SUCCEEDED(gp(t->dev, DIPROP_RANGE, &r.diph)) && r.lMax > r.lMin) { t->lo[i] = r.lMin; t->hi[i] = r.lMax; }
    }
    t->rangesKnown = true;
    logf("[input] axis ranges: X %ld..%ld  Z %ld..%ld  Rz %ld..%ld  S0 %ld..%ld", t->lo[0], t->hi[0],
         t->lo[2], t->hi[2], t->lo[5], t->hi[5], t->lo[6], t->hi[6]);
}

static float norm01(LONG v, LONG lo, LONG hi)
{
    if (hi <= lo) return 0.f;
    float f = float(v - lo) / float(hi - lo);
    return f < 0 ? 0 : f > 1 ? 1 : f;
}

static HRESULT STDMETHODCALLTYPE HookGetState(void *self, DWORD cb, LPVOID data, bool uni)
{
    HRESULT hr = ((PFN_GetState)(uni ? g_oGetStateW : g_oGetStateA))(self, cb, data);
    TrackedDev *t = findDev(self);
    if (FAILED(hr) || !t || !data || cb < 80)
        return hr;                                   // not ours, or a custom format
    if (!t->rangesKnown) learnRanges(t);
    const LONG *ax = (const LONG *)data;             // lX lY lZ lRx lRy lRz slider[2]
    {
        bool blank = true;
        for (int i = 0; i < 8 && blank; ++i) if (ax[i] != 0) blank = false;
        const BYTE *b = (const BYTE *)data + 48;
        for (int i = 0; i < 32 && blank; ++i) if (b[i] & 0x80) blank = false;
        if (!blank && !t->everLive) { t->everLive = true; g_liveDev = self; logf("[input] device %p is delivering real state", self); }
        if (g_liveDev && !t->everLive) return hr;      // a blank twin; ignore
    }
    auto get = [&](const char *name, bool bipolar) -> float {
        int i = axisIndex(name);
        if (i < 0) return 0.f;
        float f = norm01(ax[i], t->lo[i], t->hi[i]);
        return bipolar ? f * 2.f - 1.f : f;
    };
    { int i = axisIndex(g_cfg.steer); g_input.rawSteer = i >= 0 ? (int)ax[i] : 0; }
    { int i = axisIndex(g_cfg.throttle); g_input.rawThr = i >= 0 ? (int)ax[i] : 0; }
    { int i = axisIndex(g_cfg.brake); g_input.rawBrk = i >= 0 ? (int)ax[i] : 0; }
    g_input.steer     = get(g_cfg.steer, true);
    g_input.throttle  = get(g_cfg.throttle, false);
    g_input.brake     = get(g_cfg.brake, false);
    g_input.handbrake = get(g_cfg.handbrake, false);
    g_input.clutch    = g_cfg.clutch[0] ? get(g_cfg.clutch, false) : 0.f;
    const BYTE *btn = (const BYTE *)data + 48;
    uint32_t mask = 0;
    for (int i = 0; i < 32; ++i) if (btn[i] & 0x80) mask |= 1u << i;
    g_input.buttons = mask;
    g_input.live = true;
    ++g_input.reads;
    return hr;
}
static HRESULT STDMETHODCALLTYPE HookGetStateW(void *s, DWORD c, LPVOID d) { return HookGetState(s, c, d, true); }
static HRESULT STDMETHODCALLTYPE HookGetStateA(void *s, DWORD c, LPVOID d) { return HookGetState(s, c, d, false); }

// ----------------------------------------------------------- effect hooks
enum EffKind { EK_OTHER, EK_CONSTANT, EK_PERIODIC, EK_SPRING, EK_DAMPER, EK_RAMP };
// The game runs two constant-force effects at once; a single shared value
// would be whichever updated last, which is usually the smaller of the two.
// Each effect keeps its own magnitude and the strongest wins.
struct TrackedEff { void *eff = nullptr; EffKind kind = EK_OTHER; float gain = 1.f; float mag = 0.f; };
static TrackedEff g_effs[64];
typedef HRESULT (STDMETHODCALLTYPE *PFN_SetParams)(void *, LPCDIEFFECT, DWORD);
static void *g_oSetParams = nullptr;

static EffKind kindOf(REFGUID g)
{
    if (IsEqualGUID(g, GUID_ConstantForce)) return EK_CONSTANT;
    if (IsEqualGUID(g, GUID_Sine) || IsEqualGUID(g, GUID_Square) || IsEqualGUID(g, GUID_Triangle) ||
        IsEqualGUID(g, GUID_SawtoothUp) || IsEqualGUID(g, GUID_SawtoothDown)) return EK_PERIODIC;
    if (IsEqualGUID(g, GUID_Spring)) return EK_SPRING;
    if (IsEqualGUID(g, GUID_Damper) || IsEqualGUID(g, GUID_Friction) || IsEqualGUID(g, GUID_Inertia)) return EK_DAMPER;
    if (IsEqualGUID(g, GUID_RampForce)) return EK_RAMP;
    return EK_OTHER;
}
static const char *kindName(EffKind k)
{
    static const char *n[] = {"other", "constant", "periodic", "spring", "damper", "ramp"};
    return n[k];
}

static void absorb(TrackedEff *t, LPCDIEFFECT e, DWORD flags)
{
    if (!e) return;
    if (flags & DIEP_GAIN) t->gain = float(e->dwGain) / 10000.f;
    if (!(flags & DIEP_TYPESPECIFICPARAMS) || !e->lpvTypeSpecificParams) return;
    float g = t->gain * g_cfg.ffbGain;
    switch (t->kind) {
    case EK_CONSTANT:
        if (e->cbTypeSpecificParams >= sizeof(DICONSTANTFORCE)) {
            LONG m = ((const DICONSTANTFORCE *)e->lpvTypeSpecificParams)->lMagnitude;
            // direction: with a single axis, rglDirection[0] sign flips the force
            float dir = (e->cAxes >= 1 && e->rglDirection && e->rglDirection[0] < 0) ? -1.f : 1.f;
            t->mag = float(m) / 10000.f * dir * g;
            float best = 0.f;
            for (const auto &o : g_effs)
                if (o.eff && o.kind == EK_CONSTANT && fabsf(o.mag) > fabsf(best)) best = o.mag;
            g_ffb.constant = best;
        }
        break;
    case EK_PERIODIC:
        if (e->cbTypeSpecificParams >= sizeof(DIPERIODIC))
            g_ffb.periodic = float(((const DIPERIODIC *)e->lpvTypeSpecificParams)->dwMagnitude) / 10000.f * g;
        break;
    case EK_SPRING: case EK_DAMPER:
        if (e->cbTypeSpecificParams >= sizeof(DICONDITION)) {
            const DICONDITION *c = (const DICONDITION *)e->lpvTypeSpecificParams;
            LONG a = c->lPositiveCoefficient < 0 ? -c->lPositiveCoefficient : c->lPositiveCoefficient;
            LONG b = c->lNegativeCoefficient < 0 ? -c->lNegativeCoefficient : c->lNegativeCoefficient;
            float v = float(a > b ? a : b) / 10000.f * g;
            if (t->kind == EK_SPRING) g_ffb.spring = v; else g_ffb.damper = v;
        }
        break;
    case EK_RAMP:
        if (e->cbTypeSpecificParams >= sizeof(DIRAMPFORCE))
            g_ffb.constant = float(((const DIRAMPFORCE *)e->lpvTypeSpecificParams)->lEnd) / 10000.f * g;
        break;
    default: break;
    }
    g_ffb.live = true;
    ++g_ffb.updates;
}

static HRESULT STDMETHODCALLTYPE HookSetParams(void *self, LPCDIEFFECT e, DWORD flags)
{
    for (auto &t : g_effs)
        if (t.eff == self) { absorb(&t, e, flags); break; }
    return ((PFN_SetParams)g_oSetParams)(self, e, flags);
}

static HRESULT STDMETHODCALLTYPE HookCreateEffect(void *self, REFGUID guid, LPCDIEFFECT e,
                                                  LPDIRECTINPUTEFFECT *out, LPUNKNOWN unk, bool uni)
{
    HRESULT hr = ((PFN_CreateEffect)(uni ? g_oCreateEffW : g_oCreateEffA))(self, guid, e, out, unk);
    if (FAILED(hr) || !out || !*out || !findDev(self))
        return hr;
    for (auto &t : g_effs) {
        if (t.eff && t.eff != *out) continue;
        t.eff = *out; t.kind = kindOf(guid); t.gain = 1.f;
        patchVtable(*out, 6, (void *)HookSetParams, &g_oSetParams);   // IDirectInputEffect::SetParameters
        absorb(&t, e, DIEP_ALLPARAMS);
        ++g_ffb.effects;
        logf("[ffb] effect created: %s (#%u)", kindName(t.kind), (unsigned)g_ffb.effects);
        break;
    }
    return hr;
}
static HRESULT STDMETHODCALLTYPE HookCreateEffW(void *s, REFGUID g, LPCDIEFFECT e, LPDIRECTINPUTEFFECT *o, LPUNKNOWN u) { return HookCreateEffect(s, g, e, o, u, true); }
static HRESULT STDMETHODCALLTYPE HookCreateEffA(void *s, REFGUID g, LPCDIEFFECT e, LPDIRECTINPUTEFFECT *o, LPUNKNOWN u) { return HookCreateEffect(s, g, e, o, u, false); }

// -------------------------------------------------------- interface hooks
typedef HRESULT (STDMETHODCALLTYPE *PFN_EnumW)(void *, DWORD, LPDIENUMDEVICESCALLBACKW, LPVOID, DWORD);
typedef HRESULT (STDMETHODCALLTYPE *PFN_EnumA)(void *, DWORD, LPDIENUMDEVICESCALLBACKA, LPVOID, DWORD);
typedef HRESULT (STDMETHODCALLTYPE *PFN_CreateW)(void *, REFGUID, LPDIRECTINPUTDEVICE8W *, LPUNKNOWN);
typedef HRESULT (STDMETHODCALLTYPE *PFN_CreateA)(void *, REFGUID, LPDIRECTINPUTDEVICE8A *, LPUNKNOWN);
static void *g_oEnumW, *g_oEnumA, *g_oCreateW, *g_oCreateA;

struct CtxW { LPDIENUMDEVICESCALLBACKW cb; LPVOID ref; };
struct CtxA { LPDIENUMDEVICESCALLBACKA cb; LPVOID ref; };
static BOOL CALLBACK ShimEnumW(LPCDIDEVICEINSTANCEW i, LPVOID pv)
{
    CtxW *c = (CtxW *)pv; DIDEVICEINSTANCEW copy = *i; retype(copy.guidProduct, copy.dwDevType);
    return c->cb(&copy, c->ref);
}
static BOOL CALLBACK ShimEnumA(LPCDIDEVICEINSTANCEA i, LPVOID pv)
{
    CtxA *c = (CtxA *)pv; DIDEVICEINSTANCEA copy = *i; retype(copy.guidProduct, copy.dwDevType);
    return c->cb(&copy, c->ref);
}
static HRESULT STDMETHODCALLTYPE HookEnumW(void *s, DWORD t, LPDIENUMDEVICESCALLBACKW cb, LPVOID pv, DWORD f)
{ CtxW c = {cb, pv}; return ((PFN_EnumW)g_oEnumW)(s, t, ShimEnumW, &c, f); }
static HRESULT STDMETHODCALLTYPE HookEnumA(void *s, DWORD t, LPDIENUMDEVICESCALLBACKA cb, LPVOID pv, DWORD f)
{ CtxA c = {cb, pv}; return ((PFN_EnumA)g_oEnumA)(s, t, ShimEnumA, &c, f); }

// IDirectInputDevice8 vtable: 3 GetCapabilities, 5 GetProperty, 9 GetDeviceState,
// 11 SetDataFormat, 15 GetDeviceInfo, 18 CreateEffect
static void hookDevice(void *dev, bool uni)
{
    if (uni) {
        patchVtable(dev, 15, (void *)HookGetInfoW,   &g_oGetInfoW);
        patchVtable(dev,  3, (void *)HookGetCapsW,   &g_oGetCapsW);
        patchVtable(dev, 11, (void *)HookSetFmtW,    &g_oSetFmtW);
        patchVtable(dev,  9, (void *)HookGetStateW,  &g_oGetStateW);
        patchVtable(dev, 18, (void *)HookCreateEffW, &g_oCreateEffW);
        patchVtable(dev,  6, (void *)HookSetPropW,   &g_oSetPropW);
        if (!g_oGetPropW) g_oGetPropW = (*(void ***)dev)[5];
    } else {
        patchVtable(dev, 15, (void *)HookGetInfoA,   &g_oGetInfoA);
        patchVtable(dev,  3, (void *)HookGetCapsA,   &g_oGetCapsA);
        patchVtable(dev, 11, (void *)HookSetFmtA,    &g_oSetFmtA);
        patchVtable(dev,  9, (void *)HookGetStateA,  &g_oGetStateA);
        patchVtable(dev, 18, (void *)HookCreateEffA, &g_oCreateEffA);
        patchVtable(dev,  6, (void *)HookSetPropA,   &g_oSetPropA);
        if (!g_oGetPropA) g_oGetPropA = (*(void ***)dev)[5];
    }
    GUID p;
    if (productOf(dev, uni, p) && p.Data1 == g_cfg.product) {
        trackDev(dev, uni);
        logf("[proxy] tracking device %p (product %08lx)", dev, (unsigned long)p.Data1);
    }
}
static HRESULT STDMETHODCALLTYPE HookCreateW(void *s, REFGUID g, LPDIRECTINPUTDEVICE8W *out, LPUNKNOWN u)
{
    HRESULT hr = ((PFN_CreateW)g_oCreateW)(s, g, out, u);
    if (SUCCEEDED(hr) && out && *out) hookDevice(*out, true);
    return hr;
}
static HRESULT STDMETHODCALLTYPE HookCreateA(void *s, REFGUID g, LPDIRECTINPUTDEVICE8A *out, LPUNKNOWN u)
{
    HRESULT hr = ((PFN_CreateA)g_oCreateA)(s, g, out, u);
    if (SUCCEEDED(hr) && out && *out) hookDevice(*out, false);
    return hr;
}

// ---------------------------------------------------------------- exports
static std::atomic<bool> g_inited{false};
void proxyInit()
{
    if (g_inited.exchange(true)) return;
    InitializeCriticalSection(&g_devLock);
    loadConfig();
    tripleReport();
    char sys[MAX_PATH];
    GetSystemDirectoryA(sys, MAX_PATH);
    strcat(sys, "\\dinput8.dll");
    g_real = LoadLibraryA(sys);
    logf("[proxy] real dinput8=%p product=%08lx retype=%d", (void *)g_real, (unsigned long)g_cfg.product, g_cfg.retype);
    fmodTapInstall();
    if (g_cfg.enabled) telemetryStart();
}

// IDirectInput8 vtable: 3 CreateDevice, 4 EnumDevices
extern "C" HRESULT WINAPI DirectInput8Create(HINSTANCE h, DWORD ver, REFIID riid, LPVOID *out, LPUNKNOWN unk)
{
    proxyInit();
    if (!g_real) return E_FAIL;
    PFN_Create fn = (PFN_Create)GetProcAddress(g_real, "DirectInput8Create");
    if (!fn) return E_FAIL;
    HRESULT hr = fn(h, ver, riid, out, unk);
    if (FAILED(hr) || !out || !*out) return hr;
    if (IsEqualIID(riid, IID_IDirectInput8W)) {
        patchVtable(*out, 4, (void *)HookEnumW, &g_oEnumW);
        patchVtable(*out, 3, (void *)HookCreateW, &g_oCreateW);
        logf("[proxy] hooked IDirectInput8W");
    } else if (IsEqualIID(riid, IID_IDirectInput8A)) {
        patchVtable(*out, 4, (void *)HookEnumA, &g_oEnumA);
        patchVtable(*out, 3, (void *)HookCreateA, &g_oCreateA);
        logf("[proxy] hooked IDirectInput8A");
    }
    return hr;
}

#define FWD(name, ret, sig, call, fail)                                     \
    extern "C" ret WINAPI name sig {                                        \
        proxyInit();                                                        \
        auto f = (ret(WINAPI *) sig)(g_real ? GetProcAddress(g_real, #name) : nullptr); \
        return f ? f call : (fail);                                         \
    }
FWD(DllCanUnloadNow, HRESULT, (void), (), S_FALSE)
FWD(DllGetClassObject, HRESULT, (REFCLSID c, REFIID i, LPVOID *o), (c, i, o), E_FAIL)
FWD(DllRegisterServer, HRESULT, (void), (), E_FAIL)
FWD(DllUnregisterServer, HRESULT, (void), (), E_FAIL)
FWD(GetdfDIJoystick, LPCDIDATAFORMAT, (void), (), nullptr)

BOOL WINAPI DllMain(HINSTANCE, DWORD reason, LPVOID)
{
    // Everything is deferred to the first exported call - LoadLibrary and
    // thread creation inside the loader lock are deadlocks waiting to happen.
    // Triples are the exception: they must answer the game's command line before its
    // entry point runs, and only write memory here (see triple.cpp).
    if (reason == DLL_PROCESS_ATTACH) tripleAttach();
    return TRUE;
}
