// See dbce_ffb_layer.h. Ported from the Rush Rally 3 proxy (0.2.2/0.3.0, 2026-10-08), where it was verified muted.
#define DIRECTINPUT_VERSION 0x0800
#include "dbce_ffb_layer.h"
#include "../settings/dbce_ini_single_key_win.hpp"
#include "dbce_ffb_gain.hpp"
#include "../panel/dbce_panel.h"
#include "../controls/dbce_inject_table.hpp"
#include "dbce_input_suppression.hpp"
#include "dbce_shape_state.hpp"
#include <dinput.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <algorithm>

#pragma comment(lib, "dxguid.lib")

namespace dbce { namespace ffb {

namespace {
Options g_opt;
CRITICAL_SECTION g_cs;
INIT_ONCE g_once = INIT_ONCE_STATIC_INIT;
volatile LONG g_on = 1, g_strength = 50, g_diagLines, g_swallowed;
void *g_wheelDev;

// Test injection (STD-033): the devices the game created, by instance, and the raw samples running on them (the same
// table and delivery as the fenced wrapper, dbce_fence.hpp).
// instance: the GUID the game passed to CreateDevice (test injection); reported: the instance GUID the device itself
// reports (GetDeviceInfo), which pedal shapes match, as the fence does.
struct DevRec { void *dev; GUID instance; int format; GUID reported; bool reportedOk; unsigned gen; };   // input::format; joystick injection/shaping only for exact 1/2
DevRec g_devs[32];
int g_devCount;
unsigned g_devGen;   // each recorded CreateDevice gets a new generation (a freed address can come back as a new device)
dbce::controls::InjectionTable g_table;
volatile LONG g_injectOk;
const dbce::controls::ShapeTable *volatile g_shapes;   // pedal rest shaping (SetShapes): set once, never freed
std::string guidText(const GUID &g)
{
    char b[40];
    sprintf_s(b, "{%08lx-%04hx-%04hx-%02x%02x-%02x%02x%02x%02x%02x%02x}", g.Data1, g.Data2, g.Data3, g.Data4[0], g.Data4[1],
              g.Data4[2], g.Data4[3], g.Data4[4], g.Data4[5], g.Data4[6], g.Data4[7]);
    return b;
}

BOOL CALLBACK initOnce(PINIT_ONCE, PVOID, PVOID *) { InitializeCriticalSection(&g_cs); return TRUE; }
void ensure() { InitOnceExecuteOnce(&g_once, initOnce, nullptr, nullptr); }

void dlog(const char *fmt, ...)
{
    if (!g_opt.log) return;
    char b[512];
    va_list ap;
    va_start(ap, fmt);
    _vsnprintf_s(b, sizeof b, _TRUNCATE, fmt, ap);
    va_end(ap);
    g_opt.log(b);
}
bool diagOk() { return InterlockedIncrement(&g_diagLines) <= 2000; }   // bounded per process

// ---- vtable patching, one original per (hook, table) -------------------------------------------------------
enum Hook { H_CREATEDEV, H_SETPROP, H_ACQUIRE, H_GETSTATE, H_GETDATA, H_SETCOOP, H_CREATEEFF, H_SENDFF,
            H_E_RELEASE, H_E_SETPARAMS, H_E_START, H_E_STOP, H_E_DOWNLOAD, H_SETFMT, H_D_RELEASE, H_SETACTION, H_COUNT };
struct Slot { void **vt; void *orig; };
Slot g_slots[H_COUNT][8];
// The answer for a table that was never recorded (should not happen): E_FAIL from a stub with the hook's own argument
// count, so a 32-bit __stdcall caller's stack stays balanced (every argument here is pointer-sized on x86).
HRESULT STDMETHODCALLTYPE unrecorded1(void *) { return E_FAIL; }
HRESULT STDMETHODCALLTYPE unrecorded2(void *, void *) { return E_FAIL; }
HRESULT STDMETHODCALLTYPE unrecorded3(void *, void *, void *) { return E_FAIL; }
HRESULT STDMETHODCALLTYPE unrecorded4(void *, void *, void *, void *) { return E_FAIL; }
HRESULT STDMETHODCALLTYPE unrecorded5(void *, void *, void *, void *, void *) { return E_FAIL; }
void *unrecorded(Hook h)
{
    static void *const byArgs[6] = {nullptr, (void *)&unrecorded1, (void *)&unrecorded2, (void *)&unrecorded3, (void *)&unrecorded4, (void *)&unrecorded5};
    static const int args[H_COUNT] = {4, 3, 1, 3, 5, 3, 5, 2, 1, 3, 3, 1, 1, 2, 1, 4};   // self included, in Hook order
    return byArgs[args[h]];
}
void *orig(Hook h, void *self)
{
    void **vt = *(void ***)self;
    void *o = nullptr;
    EnterCriticalSection(&g_cs);
    for (auto &s : g_slots[h]) if (s.vt == vt) { o = s.orig; break; }
    LeaveCriticalSection(&g_cs);
    if (!o) { static volatile LONG n; if (InterlockedIncrement(&n) <= 5) dlog("ffb: hook %d reached through an unrecorded table; returned E_FAIL", (int)h); }
    return o ? o : unrecorded(h);
}
void patch(Hook h, void *obj, int index, void *hook)
{
    void **vt = *(void ***)obj;
    EnterCriticalSection(&g_cs);
    if (vt[index] != hook) {
        for (auto &s : g_slots[h]) {
            if (s.vt == vt) break;
            if (!s.vt) {
                DWORD old;
                if (VirtualProtect(&vt[index], sizeof(void *), PAGE_EXECUTE_READWRITE, &old)) {
                    s.vt = vt; s.orig = vt[index]; vt[index] = hook;
                    VirtualProtect(&vt[index], sizeof(void *), old, &old);
                }
                break;
            }
        }
    }
    LeaveCriticalSection(&g_cs);
}
HRESULT reportedIdentity(void *self, GUID &reported) {
    DIDEVICEINSTANCEW info{};
    typedef HRESULT(STDMETHODCALLTYPE *InfoFn)(void *, void *);
    void **vt=*(void ***)self;
    info.dwSize=sizeof(DIDEVICEINSTANCEA);
    HRESULT hr=((InfoFn)vt[15])(self,&info);
    if(FAILED(hr)) { info.dwSize=sizeof(DIDEVICEINSTANCEW);hr=((InfoFn)vt[15])(self,&info); }
    if(SUCCEEDED(hr))reported=info.guidInstance;
    return hr;
}

// ---- effects -------------------------------------------------------------------------------------------------
const char *effName(REFGUID g)
{
    if (g == GUID_ConstantForce) return "ConstantForce";
    if (g == GUID_RampForce) return "RampForce";
    if (g == GUID_Square) return "Square";
    if (g == GUID_Sine) return "Sine";
    if (g == GUID_Triangle) return "Triangle";
    if (g == GUID_SawtoothUp) return "SawtoothUp";
    if (g == GUID_SawtoothDown) return "SawtoothDown";
    if (g == GUID_Spring) return "Spring";
    if (g == GUID_Damper) return "Damper";
    if (g == GUID_Inertia) return "Inertia";
    if (g == GUID_Friction) return "Friction";
    if (g == GUID_CustomForce) return "CustomForce";
    return "other";
}
Kind kindOf(REFGUID g)
{
    if (g == GUID_ConstantForce) return K_CONSTANT;
    if (g == GUID_RampForce) return K_RAMP;
    if (g == GUID_Square || g == GUID_Sine || g == GUID_Triangle || g == GUID_SawtoothUp || g == GUID_SawtoothDown) return K_PERIODIC;
    if (g == GUID_Spring || g == GUID_Damper || g == GUID_Inertia || g == GUID_Friction) return K_CONDITION;
    return K_OTHER;
}
// Per effect: the game's own (unscaled) parameters and gain, whether the game wants it running.
struct EffRec { void *eff; Kind kind; const char *name; int id; LONG calls; LONG lastSign; LONG minMag, maxMag; ULONGLONG summaryAt;
                bool emulate; DWORD gain; TypeParams params; DWORD bytes; bool haveEnv; DIENVELOPE env; bool running; DWORD iterations; };
EffRec g_effs[32];
int g_effCount;
EffRec *effRec(void *eff)
{
    for (int i = 0; i < g_effCount; ++i) if (g_effs[i].eff == eff) return &g_effs[i];
    return nullptr;
}
bool blocked() { return g_opt.mute || !g_on; }

void describe(char *b, size_t n, Kind k, LPCDIEFFECT e, DWORD flags)
{
    b[0] = 0;
    if (!e) { strcat_s(b, n, "no DIEFFECT"); return; }
    char t[160];
    if (flags & DIEP_DURATION) { sprintf_s(t, "dur=%lu ", e->dwDuration); strcat_s(b, n, t); }
    if (flags & DIEP_SAMPLEPERIOD) { sprintf_s(t, "sample=%lu ", e->dwSamplePeriod); strcat_s(b, n, t); }
    if (flags & DIEP_GAIN) { sprintf_s(t, "gain=%lu ", e->dwGain); strcat_s(b, n, t); }
    if ((flags & DIEP_STARTDELAY) && e->dwSize >= sizeof(DIEFFECT)) { sprintf_s(t, "delay=%lu ", e->dwStartDelay); strcat_s(b, n, t); }
    if (flags & DIEP_AXES) {
        sprintf_s(t, "axes=%lu[", e->cAxes); strcat_s(b, n, t);
        for (DWORD i = 0; e->rgdwAxes && i < e->cAxes && i < 4; ++i) { sprintf_s(t, "%s0x%lx", i ? "," : "", e->rgdwAxes[i]); strcat_s(b, n, t); }
        strcat_s(b, n, "] ");
    }
    if (flags & DIEP_DIRECTION) {
        sprintf_s(t, "dir(%s)=", (e->dwFlags & DIEFF_POLAR) ? "polar" : (e->dwFlags & DIEFF_SPHERICAL) ? "sph" : "cart"); strcat_s(b, n, t);
        for (DWORD i = 0; e->rglDirection && i < e->cAxes && i < 4; ++i) { sprintf_s(t, "%s%ld", i ? "," : "", e->rglDirection[i]); strcat_s(b, n, t); }
        strcat_s(b, n, " ");
    }
    if (flags & DIEP_ENVELOPE) {
        if (e->lpEnvelope) { sprintf_s(t, "env(att %lu/%lu fade %lu/%lu) ", e->lpEnvelope->dwAttackLevel, e->lpEnvelope->dwAttackTime, e->lpEnvelope->dwFadeLevel, e->lpEnvelope->dwFadeTime); strcat_s(b, n, t); }
        else strcat_s(b, n, "env(none) ");
    }
    if ((flags & DIEP_TYPESPECIFICPARAMS) && e->lpvTypeSpecificParams) {
        const DWORD cb = e->cbTypeSpecificParams;
        if (k == K_CONSTANT && cb >= sizeof(DICONSTANTFORCE)) { sprintf_s(t, "mag=%ld ", ((LPCDICONSTANTFORCE)e->lpvTypeSpecificParams)->lMagnitude); strcat_s(b, n, t); }
        else if (k == K_RAMP && cb >= sizeof(DIRAMPFORCE)) { auto r = (LPCDIRAMPFORCE)e->lpvTypeSpecificParams; sprintf_s(t, "ramp=%ld..%ld ", r->lStart, r->lEnd); strcat_s(b, n, t); }
        else if (k == K_PERIODIC && cb >= sizeof(DIPERIODIC)) {
            auto p = (LPCDIPERIODIC)e->lpvTypeSpecificParams;
            sprintf_s(t, "mag=%lu offset=%ld phase=%lu period=%lu ", p->dwMagnitude, p->lOffset, p->dwPhase, p->dwPeriod); strcat_s(b, n, t);
        } else if (k == K_CONDITION && cb >= sizeof(DICONDITION)) {
            for (DWORD i = 0; i < cb / sizeof(DICONDITION) && i < 2; ++i) {
                auto c = (LPCDICONDITION)e->lpvTypeSpecificParams + i;
                sprintf_s(t, "cond%lu(off=%ld +k=%ld -k=%ld +sat=%lu -sat=%lu dead=%ld) ", i, c->lOffset, c->lPositiveCoefficient, c->lNegativeCoefficient,
                          c->dwPositiveSaturation, c->dwNegativeSaturation, c->lDeadBand); strcat_s(b, n, t);
            }
        } else { sprintf_s(t, "typeparams %lu bytes ", cb); strcat_s(b, n, t); }
    }
}
LONG mainMagnitude(Kind k, LPCDIEFFECT e, DWORD flags, bool &has)
{
    has = false;
    if (!e || !(flags & DIEP_TYPESPECIFICPARAMS) || !e->lpvTypeSpecificParams) return 0;
    if (k == K_CONSTANT && e->cbTypeSpecificParams >= sizeof(DICONSTANTFORCE)) { has = true; return ((LPCDICONSTANTFORCE)e->lpvTypeSpecificParams)->lMagnitude; }
    if (k == K_PERIODIC && e->cbTypeSpecificParams >= sizeof(DIPERIODIC)) { has = true; return (LONG)((LPCDIPERIODIC)e->lpvTypeSpecificParams)->dwMagnitude; }
    return 0;
}
bool copyEffect(DIEFFECT &out, LPCDIEFFECT e)
{
    if (!e || e->dwSize < sizeof(DIEFFECT_DX5) || e->dwSize > sizeof(DIEFFECT)) return false;
    memset(&out, 0, sizeof out);
    memcpy(&out, e, e->dwSize);
    return true;
}

typedef HRESULT(STDMETHODCALLTYPE *SetParamsFn)(IDirectInputEffect *, LPCDIEFFECT, DWORD);
typedef HRESULT(STDMETHODCALLTYPE *StartFn)(IDirectInputEffect *, DWORD, DWORD);
typedef HRESULT(STDMETHODCALLTYPE *NoArgFn)(IDirectInputEffect *);
typedef ULONG(STDMETHODCALLTYPE *ReleaseFn)(IDirectInputEffect *);
typedef HRESULT(STDMETHODCALLTYPE *SendFfFn)(void *, DWORD);

HRESULT STDMETHODCALLTYPE hkSetParams(IDirectInputEffect *self, LPCDIEFFECT e, DWORD flags)
{
    EnterCriticalSection(&g_cs);
    EffRec *r = effRec(self);
    char d[400] = "";
    bool logIt = false, summary = false, gaveUp = false;
    DIEFFECT copy;
    TypeParams sent{};
    DIENVELOPE sentEnv{};
    LPCDIEFFECT out = e;
    DWORD outFlags = flags;
    if (r && r->emulate && e && (flags & (DIEP_GAIN | DIEP_TYPESPECIFICPARAMS | DIEP_ENVELOPE))) {
        if (flags & DIEP_GAIN) r->gain = e->dwGain;
        if ((flags & DIEP_TYPESPECIFICPARAMS) && e->lpvTypeSpecificParams) {
            DWORD nb = paramBytes(r->kind, e->cbTypeSpecificParams);
            if (nb) { memcpy(&r->params, e->lpvTypeSpecificParams, nb); r->bytes = nb; }
            else { r->emulate = false; gaveUp = true; }
        }
        if (flags & DIEP_ENVELOPE) { r->haveEnv = e->lpEnvelope != nullptr; if (e->lpEnvelope) r->env = *e->lpEnvelope; }
        if (r->emulate && r->bytes && copyEffect(copy, e)) {
            const DWORD g = combinedGain(r->gain, g_strength);
            sent = applyGain(r->kind, r->params, r->bytes, g);
            copy.lpvTypeSpecificParams = &sent;
            copy.cbTypeSpecificParams = r->bytes;
            outFlags |= DIEP_TYPESPECIFICPARAMS;
            if (flags & DIEP_GAIN) copy.dwGain = DI_FFNOMINALMAX;
            if (r->haveEnv && (flags & (DIEP_GAIN | DIEP_ENVELOPE))) { sentEnv = applyGain(r->env, g); copy.lpEnvelope = &sentEnv; outFlags |= DIEP_ENVELOPE; }
            out = &copy;
        }
    }
    if (r) {
        LONG calls = ++r->calls;
        bool has; LONG m = mainMagnitude(r->kind, e, flags, has);
        LONG sign = has ? (m > 0) - (m < 0) : r->lastSign;
        if (has) { r->minMag = calls == 1 ? m : (std::min)(r->minMag, m); r->maxMag = calls == 1 ? m : (std::max)(r->maxMag, m); }
        bool flip = r->kind == K_CONSTANT && has && sign && r->lastSign && sign != r->lastSign;
        if (has && sign) r->lastSign = sign;
        logIt = calls <= 40 || flip;
        ULONGLONG now = GetTickCount64();
        if (now - r->summaryAt >= 5000) { summary = true; r->summaryAt = now; }
        if (logIt) describe(d, sizeof d, r->kind, e, flags);
        if (summary) sprintf_s(d + strlen(d), sizeof d - strlen(d), "%s[5 s summary: %ld calls, magnitude %ld..%ld]", logIt ? " " : "", calls, r->minMag, r->maxMag);
        if (outFlags & DIEP_START) { r->running = true; r->iterations = 1; }
    }
    const int id = r ? r->id : -1;
    const char *name = r ? r->name : "?";
    LeaveCriticalSection(&g_cs);
    if ((logIt || summary) && diagOk())
        dlog("ffb: effect %d %s SetParameters flags=0x%lx%s %s%s", id, name, flags, (flags & DIEP_START) ? " (START)" : "", d,
             out != e ? " [sent with gain 10000, the game's gain and Strength applied]" : "");
    if (gaveUp && diagOk()) dlog("ffb: effect %d %s parameters of an unexpected size; gain emulation off for this effect", id, name);
    if ((outFlags & DIEP_START) && blocked()) { outFlags &= ~DIEP_START; InterlockedIncrement(&g_swallowed); }
    if (!outFlags) return DI_OK;
    HRESULT hr = ((SetParamsFn)orig(H_E_SETPARAMS, self))(self, out, outFlags);
    static volatile LONG failures;
    if (FAILED(hr) && InterlockedIncrement(&failures) <= 20 && diagOk()) dlog("ffb: effect %d %s SetParameters flags=0x%lx refused by the device: 0x%08lx", id, name, outFlags, (unsigned long)hr);
    return hr;
}
HRESULT STDMETHODCALLTYPE hkStart(IDirectInputEffect *self, DWORD iterations, DWORD flags)
{
    EnterCriticalSection(&g_cs);
    EffRec *r = effRec(self);
    if (r) { r->running = true; r->iterations = iterations; }
    const int id = r ? r->id : -1;
    const char *name = r ? r->name : "?";
    LeaveCriticalSection(&g_cs);
    const bool b = blocked();
    if (diagOk()) dlog("ffb: effect %d %s Start iterations=%lu flags=0x%lx%s", id, name, iterations, flags, g_opt.mute ? " (muted: not started)" : b ? " (FFB off: not started)" : "");
    if (b) { InterlockedIncrement(&g_swallowed); return DI_OK; }
    return ((StartFn)orig(H_E_START, self))(self, iterations, flags);
}
HRESULT STDMETHODCALLTYPE hkStop(IDirectInputEffect *self)
{
    EnterCriticalSection(&g_cs);
    EffRec *r = effRec(self);
    if (r) r->running = false;
    const int id = r ? r->id : -1;
    const char *name = r ? r->name : "?";
    LeaveCriticalSection(&g_cs);
    if (diagOk()) dlog("ffb: effect %d %s Stop", id, name);
    return ((NoArgFn)orig(H_E_STOP, self))(self);
}
HRESULT STDMETHODCALLTYPE hkDownload(IDirectInputEffect *self)
{
    // Download can start an effect that was set to start on download; blocked, it is not passed on.
    EnterCriticalSection(&g_cs);
    EffRec *r = effRec(self);
    const int id = r ? r->id : -1;
    const char *name = r ? r->name : "?";
    LeaveCriticalSection(&g_cs);
    const bool b = blocked();
    if (diagOk()) dlog("ffb: effect %d %s Download%s", id, name, g_opt.mute ? " (muted: skipped)" : b ? " (FFB off: skipped)" : "");
    if (b) return DI_OK;
    return ((NoArgFn)orig(H_E_DOWNLOAD, self))(self);
}
ULONG STDMETHODCALLTYPE hkRelease(IDirectInputEffect *self)
{
    ULONG n = ((ReleaseFn)orig(H_E_RELEASE, self))(self);
    if (n == 0) {   // a released effect frees its record, so the table never fills over a long session
        EnterCriticalSection(&g_cs);
        if (EffRec *r = effRec(self)) r->eff = nullptr;
        LeaveCriticalSection(&g_cs);
    }
    return n;
}

typedef HRESULT(STDMETHODCALLTYPE *CreateEffectFn)(void *, REFGUID, LPCDIEFFECT, LPDIRECTINPUTEFFECT *, LPUNKNOWN);
HRESULT STDMETHODCALLTYPE hkCreateEffect(void *self, REFGUID g, LPCDIEFFECT e, LPDIRECTINPUTEFFECT *out, LPUNKNOWN unk)
{
    // Creating (and downloading) an effect starts nothing; only Start and SetParameters with DIEP_START do.
    const Kind kind = kindOf(g);
    DIEFFECT copy;
    TypeParams own{}, sent{};
    DIENVELOPE sentEnv{};
    LPCDIEFFECT send = e;
    DWORD bytes = (g_opt.gainEmulation && e && e->lpvTypeSpecificParams) ? paramBytes(kind, e->cbTypeSpecificParams) : 0;
    const DWORD gain = e ? e->dwGain : DI_FFNOMINALMAX;
    if (bytes && copyEffect(copy, e)) {
        memcpy(&own, e->lpvTypeSpecificParams, bytes);
        const DWORD gg = combinedGain(gain, g_strength);
        sent = applyGain(kind, own, bytes, gg);
        copy.lpvTypeSpecificParams = &sent;
        copy.cbTypeSpecificParams = bytes;
        copy.dwGain = DI_FFNOMINALMAX;
        if (e->lpEnvelope) { sentEnv = applyGain(*e->lpEnvelope, gg); copy.lpEnvelope = &sentEnv; }
        send = &copy;
    } else bytes = 0;
    HRESULT hr = ((CreateEffectFn)orig(H_CREATEEFF, self))(self, g, send, out, unk);
    if (SUCCEEDED(hr)) g_wheelDev = self;
    char d[400];
    describe(d, sizeof d, kind, e, DIEP_ALLPARAMS);
    int id = -1;
    if (SUCCEEDED(hr) && out && *out) {
        patch(H_E_RELEASE, *out, 2, (void *)&hkRelease);
        patch(H_E_SETPARAMS, *out, 6, (void *)&hkSetParams);
        patch(H_E_START, *out, 7, (void *)&hkStart);
        patch(H_E_STOP, *out, 8, (void *)&hkStop);
        patch(H_E_DOWNLOAD, *out, 10, (void *)&hkDownload);
        EnterCriticalSection(&g_cs);
        int slot = -1;
        for (int i = 0; i < g_effCount; ++i) if (!g_effs[i].eff) { slot = i; break; }
        if (slot < 0 && g_effCount < 32) slot = g_effCount++;
        static int nextId;
        if (slot >= 0) {
            EffRec &r = g_effs[slot];
            r = EffRec{};
            r.eff = *out; r.kind = kind; r.name = effName(g); r.id = id = nextId++; r.summaryAt = GetTickCount64();
            r.emulate = g_opt.gainEmulation && kind != K_OTHER; r.gain = gain; r.params = own; r.bytes = bytes;
            r.haveEnv = e && e->lpEnvelope; if (r.haveEnv) r.env = *e->lpEnvelope;
        }
        LeaveCriticalSection(&g_cs);
        if (slot < 0) {
            // Fail closed: an effect the layer cannot track would escape FFB Off (and the gain emulation).
            (*out)->Release();
            *out = nullptr;
            dlog("ffb: CreateEffect %s refused: 32 live effects already tracked", effName(g));
            return DIERR_OUTOFMEMORY;
        }
    }
    if (diagOk()) dlog("ffb: CreateEffect %s -> 0x%08lx effect %d flags=0x%lx %s%s", effName(g), (unsigned long)hr, id, e ? e->dwFlags : 0, d,
                       send != e ? " [sent with gain 10000, the game's gain and Strength applied]" : "");
    return hr;
}

// ---- device calls (logged; FFB commands and panel input rule) ------------------------------------------------
typedef HRESULT(STDMETHODCALLTYPE *SetPropFn)(void *, REFGUID, LPCDIPROPHEADER);
HRESULT STDMETHODCALLTYPE hkSetProp(void *self, REFGUID prop, LPCDIPROPHEADER ph)
{
    ULONG_PTR id = (ULONG_PTR)&prop;
    // Auto-centring is a force too (the wheel's own spring): while FFB is off or muted it is never switched on.
    if (blocked() && ph && id == (ULONG_PTR)&DIPROP_AUTOCENTER && ph->dwSize >= sizeof(DIPROPDWORD) && ((LPCDIPROPDWORD)ph)->dwData != DIPROPAUTOCENTER_OFF) {
        if (diagOk()) dlog("ffb: SetProperty AUTOCENTER on refused while FFB is %s", g_opt.mute ? "muted" : "off");
        return DI_OK;
    }
    HRESULT hr = ((SetPropFn)orig(H_SETPROP, self))(self, prop, ph);
    if (ph && id <= 0xFFFF && diagOk()) {
        if (id == (ULONG_PTR)&DIPROP_RANGE && ph->dwSize >= sizeof(DIPROPRANGE))
            dlog("input: SetProperty RANGE obj=0x%lx how=%lu %ld..%ld -> 0x%08lx", ph->dwObj, ph->dwHow, ((LPCDIPROPRANGE)ph)->lMin, ((LPCDIPROPRANGE)ph)->lMax, (unsigned long)hr);
        else if (ph->dwSize >= sizeof(DIPROPDWORD))
            dlog("input: SetProperty %llu obj=0x%lx how=%lu value=%lu -> 0x%08lx (1 buffer 2 axismode 4 range 5 deadzone 6 saturation 7 ffgain 9 autocenter 10 calibration)",
                 (unsigned long long)id, ph->dwObj, ph->dwHow, ((LPCDIPROPDWORD)ph)->dwData, (unsigned long)hr);
    }
    return hr;
}
typedef HRESULT(STDMETHODCALLTYPE *DevNoArgFn)(void *);
HRESULT STDMETHODCALLTYPE hkAcquire(void *self)
{
    HRESULT hr = ((DevNoArgFn)orig(H_ACQUIRE, self))(self);
    static volatile LONG n;
    if ((InterlockedIncrement(&n) <= 20 || FAILED(hr)) && diagOk()) dlog("input: Acquire device %p -> 0x%08lx", self, (unsigned long)hr);
    return hr;
}
typedef HRESULT(STDMETHODCALLTYPE *SetCoopFn)(void *, HWND, DWORD);
HRESULT STDMETHODCALLTYPE hkSetCoop(void *self, HWND w, DWORD flags)
{
    HRESULT hr = ((SetCoopFn)orig(H_SETCOOP, self))(self, w, flags);
    if (diagOk()) dlog("input: SetCooperativeLevel device %p flags=0x%lx -> 0x%08lx", self, flags, (unsigned long)hr);
    return hr;
}
HRESULT STDMETHODCALLTYPE hkSendFf(void *self, DWORD cmd)
{
    if (diagOk()) dlog("ffb: SendForceFeedbackCommand 0x%lx (1 reset 2 stopall 4 pause 8 continue 0x10 actuators on 0x20 off)%s", cmd, g_opt.mute ? " (muted)" : "");
    return ((SendFfFn)orig(H_SENDFF, self))(self, cmd);
}
// Reads use the same format-checked input-only policy as the closed force fence.
int inputFormat(void *self)
{
    int layout = 0;
    EnterCriticalSection(&g_cs);
    for (int i = 0; i < g_devCount; ++i) if (g_devs[i].dev == self) { layout = g_devs[i].format; break; }
    LeaveCriticalSection(&g_cs);
    return layout;
}
void setInputFormat(void *self, int layout)
{
    EnterCriticalSection(&g_cs);
    for (int i = 0; i < g_devCount; ++i) if (g_devs[i].dev == self) g_devs[i].format = layout;
    LeaveCriticalSection(&g_cs);
}
typedef HRESULT(STDMETHODCALLTYPE *GetDataFn)(void *, DWORD, LPDIDEVICEOBJECTDATA, LPDWORD, DWORD);
HRESULT STDMETHODCALLTYPE hkGetData(void *self, DWORD cbObj, LPDIDEVICEOBJECTDATA rgdod, LPDWORD inOut, DWORD flags)
{
    HRESULT hr = ((GetDataFn)orig(H_GETDATA, self))(self, cbObj, rgdod, inOut, flags);
    if (SUCCEEDED(hr) && inOut && dbce::panel::IsOpen()) dbce::input::suppressData(inOut);
    return hr;
}
// Running test injections on this device replace its raw sample (before the game resolves its bindings). Axis values
// arrive in the binding's capture range and are rescaled to the range the game set on that axis; an axis whose range
// cannot be read is not delivered.
typedef HRESULT(STDMETHODCALLTYPE *GetPropFn)(void *, REFGUID, LPDIPROPHEADER);
void applyInjections(void *self, DWORD cb, LPVOID data)
{
    const bool two = cb == sizeof(DIJOYSTATE2);
    if (!data || (!two && cb != sizeof(DIJOYSTATE))) return;
    std::string inst;
    int format = 0;
    EnterCriticalSection(&g_cs);
    for (int i = 0; i < g_devCount; ++i) if (g_devs[i].dev == self) { inst = guidText(g_devs[i].instance); format = g_devs[i].format; break; }
    LeaveCriticalSection(&g_cs);
    if (inst.empty()) return;
    if (!((format == 1 && !two) || (format == 2 && two))) {   // a custom format: the fields are not the standard ones
        static volatile LONG warned;
        if (g_table.pendingFor(inst) && InterlockedIncrement(&warned) <= 3) dlog("inject: device %p reads with a data format that is not c_dfDIJoystick/2; not delivered", self);
        return;
    }
    auto s = (DIJOYSTATE *)data;
    dbce::controls::StateView v;
    v.axes = (long *)&s->lX;
    v.buttons = two ? ((DIJOYSTATE2 *)data)->rgbButtons : s->rgbButtons;
    v.buttonCount = two ? 128 : 32;
    v.povs = two ? (unsigned long *)((DIJOYSTATE2 *)data)->rgdwPOV : (unsigned long *)s->rgdwPOV;
    std::vector<std::string> lines;
    g_table.apply(inst, v, GetTickCount64(), [self](int axis, long &mn, long &mx) {
        static const DWORD ofs[8] = {DIJOFS_X, DIJOFS_Y, DIJOFS_Z, DIJOFS_RX, DIJOFS_RY, DIJOFS_RZ, DIJOFS_SLIDER(0), DIJOFS_SLIDER(1)};
        DIPROPRANGE r{};
        r.diph.dwSize = sizeof r; r.diph.dwHeaderSize = sizeof r.diph; r.diph.dwObj = ofs[axis]; r.diph.dwHow = DIPH_BYOFFSET;
        void **vt = *(void ***)self;
        if (FAILED(((GetPropFn)vt[5])(self, DIPROP_RANGE, &r.diph))) return false;
        mn = r.lMin; mx = r.lMax;
        return true;
    }, lines);
    for (auto &l : lines) dlog("%s", l.c_str());
}
// Pedal rest shapes (SetShapes) on this device, after any injection: by the instance the device reports.
void applyShapes(void *self, DWORD cb, LPVOID data, const dbce::controls::ShapeTable &shapes)
{
    std::string inst;
    int format = 0;
    EnterCriticalSection(&g_cs);
    for (int i = 0; i < g_devCount; ++i) if (g_devs[i].dev == self && g_devs[i].reportedOk) { inst = guidText(g_devs[i].reported); format = g_devs[i].format; break; }
    LeaveCriticalSection(&g_cs);
    if (inst.empty()) return;   // an unidentified device was counted once at CreateDevice
    dbce::shape::applyToState(shapes, inst, format, cb, data, [self](int axis, long &mn, long &mx) {
        static const DWORD ofs[8] = {DIJOFS_X, DIJOFS_Y, DIJOFS_Z, DIJOFS_RX, DIJOFS_RY, DIJOFS_RZ, DIJOFS_SLIDER(0), DIJOFS_SLIDER(1)};
        DIPROPRANGE rg{};
        rg.diph.dwSize = sizeof rg; rg.diph.dwHeaderSize = sizeof rg.diph; rg.diph.dwObj = ofs[axis]; rg.diph.dwHow = DIPH_BYOFFSET;
        void **vt = *(void ***)self;
        if (FAILED(((GetPropFn)vt[5])(self, DIPROP_RANGE, &rg.diph))) return false;
        mn = rg.lMin; mx = rg.lMax;
        return true;
    }, [](const dbce::controls::PedalShape &s, dbce::controls::ShapeMiss m, long mn, long mx) { dlog("%s", dbce::shape::firstLine(s, m, mn, mx).c_str()); });
}
typedef HRESULT(STDMETHODCALLTYPE *GetStateFn)(void *, DWORD, LPVOID);
HRESULT STDMETHODCALLTYPE hkGetState(void *self, DWORD cb, LPVOID data)
{
    HRESULT hr = ((GetStateFn)orig(H_GETSTATE, self))(self, cb, data);
    if (SUCCEEDED(hr) && g_injectOk && g_table.any()) applyInjections(self, cb, data);
    if (SUCCEEDED(hr) && data && g_shapes) applyShapes(self, cb, data, *g_shapes);
    if (SUCCEEDED(hr) && data && dbce::panel::IsOpen()) dbce::input::suppressState(inputFormat(self), cb, data);
    if (SUCCEEDED(hr) && data && (cb == sizeof(DIJOYSTATE) || cb == sizeof(DIJOYSTATE2))) {   // the first five reads, then every 10 s
        static volatile LONG n;
        static ULONGLONG last;
        LONG k = InterlockedIncrement(&n);
        ULONGLONG now = GetTickCount64();
        if (k <= 5 || now - last >= 10000) {
            last = now;
            auto s = (const DIJOYSTATE *)data;
            if (diagOk()) dlog("input: device %p state X=%ld Y=%ld Z=%ld Rx=%ld Ry=%ld Rz=%ld sliders=%ld,%ld pov=%lu buttons0-7=%d%d%d%d%d%d%d%d", self,
                               s->lX, s->lY, s->lZ, s->lRx, s->lRy, s->lRz, s->rglSlider[0], s->rglSlider[1], s->rgdwPOV[0],
                               !!s->rgbButtons[0], !!s->rgbButtons[1], !!s->rgbButtons[2], !!s->rgbButtons[3], !!s->rgbButtons[4], !!s->rgbButtons[5], !!s->rgbButtons[6], !!s->rgbButtons[7]);
        }
    }
    return hr;
}

typedef HRESULT(STDMETHODCALLTYPE *SetFmtFn)(void *, LPCDIDATAFORMAT);
HRESULT STDMETHODCALLTYPE hkSetFormat(void *self, LPCDIDATAFORMAT f)
{
    HRESULT hr = ((SetFmtFn)orig(H_SETFMT, self))(self, f);
    if (SUCCEEDED(hr)) setInputFormat(self, dbce::input::format(f));
    return hr;
}

typedef HRESULT(STDMETHODCALLTYPE *SetActionFn)(void *, void *, const void *, DWORD);
HRESULT STDMETHODCALLTYPE hkSetAction(void *self, void *f, const void *user, DWORD flags)
{
    const HRESULT hr = ((SetActionFn)orig(H_SETACTION, self))(self, f, user, flags);
    if (SUCCEEDED(hr)) setInputFormat(self, 0);   // the action map establishes its own layout
    return hr;
}

// A released device leaves the table, so the game's later devices still get injection and pedal shaping (the table
// holds 32 live devices). Only the object's address is used after the real Release, never the object.
typedef ULONG(STDMETHODCALLTYPE *DevReleaseFn)(void *);
ULONG STDMETHODCALLTYPE hkDevRelease(void *self)
{
    // The record's generation before the real Release: if the object is freed and its address is handed to a new device
    // (another thread, or inside this Release) before the cleanup below, that new record has another generation and stays.
    unsigned gen = 0;
    EnterCriticalSection(&g_cs);
    for (int i = 0; i < g_devCount; ++i) if (g_devs[i].dev == self) { gen = g_devs[i].gen; break; }
    LeaveCriticalSection(&g_cs);
    const ULONG n = ((DevReleaseFn)orig(H_D_RELEASE, self))(self);
    if (n == 0 && gen) {
        EnterCriticalSection(&g_cs);
        for (int i = 0; i < g_devCount; ++i)
            if (g_devs[i].dev == self && g_devs[i].gen == gen) { g_devs[i] = g_devs[--g_devCount]; break; }
        LeaveCriticalSection(&g_cs);
    }
    return n;
}

typedef HRESULT(STDMETHODCALLTYPE *CreateDevFn)(void *, REFGUID, void **, LPUNKNOWN);
HRESULT STDMETHODCALLTYPE hkCreateDevice(void *self, REFGUID g, void **out, LPUNKNOWN unk)
{
    HRESULT hr = ((CreateDevFn)orig(H_CREATEDEV, self))(self, g, out, unk);
    if (SUCCEEDED(hr) && out && *out) {
        HookDevice(*out);
        // The instance the device reports (GetDeviceInfo, slot 15; A and W differ only in the strings after
        // guidInstance, so a W-sized buffer is tried with the A size first, then the W size). Pedal shapes match it.
        GUID reported{};
        bool reportedOk = false;
        if (g_shapes) {
            HRESULT ih=reportedIdentity(*out,reported);
            if (SUCCEEDED(ih)) { reportedOk = true; }
            else {
                g_shapes->noteUnidentified();
                dlog("shape: CreateDevice: the device's own instance could not be read (0x%08lx); any pedal rest correction it needs will not apply", ih);
            }
        }
        EnterCriticalSection(&g_cs);   // the instance the game asked for, for test injection by instance
        int slot = -1;
        for (int i = 0; i < g_devCount; ++i) if (g_devs[i].dev == *out) slot = i;
        if (slot < 0 && g_devCount < 32) slot = g_devCount++;
        if (slot >= 0) { if (++g_devGen == 0) ++g_devGen; g_devs[slot] = {*out, g, 0, reported, reportedOk, g_devGen}; }   // 0 is never a generation
        LeaveCriticalSection(&g_cs);
        if (slot < 0) {
            if (g_shapes && reportedOk && g_shapes->countFor(guidText(reported))) g_shapes->noteUnidentified();
            dlog("ffb: CreateDevice: 32 devices are already open; this one gets no test injection or pedal rest correction");
        }
    }
    return hr;
}

bool saveIni(const char *key, const char *value, const char *failure = "Settings could not be saved")
{
    if (!g_opt.iniPath) return true;   // explicitly memory-only: no persistence is requested
    if (dbce::settings::saveKey(g_opt.iniPath, "FFB", key, value)) {
        dbce::panel::NoteSaved(true);
        return true;
    }
    dbce::panel::NoteSaved(false, failure);
    dlog("ffb: could not save [FFB] %s (%lu)", key, GetLastError());
    return false;
}
} // namespace

void Init(const Options &o)
{
    ensure();
    g_opt = o;
    if (o.iniPath) {
        g_on = GetPrivateProfileIntA("FFB", "Enabled", o.onByDefault ? 1 : 0, o.iniPath) != 0;
        g_strength = (std::min)(100, (std::max)(0, (int)GetPrivateProfileIntA("FFB", "Strength", 50, o.iniPath)));
    }
    dlog("ffb: force feedback %s%s%s", g_on ? "on" : "off", o.gainEmulation ? "" : " (the game's own effects, unchanged)",
         o.mute ? "; MUTED by the test switch" : "");
    if (o.gainEmulation) dlog("ffb: strength %ld%% (50%% = the game's own level), effect gain applied by the mod", (long)g_strength);
}

void HookDirectInput(void *di)
{
    ensure();
    if (di) patch(H_CREATEDEV, di, 3, (void *)&hkCreateDevice);
}

void HookDevice(void *dev)
{
    ensure();
    if (!dev) return;
    patch(H_SETPROP, dev, 6, (void *)&hkSetProp);
    patch(H_ACQUIRE, dev, 7, (void *)&hkAcquire);
    patch(H_GETSTATE, dev, 9, (void *)&hkGetState);
    patch(H_GETDATA, dev, 10, (void *)&hkGetData);
    patch(H_SETFMT, dev, 11, (void *)&hkSetFormat);
    patch(H_SETACTION, dev, 30, (void *)&hkSetAction);
    patch(H_SETCOOP, dev, 13, (void *)&hkSetCoop);
    patch(H_CREATEEFF, dev, 18, (void *)&hkCreateEffect);
    patch(H_SENDFF, dev, 22, (void *)&hkSendFf);
    patch(H_D_RELEASE, dev, 2, (void *)&hkDevRelease);
}

bool SetShapes(const dbce::controls::ShapeTable *table)
{
    ensure();
    if (!table || !table->any()) return false;
    if (InterlockedCompareExchangePointer((PVOID volatile *)&g_shapes, (PVOID)table, nullptr) != nullptr) {
        dlog("shape: a pedal shape table is already set for this process; the new one is ignored");
        return false;
    }
    for (auto &s : table->shapes())
        dlog("shape: %s axis %d (%s): rest %ld of %ld..%ld, travel %+d%s, shaped in GetDeviceState", s.instance.c_str(), s.axis, s.action.c_str(),
             s.binding.rest, s.binding.min, s.binding.max, s.binding.travel, s.binding.inverted ? ", inverted" : "");
    return true;
}

void AllowInjection(bool on)
{
    ensure();
    InterlockedExchange(&g_injectOk, on ? 1 : 0);
    if (on) dlog("inject: test injection is ON (inject.on at start): raw samples may replace the wheel's for a test run");
}

bool ParseGuid(const char *s, GUID *out)
{
    // "{xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx}" (no ole32 in a proxy)
    if (!s || !out || strlen(s) != 38 || s[0] != '{' || s[37] != '}') return false;
    auto hex = [](const char *p, int n, unsigned long long &v) {
        v = 0;
        for (int i = 0; i < n; ++i) {
            char c = p[i];
            int d = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
            if (d < 0) return false;
            v = v * 16 + (unsigned)d;
        }
        return true;
    };
    unsigned long long a, b, c, d, e;
    if (s[9] != '-' || s[14] != '-' || s[19] != '-' || s[24] != '-' || !hex(s + 1, 8, a) || !hex(s + 10, 4, b) || !hex(s + 15, 4, c) || !hex(s + 20, 4, d) || !hex(s + 25, 12, e)) return false;
    out->Data1 = (unsigned long)a; out->Data2 = (unsigned short)b; out->Data3 = (unsigned short)c;
    out->Data4[0] = (unsigned char)(d >> 8); out->Data4[1] = (unsigned char)d;
    for (int i = 0; i < 6; ++i) out->Data4[2 + i] = (unsigned char)(e >> (8 * (5 - i)));
    return true;
}

bool Inject(const GUID &instance, int kind, int index, long value, long bindMin, long bindMax, DWORD ms, char *why, size_t whyLen)
{
    ensure();
    auto fail = [&](const char *m) { if (why && whyLen) strcpy_s(why, whyLen, m); return false; };
    if (!g_injectOk) return fail("test injection is off (no inject.on at game start)");
    bool opened = false;
    EnterCriticalSection(&g_cs);
    for (int i = 0; i < g_devCount; ++i) if (g_devs[i].instance == instance) opened = true;
    LeaveCriticalSection(&g_cs);
    if (!opened) return fail("the game has not opened that device instance");
    dbce::controls::InjectCommand c;
    c.binding.kind = kind == 1 ? dbce::controls::Kind::Axis : kind == 2 ? dbce::controls::Kind::Button : kind == 3 ? dbce::controls::Kind::Hat : dbce::controls::Kind::None;
    c.binding.index = index;
    c.binding.dev = guidText(instance);
    c.binding.min = bindMin; c.binding.max = bindMax;
    c.raw = value;
    c.ms = (int)ms;
    std::string r = g_table.add(c, GetTickCount64());
    if (!r.empty()) return fail(r.c_str());
    if (why && whyLen) why[0] = 0;
    return true;
}
bool On() { return g_on != 0; }
bool Muted() { return g_opt.mute; }

void StopForExit()
{
    ensure();
    InterlockedExchange(&g_on, 0);
    IDirectInputEffect *items[32];
    int n = 0;
    void *wheel = nullptr;
    EnterCriticalSection(&g_cs);
    for (int i = 0; i < g_effCount && n < 32; ++i) if (g_effs[i].eff) items[n++] = (IDirectInputEffect *)g_effs[i].eff;
    for (int i = 0; i < g_devCount; ++i) if (g_devs[i].dev == g_wheelDev) wheel = g_wheelDev;   // only a device still alive
    LeaveCriticalSection(&g_cs);
    dlog("ffb: stopped for exit (%d effects%s); not saved", n, wheel ? ", wheel STOPALL" : "");
    for (int i = 0; !g_opt.mute && i < n; ++i) ((NoArgFn)orig(H_E_STOP, items[i]))(items[i]);
    if (!g_opt.mute && wheel) ((SendFfFn)orig(H_SENDFF, wheel))(wheel, DISFFC_STOPALL);
}
int Strength() { return g_strength; }

void SetOn(bool on)
{
    ensure();
    // Enabling must be confirmed before forces resume. Off is a safety action: block and stop first,
    // then attempt persistence; a failed write must never restore the previous enabled state.
    if (on && !saveIni("Enabled", "1")) return;
    InterlockedExchange(&g_on, on ? 1 : 0);
    struct Item { IDirectInputEffect *eff; DWORD iterations; bool running; } items[32];
    int n = 0;
    EnterCriticalSection(&g_cs);
    for (int i = 0; i < g_effCount; ++i) if (g_effs[i].eff) items[n++] = {(IDirectInputEffect *)g_effs[i].eff, g_effs[i].iterations, g_effs[i].running};
    LeaveCriticalSection(&g_cs);
    dlog("ffb: %s from the F6 panel (%d effects)%s", on ? "on" : "off", n, g_opt.mute ? "; the test switch keeps forces muted" : "");
    for (int i = 0; !g_opt.mute && i < n; ++i) {
        if (!on) ((NoArgFn)orig(H_E_STOP, items[i].eff))(items[i].eff);
        else if (items[i].running) ((StartFn)orig(H_E_START, items[i].eff))(items[i].eff, items[i].iterations ? items[i].iterations : 1, 0);
    }
    if (!g_opt.mute && !on && g_wheelDev) ((SendFfFn)orig(H_SENDFF, g_wheelDev))(g_wheelDev, DISFFC_STOPALL);
    if (!on) saveIni("Enabled", "0", "FFB stopped; Off could not be saved");
}

void SetStrength(int percent)
{
    ensure();
    percent = (std::min)(100, (std::max)(0, percent));
    char v[16];
    sprintf_s(v, "%d", percent);
    if (!saveIni("Strength", v)) return;
    InterlockedExchange(&g_strength, percent);
    struct Item { IDirectInputEffect *eff; Kind kind; TypeParams params; DWORD bytes; DWORD gain; } items[32];
    int n = 0;
    EnterCriticalSection(&g_cs);
    for (int i = 0; i < g_effCount; ++i)
        if (g_effs[i].eff && g_effs[i].emulate && g_effs[i].bytes)
            items[n++] = {(IDirectInputEffect *)g_effs[i].eff, g_effs[i].kind, g_effs[i].params, g_effs[i].bytes, g_effs[i].gain};
    LeaveCriticalSection(&g_cs);
    for (int i = 0; i < n; ++i) {   // every tracked effect sent again at the new level
        TypeParams sent = applyGain(items[i].kind, items[i].params, items[i].bytes, combinedGain(items[i].gain, percent));
        DIEFFECT e = {};
        e.dwSize = sizeof(DIEFFECT);
        e.cbTypeSpecificParams = items[i].bytes;
        e.lpvTypeSpecificParams = &sent;
        ((SetParamsFn)orig(H_E_SETPARAMS, items[i].eff))(items[i].eff, &e, DIEP_TYPESPECIFICPARAMS);
    }
}

void DrawPanelPage(bool advanced)
{
    using namespace dbce::panel;
    ensure();
    BeginGroup("ffb");
    bool on = g_on != 0;
    if (OffOn("FFB", &on, "Force feedback from the game to your wheel. F8 stops it at any time.")) SetOn(on);
    if (g_opt.gainEmulation) {
        int s = g_strength;
        if (Percent("Strength", &s, 0, 100, 50, "50% is the game's own strength; 100% doubles it, capped at the wheel's maximum.")) SetStrength(s);
    }
    int created = 0, running = 0;
    EnterCriticalSection(&g_cs);
    for (int i = 0; i < g_effCount; ++i) if (g_effs[i].eff) { ++created; if (g_effs[i].running) ++running; }
    LeaveCriticalSection(&g_cs);
    char b[160];
    if (g_opt.mute) Status("Status", "Muted by the test switch: nothing reaches the wheel", Tone::Warning);
    else if (!g_on) Status("Status", "Off: nothing reaches the wheel");
    else if (!created) Status("Status", "Waiting for the game to create its effects");
    else { sprintf_s(b, "Active: %d of %d effects running", running, created); Status("Status", b, Tone::Good); }
    EndGroup();
    if (!advanced) return;
    BeginGroup("ffb-effects");
    Heading("Effects the game uses");
    EnterCriticalSection(&g_cs);
    for (int i = 0; i < g_effCount; ++i) {
        if (!g_effs[i].eff) continue;
        if (g_effs[i].emulate) sprintf_s(b, "%s; the game's gain %lu%%, sent at %lu%%", g_effs[i].running ? "running" : "stopped", g_effs[i].gain / 100, combinedGain(g_effs[i].gain, g_strength) / 100);
        else sprintf_s(b, "%s", g_effs[i].running ? "running" : "stopped");
        Status(g_effs[i].name, b);
    }
    LeaveCriticalSection(&g_cs);
    if (!created) Text("None yet.");
    if (g_opt.wheelNote) Text(g_opt.wheelNote);
    EndGroup();
}

void EffectSummary(int *created, int *running, char *list, size_t listSize)
{
    ensure();
    int c = 0, r = 0;
    if (list && listSize) list[0] = 0;
    EnterCriticalSection(&g_cs);
    for (int i = 0; i < g_effCount; ++i) {
        if (!g_effs[i].eff) continue;
        ++c;
        if (g_effs[i].running) ++r;
        if (!list || listSize < 2) continue;
        const size_t used = strlen(list);
        char b[192];
        if (g_effs[i].emulate) sprintf_s(b, "%s%s: %s; the game's gain %lu%%, sent at %lu%%", used ? "; " : "", g_effs[i].name,
                                         g_effs[i].running ? "running" : "stopped", g_effs[i].gain / 100, combinedGain(g_effs[i].gain, g_strength) / 100);
        else sprintf_s(b, "%s%s: %s", used ? "; " : "", g_effs[i].name, g_effs[i].running ? "running" : "stopped");
        strncpy_s(list + used, listSize - used, b, _TRUNCATE);
    }
    LeaveCriticalSection(&g_cs);
    if (created) *created = c;
    if (running) *running = r;
}

}} // namespace dbce::ffb
