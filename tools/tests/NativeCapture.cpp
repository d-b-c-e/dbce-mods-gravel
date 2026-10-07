// Actual proxy hooks against fake COM calls. No DirectInput creation/devices.
#include "../../src/proxy.cpp"
#include <cassert>
#include <iostream>

Config g_cfg; InputState g_input; FfbState g_ffb; FmodState g_fmod; Ue4State g_ue4;
static bool mutedMode = true;
bool recordingMuted() { return mutedMode; }
void recordingInit() {} void recordingPoll() {} void loadConfig() {} void tripleReport() {}
void tripleAttach() {} void fmodTapInstall() {} void telemetryStart() {}
void logf(const char*, ...) {}
bool safeRead(const void* p, void* out, size_t n) { memcpy(out,p,n); return true; }

struct FakeObject { void** vt; };
static DWORD createdGain = 999, sentGain = 999, sentFlags = 0, autocentre = 1, deviceGain = 10000;
static int creates=0, sends=0, starts=0, acquires=0, commands=0, escapes=0, releases=0;
static LONG observedMagnitude = 0;
static bool failProperty=false;
static HRESULT STDMETHODCALLTYPE params(void*, LPCDIEFFECT e, DWORD flags) {
    ++sends; sentGain=e->dwGain; sentFlags=flags;
    if (e->lpvTypeSpecificParams) observedMagnitude=((DICONSTANTFORCE*)e->lpvTypeSpecificParams)->lMagnitude;
    return DI_OK;
}
static HRESULT STDMETHODCALLTYPE start(void*, DWORD, DWORD) { ++starts; return DI_OK; }
static HRESULT STDMETHODCALLTYPE escape(void*, LPDIEFFESCAPE) { ++escapes; return DI_OK; }
static ULONG STDMETHODCALLTYPE release(void*) { ++releases; return 0; }
static void* effectVt[13] = {nullptr,nullptr,(void*)release,nullptr,nullptr,nullptr,(void*)params,(void*)start,nullptr,nullptr,nullptr,nullptr,(void*)escape};
static FakeObject effect{effectVt};
static HRESULT STDMETHODCALLTYPE create(void*, REFGUID, LPCDIEFFECT e, LPDIRECTINPUTEFFECT* out, LPUNKNOWN) {
    ++creates; createdGain=e ? e->dwGain : 0; *out=(LPDIRECTINPUTEFFECT)&effect; return DI_OK;
}
static HRESULT STDMETHODCALLTYPE caps(void*, LPDIDEVCAPS c) { c->dwFlags=DIDC_FORCEFEEDBACK; return DI_OK; }
static HRESULT STDMETHODCALLTYPE prop(void*, REFGUID p, LPCDIPROPHEADER v) {
    if (failProperty) return DIERR_GENERIC;
    if (&p == &DIPROP_AUTOCENTER) autocentre=((DIPROPDWORD*)v)->dwData;
    if (&p == &DIPROP_FFGAIN) deviceGain=((DIPROPDWORD*)v)->dwData;
    return DI_OK;
}
static HRESULT STDMETHODCALLTYPE acquire(void*) { ++acquires; return DI_OK; }
static HRESULT STDMETHODCALLTYPE command(void*, DWORD) { ++commands; return DI_OK; }

int main() {
    InitializeCriticalSection(&g_devLock);
    void* device=(void*)0x1234; trackDev(device,true);
    g_oCreateEffW=(void*)create; g_oSetPropW=(void*)prop; g_oGetCapsW=(void*)caps; g_oAcquireW=(void*)acquire;
    g_oCommandW=(void*)command; g_oEscapeW=(void*)escape;
    assert(SUCCEEDED(HookAcquireW(device)) && acquires==1 && autocentre==0 && deviceGain==0);
    failProperty=true; assert(FAILED(HookAcquireW(device)) && acquires==1); failProperty=false;
    DIPROPDWORD p{}; p.diph={sizeof p,sizeof p.diph,0,DIPH_DEVICE}; p.dwData=10000;
    assert(SUCCEEDED(HookSetPropW(device,DIPROP_FFGAIN,&p.diph)) && deviceGain==0 && p.dwData==10000);
    p.dwData=1; assert(SUCCEEDED(HookSetPropW(device,DIPROP_AUTOCENTER,&p.diph)) && autocentre==0 && p.dwData==1);
    DICONSTANTFORCE constant{7500}; DWORD axis=DIJOFS_X; LONG direction=1;
    DIEFFECT e{}; e.dwSize=sizeof e; e.dwGain=8000; e.cAxes=1; e.rgdwAxes=&axis; e.rglDirection=&direction;
    e.cbTypeSpecificParams=sizeof constant; e.lpvTypeSpecificParams=&constant;
    LPDIRECTINPUTEFFECT out=nullptr;
    assert(SUCCEEDED(HookCreateEffW(device,GUID_ConstantForce,&e,&out,nullptr)) && creates==1 && createdGain==0);
    assert(std::abs(g_ffb.constant.load() - .6f) < .00001f && e.dwGain==8000);
    auto set=(PFN_SetParams)effectVt[6];
    constant.lMagnitude=-5000;
    assert(SUCCEEDED(set(&effect,&e,DIEP_TYPESPECIFICPARAMS|DIEP_START)) && sentGain==0 && !(sentFlags&DIEP_START));
    assert(std::abs(g_ffb.constant.load() + .4f) < .00001f && observedMagnitude==-5000);
    assert(SUCCEEDED(((HRESULT (STDMETHODCALLTYPE*)(void*,DWORD,DWORD))effectVt[7])(&effect,1,0)) && starts==0);
    assert(FAILED(((HRESULT (STDMETHODCALLTYPE*)(void*,LPDIEFFESCAPE))effectVt[12])(&effect,nullptr)) && escapes==0);
    assert(SUCCEEDED(HookCommandW(device,DISFFC_SETACTUATORSON)) && commands==0);
    assert(FAILED(HookDeviceEscapeW(device,nullptr)) && escapes==0);
    // Guard even a non-selected FFB device and an effect table at capacity.
    assert(SUCCEEDED(HookCreateEffW((void*)0x9999,GUID_ConstantForce,&e,&out,nullptr)) && createdGain==0);
    for (auto& t:g_effs) t.eff=(void*)0x7777;
    assert(SUCCEEDED(HookCreateEffW(device,GUID_ConstantForce,&e,&out,nullptr)) && g_ffb.tableOverflow==1);
    assert(SUCCEEDED(set(&effect,&e,DIEP_GAIN|DIEP_START)) && sentGain==0 && !(sentFlags&DIEP_START));
    // Normal sessions retain native calls and original gains/flags.
    mutedMode=false;
    assert(SUCCEEDED(set(&effect,&e,DIEP_GAIN|DIEP_START)) && sentGain==8000 && (sentFlags&DIEP_START));
    assert(SUCCEEDED(HookEffectStart(&effect,1,0)) && starts==1);
    assert(SUCCEEDED(HookCommandW(device,DISFFC_SETACTUATORSON)) && commands==1);
    assert(SUCCEEDED(HookDeviceEscapeW(device,nullptr)) && escapes==1);
    assert(SUCCEEDED(HookSetPropW(device,DIPROP_FFGAIN,&p.diph)) && deviceGain==1);
    std::cout << "PASS actual proxy hooks: original observations, muted effect/device calls, capacity, normal passthrough\n";
}
