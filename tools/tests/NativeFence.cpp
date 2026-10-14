// The STD-033 fence over Gravel's own hooked DirectInput (sana 107988), with SDK interfaces and no hardware: the switch
// rules, the re-type surviving the wrapper, no force route reaching the device, and the read tap observing exactly once per
// read, after test injection.
#include "../../src/proxy.cpp"
#include <cassert>
#include <iostream>
#include <string>

Config g_cfg; InputState g_input; FfbState g_ffb; FmodState g_fmod; Ue4State g_ue4;
static bool recording = false;
bool recordingMuted() { return recording; }
void recordingInit() {} void recordingPoll() {} void loadConfig() {} void tripleReport() {}
void tripleAttach() {} void fmodTapInstall() {} void telemetryStart() {}
void logf(const char*, ...) {}
void sidecarPath(char* out, const char* leaf) { strcpy(out, leaf); }
bool safeRead(const void* p, void* out, size_t n) { memcpy(out,p,n); return true; }

static const GUID kInst = {0x000000a1, 0, 0, {0, 0, 0, 0, 0, 0, 0, 0}};
static const char* kInstText = "{000000a1-0000-0000-0000-000000000000}";
static const DWORD kSixDof = DI8DEVTYPE_1STPERSON | (DI8DEVTYPE1STPERSON_SIXDOF << 8);

struct Device : IDirectInputDevice8W {
    ULONG refs=1; int effects=0, gains=0, centreOn=0, centreOff=0, commands=0, escapes=0, sends=0, reads=0;
    LONG lX=40000, lZ=10000, lRz=0;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID,void**) override { return E_NOINTERFACE; }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
    ULONG STDMETHODCALLTYPE Release() override { return --refs; }
    HRESULT STDMETHODCALLTYPE GetCapabilities(LPDIDEVCAPS c) override { c->dwFlags=DIDC_ATTACHED|DIDC_FORCEFEEDBACK; c->dwDevType=kSixDof; return S_OK; }
    HRESULT STDMETHODCALLTYPE EnumObjects(LPDIENUMDEVICEOBJECTSCALLBACKW,LPVOID,DWORD) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE GetProperty(REFGUID p,LPDIPROPHEADER h) override {
        if (&p == &DIPROP_RANGE) { ((LPDIPROPRANGE)h)->lMin=0; ((LPDIPROPRANGE)h)->lMax=65535; }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetProperty(REFGUID p,LPCDIPROPHEADER h) override {
        if (&p == &DIPROP_FFGAIN) ++gains;
        if (&p == &DIPROP_AUTOCENTER) ++(((LPCDIPROPDWORD)h)->dwData == DIPROPAUTOCENTER_OFF ? centreOff : centreOn);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Acquire() override { return S_OK; }
    HRESULT STDMETHODCALLTYPE Unacquire() override { return S_OK; }
    HRESULT STDMETHODCALLTYPE GetDeviceState(DWORD cb,LPVOID d) override {
        ++reads; memset(d,0,cb); auto* s=(DIJOYSTATE*)d; s->lX=lX; s->lZ=lZ; s->lRz=lRz;
        for (auto& pov : s->rgdwPOV) pov=(DWORD)-1;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetDeviceData(DWORD,LPDIDEVICEOBJECTDATA,LPDWORD k,DWORD) override { if (k) *k=0; return S_OK; }
    HRESULT STDMETHODCALLTYPE SetDataFormat(LPCDIDATAFORMAT) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE SetEventNotification(HANDLE) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE SetCooperativeLevel(HWND,DWORD) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE GetObjectInfo(LPDIDEVICEOBJECTINSTANCEW,DWORD,DWORD) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE GetDeviceInfo(LPDIDEVICEINSTANCEW i) override {
        i->guidInstance=kInst; i->guidProduct=GUID{}; i->guidProduct.Data1=g_cfg.product; i->dwDevType=kSixDof; return S_OK;
    }
    HRESULT STDMETHODCALLTYPE RunControlPanel(HWND,DWORD) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE Initialize(HINSTANCE,DWORD,REFGUID) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE CreateEffect(REFGUID,LPCDIEFFECT,LPDIRECTINPUTEFFECT*,LPUNKNOWN) override { ++effects; return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE EnumEffects(LPDIENUMEFFECTSCALLBACKW,LPVOID,DWORD) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE GetEffectInfo(LPDIEFFECTINFOW,REFGUID) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE GetForceFeedbackState(LPDWORD s) override { if (s) *s=DIGFFS_STOPPED; return S_OK; }
    HRESULT STDMETHODCALLTYPE SendForceFeedbackCommand(DWORD) override { ++commands; return S_OK; }
    HRESULT STDMETHODCALLTYPE EnumCreatedEffectObjects(LPDIENUMCREATEDEFFECTOBJECTSCALLBACK,LPVOID,DWORD) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE Escape(LPDIEFFESCAPE) override { ++escapes; return S_OK; }
    HRESULT STDMETHODCALLTYPE Poll() override { return S_OK; }
    HRESULT STDMETHODCALLTYPE SendDeviceData(DWORD,LPCDIDEVICEOBJECTDATA,LPDWORD,DWORD) override { ++sends; return S_OK; }
    HRESULT STDMETHODCALLTYPE EnumEffectsInFile(LPCWSTR,LPDIENUMEFFECTSINFILECALLBACK,LPVOID,DWORD) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE WriteEffectToFile(LPCWSTR,DWORD,LPDIFILEEFFECT,DWORD) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE BuildActionMap(LPDIACTIONFORMATW,LPCWSTR,DWORD) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE SetActionMap(LPDIACTIONFORMATW,LPCWSTR,DWORD) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE GetImageInfo(LPDIDEVICEIMAGEINFOHEADERW) override { return S_OK; }
} device;
struct Input : IDirectInput8W {
    ULONG refs=1; int creations=0;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID,void**) override { return E_NOINTERFACE; }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
    ULONG STDMETHODCALLTYPE Release() override { return --refs; }
    HRESULT STDMETHODCALLTYPE CreateDevice(REFGUID,LPDIRECTINPUTDEVICE8W* out,LPUNKNOWN) override { ++creations; *out=&device; return S_OK; }
    HRESULT STDMETHODCALLTYPE EnumDevices(DWORD,LPDIENUMDEVICESCALLBACKW cb,LPVOID ref,DWORD) override {
        DIDEVICEINSTANCEW i{}; i.dwSize=sizeof i; i.guidInstance=kInst; i.guidProduct.Data1=g_cfg.product; i.dwDevType=kSixDof;
        cb(&i,ref); return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetDeviceStatus(REFGUID) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE RunControlPanel(HWND,DWORD) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE Initialize(HINSTANCE,DWORD) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE FindDevice(REFGUID,LPCWSTR,LPGUID) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE EnumDevicesBySemantics(LPCWSTR,LPDIACTIONFORMATW,LPDIENUMDEVICESBYSEMANTICSCBW,LPVOID,DWORD) override { assert(false); return S_OK; }
    HRESULT STDMETHODCALLTYPE ConfigureDevices(LPDICONFIGUREDEVICESCALLBACK,LPDICONFIGUREDEVICESPARAMSW,DWORD,LPVOID) override { assert(false); return S_OK; }
} input;

// What the proxy's CreateHookedDirectInput does, with the fake in place of the system DLL.
static int fakeCreates=0;
static HRESULT WINAPI fakeCreate(HINSTANCE,DWORD,REFIID riid,LPVOID* out,LPUNKNOWN) {
    ++fakeCreates; *out=static_cast<IDirectInput8W*>(&input);
    return hookInput(*out,riid) ? S_OK : E_NOINTERFACE;
}
static std::wstring g_dir;
static void switchOn(const wchar_t* name, bool on) {
    std::wstring p=g_dir+L"\\dbce\\gravel\\"+name;
    if (on) { HANDLE h=CreateFileW(p.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,0,nullptr); assert(h!=INVALID_HANDLE_VALUE); CloseHandle(h); }
    else DeleteFileW(p.c_str());
}
static void resetFence() {
    g_fence=g_injectOn=g_fencePinned=false;
    dbce::fence::settings()=dbce::fence::Settings{};
}

int main() {
    InitializeCriticalSection(&g_devLock);
    g_inited=true;   // the exports below must not load the system dinput8.dll

    // Switch rules (fenceInit): read from %LOCALAPPDATA%\dbce\gravel at start.
    wchar_t tmp[MAX_PATH]; GetTempPathW(MAX_PATH,tmp);
    g_dir=std::wstring(tmp)+L"gravel-fence-fixture-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64());
    CreateDirectoryW(g_dir.c_str(),nullptr); CreateDirectoryW((g_dir+L"\\dbce").c_str(),nullptr); CreateDirectoryW((g_dir+L"\\dbce\\gravel").c_str(),nullptr);
    SetEnvironmentVariableW(L"LOCALAPPDATA",g_dir.c_str());
    fenceInit();
    assert(!g_fence && !g_injectOn && !dbce::fence::settings().observe);   // no switch: the ordinary proxy
    switchOn(L"ffb-fence.on",true); recording=true; fenceInit();
    assert(!g_fence && !dbce::fence::settings().observe);                   // a recording request keeps its own latch
    recording=false; fenceInit();
    assert(g_fence && !g_injectOn && !g_fencePinned && dbce::fence::settings().observe && !dbce::fence::settings().injections);
    resetFence(); switchOn(L"inject.on",true); fenceInit();
    assert(g_fence && g_injectOn && dbce::fence::settings().injections==&g_injections);
    // Not gravel-Win64-Shipping.exe (this fixture): DirectInput is refused before the system call.
    void* refused=(void*)1;
    assert(FAILED(dbce::testing::CreateFencedDirectInput(fakeCreate,nullptr,DIRECTINPUT_VERSION,IID_IDirectInput8W,&refused,nullptr,g_fencePinned)) && !refused && fakeCreates==0);
    // Fenced, COM creation is refused.
    void* factory=(void*)1;
    assert(DllGetClassObject(CLSID_DirectInput8,IID_IClassFactory,&factory)==CLASS_E_CLASSNOTAVAILABLE && !factory);
    switchOn(L"inject.on",false); switchOn(L"ffb-fence.on",false);

    // The fenced game's interface: the toolkit wrapper over the proxy's hooked one.
    g_fencePinned=true;
    IDirectInput8W* di=nullptr;
    assert(SUCCEEDED(dbce::testing::CreateFencedDirectInput(fakeCreate,nullptr,DIRECTINPUT_VERSION,IID_IDirectInput8W,(void**)&di,nullptr,g_fencePinned)));
    assert(di && di!=&input && fakeCreates==1);
    // The re-type survives the wrapper, in enumeration and in capabilities.
    DWORD enumerated=0;
    auto cb=[](LPCDIDEVICEINSTANCEW i,LPVOID ref)->BOOL { *(DWORD*)ref=i->dwDevType; return DIENUM_CONTINUE; };
    assert(SUCCEEDED(di->EnumDevices(DI8DEVCLASS_GAMECTRL,cb,&enumerated,DIEDFL_ATTACHEDONLY)) && (enumerated&0xFF)==DI8DEVTYPE_DRIVING);
    IDirectInputDevice8W* dev=nullptr;
    assert(SUCCEEDED(di->CreateDevice(kInst,&dev,nullptr)) && dev && dev!=&device && input.creations==1);
    assert(findDev(&device));   // the proxy tracks the real device behind the wrapper
    DIDEVCAPS caps{}; caps.dwSize=sizeof caps;
    assert(SUCCEEDED(dev->GetCapabilities(&caps)) && (caps.dwDevType&0xFF)==DI8DEVTYPE_DRIVING && (caps.dwFlags&DIDC_FORCEFEEDBACK));

    // No force route reaches the device.
    DICONSTANTFORCE constant{7500}; DWORD axis=DIJOFS_X; LONG direction=1;
    DIEFFECT e{}; e.dwSize=sizeof e; e.dwGain=10000; e.cAxes=1; e.rgdwAxes=&axis; e.rglDirection=&direction;
    e.cbTypeSpecificParams=sizeof constant; e.lpvTypeSpecificParams=&constant;
    LPDIRECTINPUTEFFECT effect=nullptr;
    assert(SUCCEEDED(dev->CreateEffect(GUID_ConstantForce,&e,&effect,nullptr)) && effect);
    assert(SUCCEEDED(effect->Start(1,0)));
    effect->Release();
    DIPROPDWORD p{}; p.diph={sizeof p,sizeof p.diph,0,DIPH_DEVICE}; p.dwData=10000;
    dev->SetProperty(DIPROP_FFGAIN,&p.diph);
    p.dwData=DIPROPAUTOCENTER_ON; dev->SetProperty(DIPROP_AUTOCENTER,&p.diph);
    dev->SendForceFeedbackCommand(DISFFC_SETACTUATORSON);
    dev->Escape(nullptr);
    dev->SendDeviceData(sizeof(DIDEVICEOBJECTDATA),nullptr,nullptr,0);
    assert(device.effects==0 && device.gains==0 && device.centreOn==0 && device.commands==0 && device.escapes==0 && device.sends==0);
    assert(g_ffb.effects.load()==0);   // the proxy's own effect tap never sees a fenced effect
    p.dwData=DIPROPAUTOCENTER_OFF; dev->SetProperty(DIPROP_AUTOCENTER,&p.diph);
    assert(device.centreOff==1);       // less force is forwarded

    // The read tap: once per read, the state the game received.
    assert(SUCCEEDED(dev->SetDataFormat(&c_dfDIJoystick2)));
    DIJOYSTATE2 s{};
    assert(SUCCEEDED(dev->GetDeviceState(sizeof s,&s)) && s.lX==40000 && device.reads==1);
    assert(g_input.reads.load()==1 && g_input.rawSteer.load()==40000 && g_input.rawThr.load()==10000 && g_input.live.load());
    dbce::fence::settings().injections=&g_injections;
    dbce::controls::InjectCommand c; std::string why;
    assert(dbce::controls::parseInject(std::string("inject raw axis 0 dev=")+kInstText+" value=0 ms=2000",nullptr,c,why) && g_injections.add(c,GetTickCount64()).empty());
    assert(dbce::controls::parseInject(std::string("inject raw axis 2 dev=")+kInstText+" value=65535 ms=2000",nullptr,c,why) && g_injections.add(c,GetTickCount64()).empty());
    assert(SUCCEEDED(dev->GetDeviceState(sizeof s,&s)) && s.lX==0 && s.lZ==65535 && device.reads==2);
    assert(g_input.reads.load()==2 && g_input.rawSteer.load()==0 && g_input.rawThr.load()==65535);
    assert(std::abs(g_input.steer.load()+1.f)<1e-6f && std::abs(g_input.throttle.load()-1.f)<1e-6f);   // telemetry shows the injection
    // A custom format: neither injected nor observed.
    DIDATAFORMAT custom=c_dfDIJoystick2;
    std::vector<DIOBJECTDATAFORMAT> swapped(c_dfDIJoystick2.rgodf,c_dfDIJoystick2.rgodf+c_dfDIJoystick2.dwNumObjs);
    std::swap(swapped[0].dwOfs,swapped[1].dwOfs); custom.rgodf=swapped.data();
    dev->SetDataFormat(&custom);
    assert(SUCCEEDED(dev->GetDeviceState(sizeof s,&s)) && s.lX==40000 && g_input.reads.load()==2);

    // Unfenced, the hook itself observes, once, as before.
    g_fence=false;
    IDirectInputDevice8W* hooked=&device;
    assert(SUCCEEDED(hooked->GetDeviceState(sizeof s,&s)) && g_input.reads.load()==3 && g_input.rawSteer.load()==40000);

    dev->Release(); di->Release();
    RemoveDirectoryW((g_dir+L"\\dbce\\gravel").c_str()); RemoveDirectoryW((g_dir+L"\\dbce").c_str()); RemoveDirectoryW(g_dir.c_str());
    std::cout<<"PASS fence over the proxy: switch rules (none, recording excludes, fence, inject, unpinned refused, COM refused), re-type through the wrapper, no effect/gain/autocentre-on/command/escape/data reaches the device, read tap once per read after injection, custom format untouched, unfenced tap unchanged\n";
}
