// FMOD Studio parameter tap.
//
// Milestone's UE4 games drive the engine audio through FMOD Studio, setting
// parameters like RPM and Load every frame via
//     FMOD::Studio::EventInstance::setParameterValue(const char*, float)
// which the shipping exe imports from fmodstudio64.dll. Hooking that one
// import gives the values with no memory scanning at all.
//
// The import is DELAY-LOADED, which changes two things:
//   - the hook is written into the delay-load IAT, not the normal one
//   - the slot's original content is the resolver thunk, not the function.
//     Calling the thunk would resolve the import AND overwrite our slot with
//     the real address, silently unhooking us. So the real function is
//     resolved by GetProcAddress once the DLL is loaded, and the thunk is
//     only used as a last resort, after which the hook is re-installed.
//
// Parameters are per event instance and unnamed at this level beyond their
// string, so discovery mode logs every distinct name seen with its range;
// the ini then names which ones are rpm/load/speed.
#include "common.h"

static const char *kMangled = "?setParameterValue@EventInstance@Studio@FMOD@@QEAA?AW4FMOD_RESULT@@PEBDM@Z";
static const char *kDll = "fmodstudio64.dll";

typedef int (*PFN_SetParam)(void *self, const char *name, float value);
static PFN_SetParam g_real = nullptr;
static void **g_slot = nullptr;       // delay-load IAT slot we patched
static void *g_thunk = nullptr;       // what it held before

struct Seen { char name[48]; float lo, hi, last; uint32_t n; };
static Seen g_seen[128];
static int  g_seenN = 0;
static CRITICAL_SECTION g_lock;

static void writeSlot(void **slot, void *val)
{
    DWORD old;
    if (VirtualProtect(slot, sizeof(void *), PAGE_READWRITE, &old)) {
        *slot = val;
        VirtualProtect(slot, sizeof(void *), old, &old);
    }
}

static int HookSetParam(void *self, const char *name, float value)
{
    if (name) {
        ++g_fmod.calls;
        if (_stricmp(name, g_cfg.fmodRpm) == 0)            { g_fmod.rpm = value; g_fmod.observedAt[0] = GetTickCount64(); g_fmod.live = true; }
        else if (_stricmp(name, g_cfg.fmodSpeed) == 0)     { g_fmod.speed = value; g_fmod.observedAt[1] = GetTickCount64(); g_fmod.live = true; }
        else if (_stricmp(name, g_cfg.fmodLoad) == 0)      { g_fmod.load = value; g_fmod.observedAt[2] = GetTickCount64(); }
        else if (_stricmp(name, g_cfg.fmodLatSlip) == 0)   { g_fmod.latSlip = value; g_fmod.observedAt[3] = GetTickCount64(); }
        else if (_stricmp(name, g_cfg.fmodLongSlip) == 0)  { g_fmod.longSlip = value; g_fmod.observedAt[4] = GetTickCount64(); }
        else if (_stricmp(name, g_cfg.fmodSusp) == 0)      { g_fmod.susp = value; g_fmod.observedAt[5] = GetTickCount64(); }
        else if (_stricmp(name, g_cfg.fmodBraking) == 0)   { g_fmod.braking = value; g_fmod.observedAt[6] = GetTickCount64(); }
        else if (_stricmp(name, g_cfg.fmodImpact) == 0) {
            // Only a rising edge past the noise floor counts as a new hit;
            // the parameter is also set to 0 to arm the next one.
            float prev = g_fmod.impact.exchange(value); g_fmod.observedAt[7] = GetTickCount64();
            if (value > 0.05f && value > prev) {
                ++g_fmod.impactSeq;
                logf("[impact] intensity %.3f (vel %.3f)", value, g_fmod.impactVel.load());
            }
        }
        else if (_stricmp(name, g_cfg.fmodImpactVel) == 0) { g_fmod.impactVel = value; g_fmod.observedAt[8] = GetTickCount64(); }
        if (g_cfg.fmodDiscover) {
            EnterCriticalSection(&g_lock);
            int i = 0;
            for (; i < g_seenN; ++i) if (strcmp(g_seen[i].name, name) == 0) break;
            if (i == g_seenN && g_seenN < 128) {
                strncpy(g_seen[i].name, name, 47); g_seen[i].name[47] = 0;
                g_seen[i].lo = g_seen[i].hi = value; g_seen[i].n = 0; ++g_seenN;
                logf("[fmod] new parameter: \"%s\" = %g", name, value);
            }
            if (i < g_seenN) {
                Seen &s = g_seen[i];
                if (value < s.lo) s.lo = value;
                if (value > s.hi) s.hi = value;
                s.last = value; ++s.n;
            }
            LeaveCriticalSection(&g_lock);
        }
    }
    if (!g_real) {
        HMODULE m = GetModuleHandleA(kDll);
        if (m) g_real = (PFN_SetParam)GetProcAddress(m, kMangled);
    }
    if (g_real) return g_real(self, name, value);
    // DLL somehow not loaded yet: let the resolver do its job, then re-hook.
    int r = ((PFN_SetParam)g_thunk)(self, name, value);
    if (g_slot && *g_slot != (void *)HookSetParam) {
        g_real = (PFN_SetParam)*g_slot;
        writeSlot(g_slot, (void *)HookSetParam);
    }
    return r;
}

void fmodDumpDiscovery()
{
    if (!g_cfg.fmodDiscover || !g_seenN) return;
    EnterCriticalSection(&g_lock);
    logf("[fmod] %d distinct parameters, %u calls:", g_seenN, (unsigned)g_fmod.calls.load());
    for (int i = 0; i < g_seenN; ++i)
        logf("[fmod]   %-32s last=%-10g range %g..%g  (%u)", g_seen[i].name, g_seen[i].last, g_seen[i].lo,
             g_seen[i].hi, g_seen[i].n);
    LeaveCriticalSection(&g_lock);
}

// Walk the main module's delay-load descriptor table and patch the slot for
// our import. Layout is ImgDelayDescr from delayimp.h; grAttrs bit 0 means
// every field is an RVA.
struct DelayDescr { DWORD attrs, name, hmod, iat, intab, boundIat, unloadIat, ts; };

void fmodTapInstall()
{
    InitializeCriticalSection(&g_lock);
    BYTE *base = (BYTE *)GetModuleHandleA(nullptr);
    auto *dos = (IMAGE_DOS_HEADER *)base;
    auto *nt = (IMAGE_NT_HEADERS64 *)(base + dos->e_lfanew);
    IMAGE_DATA_DIRECTORY dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT];
    if (!dir.VirtualAddress) { logf("[fmod] no delay-load table"); return; }
    for (auto *d = (DelayDescr *)(base + dir.VirtualAddress); d->name; ++d) {
        if (!(d->attrs & 1)) continue;
        const char *dll = (const char *)(base + d->name);
        if (_stricmp(dll, kDll) != 0) continue;
        auto *intab = (IMAGE_THUNK_DATA64 *)(base + d->intab);
        auto *iat = (IMAGE_THUNK_DATA64 *)(base + d->iat);
        for (int i = 0; intab[i].u1.AddressOfData; ++i) {
            if (intab[i].u1.Ordinal & IMAGE_ORDINAL_FLAG64) continue;
            auto *ibn = (IMAGE_IMPORT_BY_NAME *)(base + intab[i].u1.AddressOfData);
            if (strcmp(ibn->Name, kMangled) != 0) continue;
            g_slot = (void **)&iat[i].u1.Function;
            g_thunk = *g_slot;
            HMODULE m = GetModuleHandleA(kDll);
            if (m) g_real = (PFN_SetParam)GetProcAddress(m, kMangled);
            writeSlot(g_slot, (void *)HookSetParam);
            logf("[fmod] hooked setParameterValue (slot %p, dll %s)", (void *)g_slot, m ? "loaded" : "not yet loaded");
            return;
        }
        logf("[fmod] %s found but setParameterValue not in its import list", dll);
        return;
    }
    logf("[fmod] %s is not a delay-load import of this exe", kDll);
}
