// Exercise actual SDK interfaces, not hand-counted test vtables. No hardware.
#include "../../src/proxy.cpp"
#include <cassert>
#include <iostream>
#ifdef ABI_ANSI
#define ABI(name) name##A
using AbiChar=char;
#else
#define ABI(name) name##W
using AbiChar=wchar_t;
#endif
Config g_cfg; InputState g_input; FfbState g_ffb; FmodState g_fmod; Ue4State g_ue4;
bool muted=true;
bool recordingMuted() { return muted; }
void recordingInit() {} void recordingPoll() {} void loadConfig() {} void tripleReport() {}
void tripleAttach() {} void fmodTapInstall() {} void telemetryStart() {}
void logf(const char*, ...) {}
bool safeRead(const void* p, void* out, size_t n) { memcpy(out,p,n); return true; }

struct Device : ABI(IDirectInputDevice8) {
    int escapes=0, enumerations=0, queryCalls=0; ULONG refs=1;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID,void**) override { ++queryCalls; return E_NOINTERFACE; }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
    ULONG STDMETHODCALLTYPE Release() override { return --refs; }
    HRESULT STDMETHODCALLTYPE GetCapabilities(LPDIDEVCAPS c) override { c->dwFlags=DIDC_FORCEFEEDBACK; return S_OK; }
    HRESULT STDMETHODCALLTYPE EnumObjects(ABI(LPDIENUMDEVICEOBJECTSCALLBACK),LPVOID,DWORD) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE GetProperty(REFGUID,LPDIPROPHEADER) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE SetProperty(REFGUID,LPCDIPROPHEADER) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE Acquire() override { return S_OK; }
    HRESULT STDMETHODCALLTYPE Unacquire() override { return S_OK; }
    HRESULT STDMETHODCALLTYPE GetDeviceState(DWORD,LPVOID) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE GetDeviceData(DWORD,LPDIDEVICEOBJECTDATA,LPDWORD,DWORD) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE SetDataFormat(LPCDIDATAFORMAT) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE SetEventNotification(HANDLE) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE SetCooperativeLevel(HWND,DWORD) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE GetObjectInfo(ABI(LPDIDEVICEOBJECTINSTANCE),DWORD,DWORD) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE GetDeviceInfo(ABI(LPDIDEVICEINSTANCE) i) override { i->guidProduct.Data1=g_cfg.product; return S_OK; }
    HRESULT STDMETHODCALLTYPE RunControlPanel(HWND,DWORD) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE Initialize(HINSTANCE,DWORD,REFGUID) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE CreateEffect(REFGUID,LPCDIEFFECT,LPDIRECTINPUTEFFECT*,LPUNKNOWN) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE EnumEffects(ABI(LPDIENUMEFFECTSCALLBACK),LPVOID,DWORD) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE GetEffectInfo(ABI(LPDIEFFECTINFO),REFGUID) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE GetForceFeedbackState(LPDWORD) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE SendForceFeedbackCommand(DWORD) override { assert(false); return S_OK; }
    HRESULT STDMETHODCALLTYPE EnumCreatedEffectObjects(LPDIENUMCREATEDEFFECTOBJECTSCALLBACK,LPVOID,DWORD) override { ++enumerations; return S_OK; }
    HRESULT STDMETHODCALLTYPE Escape(LPDIEFFESCAPE) override { ++escapes; return S_OK; }
    HRESULT STDMETHODCALLTYPE Poll() override { return S_OK; }
    HRESULT STDMETHODCALLTYPE SendDeviceData(DWORD,LPCDIDEVICEOBJECTDATA,LPDWORD,DWORD) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE EnumEffectsInFile(const AbiChar*,LPDIENUMEFFECTSINFILECALLBACK,LPVOID,DWORD) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE WriteEffectToFile(const AbiChar*,DWORD,LPDIFILEEFFECT,DWORD) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE BuildActionMap(ABI(LPDIACTIONFORMAT),const AbiChar*,DWORD) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE SetActionMap(ABI(LPDIACTIONFORMAT),const AbiChar*,DWORD) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE GetImageInfo(ABI(LPDIDEVICEIMAGEINFOHEADER)) override { return S_OK; }
} device;
struct OtherDevice : Device {
    HRESULT STDMETHODCALLTYPE GetDeviceInfo(ABI(LPDIDEVICEINSTANCE) i) override { i->guidProduct.Data1=g_cfg.product; return S_OK; }
} otherDevice;
static Device* semanticDevice=&device;
struct Input : ABI(IDirectInput8) {
    ULONG refs=1; int semantics=0, configurations=0;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID,void**) override { assert(false); return E_NOINTERFACE; }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
    ULONG STDMETHODCALLTYPE Release() override { return --refs; }
    HRESULT STDMETHODCALLTYPE CreateDevice(REFGUID,ABI(LPDIRECTINPUTDEVICE8)* out,LPUNKNOWN) override { *out=&device; return S_OK; }
    HRESULT STDMETHODCALLTYPE EnumDevices(DWORD,ABI(LPDIENUMDEVICESCALLBACK),LPVOID,DWORD) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE GetDeviceStatus(REFGUID) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE RunControlPanel(HWND,DWORD) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE Initialize(HINSTANCE,DWORD) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE FindDevice(REFGUID,const AbiChar*,LPGUID) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE EnumDevicesBySemantics(const AbiChar*,ABI(LPDIACTIONFORMAT),ABI(LPDIENUMDEVICESBYSEMANTICSCB) cb,LPVOID ref,DWORD) override {
        ++semantics; ABI(DIDEVICEINSTANCE) info{}; info.dwSize=sizeof info; info.guidProduct.Data1=g_cfg.product;
        cb(&info,semanticDevice,123,7,ref); return S_OK;
    }
    HRESULT STDMETHODCALLTYPE ConfigureDevices(LPDICONFIGUREDEVICESCALLBACK,ABI(LPDICONFIGUREDEVICESPARAMS),DWORD,LPVOID) override { ++configurations; return S_OK; }
} input;
struct Factory : IClassFactory {
    ULONG refs=1; int creations=0;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID,void**) override { return E_NOINTERFACE; }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
    ULONG STDMETHODCALLTYPE Release() override { return --refs; }
    HRESULT STDMETHODCALLTYPE CreateInstance(IUnknown*,REFIID,void** out) override { ++creations; *out=&input; return S_OK; }
    HRESULT STDMETHODCALLTYPE LockServer(BOOL) override { return S_OK; }
} factory;

int main() {
    InitializeCriticalSection(&g_devLock);
    IClassFactory* wrapped=new CaptureFactory(&factory);
    ABI(IDirectInput8)* di=nullptr;
    assert(SUCCEEDED(wrapped->CreateInstance(nullptr,ABI(IID_IDirectInput8),(void**)&di)) && factory.creations==1);
    // This is the first device route, before CreateDevice has patched its class.
    int semanticCallbacks=0;
    auto semanticCallback=[](ABI(LPCDIDEVICEINSTANCE),ABI(LPDIRECTINPUTDEVICE8) d,DWORD flags,DWORD remaining,LPVOID ref)->BOOL {
        assert(flags==123 && remaining==7);
        assert(FAILED(d->Escape(nullptr)) && device.escapes==0);
        assert(SUCCEEDED(d->SendForceFeedbackCommand(DISFFC_SETACTUATORSON)));
        ++*(int*)ref; return DIENUM_CONTINUE;
    };
    assert(SUCCEEDED(di->EnumDevicesBySemantics(nullptr,nullptr,semanticCallback,&semanticCallbacks,0)));
    assert(semanticCallbacks==1 && input.semantics==1);
    assert(FAILED(di->EnumDevicesBySemantics(nullptr,nullptr,nullptr,nullptr,0)) && input.semantics==1);
    // The current vtable patcher refuses a second implementation rather than
    // forwarding it through the first implementation's original methods.
    semanticDevice=&otherDevice;
    assert(FAILED(di->EnumDevicesBySemantics(nullptr,nullptr,semanticCallback,&semanticCallbacks,0)));
    assert(semanticCallbacks==1 && input.semantics==2);
    semanticDevice=&device;
    assert(FAILED(di->ConfigureDevices(nullptr,nullptr,0,nullptr)) && input.configurations==0);
    ABI(IDirectInputDevice8)* dev=nullptr;
    assert(SUCCEEDED(di->CreateDevice(GUID_NULL,&dev,nullptr)));
    assert(FAILED(dev->Escape(nullptr)) && device.escapes==0);
    assert(SUCCEEDED(dev->EnumCreatedEffectObjects(nullptr,nullptr,0)) && device.enumerations==1);
    assert(SUCCEEDED(dev->SendForceFeedbackCommand(DISFFC_SETACTUATORSON)));
    void* queried=(void*)0x1234;
    assert(FAILED(dev->QueryInterface(ABI(IID_IDirectInputDevice7),&queried)) && !queried && device.queryCalls==0);
    assert(SUCCEEDED(dev->QueryInterface(IID_IUnknown,&queried)) && queried==dev);
    ((IUnknown*)queried)->Release();
    assert(FAILED(di->QueryInterface(ABI(IID_IDirectInput7),&queried)) && !queried);
    assert(SUCCEEDED(di->QueryInterface(IID_IUnknown,&queried)) && queried==di);
    ((IUnknown*)queried)->Release();
    assert(FAILED(wrapped->CreateInstance(nullptr,ABI(IID_IDirectInput7),&queried)) && !queried && factory.creations==1);
    assert(FAILED(wrapped->CreateInstance(&input,ABI(IID_IDirectInput8),&queried)) && !queried && factory.creations==1);
    muted=false;
    assert(SUCCEEDED(di->ConfigureDevices(nullptr,nullptr,0,nullptr)) && input.configurations==1);
    semanticDevice=&otherDevice;
    auto plainCallback=[](ABI(LPCDIDEVICEINSTANCE),ABI(LPDIRECTINPUTDEVICE8) d,DWORD,DWORD,void* ref)->BOOL {
        assert(d==&otherDevice && SUCCEEDED(d->Escape(nullptr)));
        ++*(int*)ref; return DIENUM_STOP;
    };
    assert(SUCCEEDED(di->EnumDevicesBySemantics(nullptr,nullptr,plainCallback,&semanticCallbacks,0)));
    assert(semanticCallbacks==2 && otherDevice.escapes==1);
    wrapped->Release(); assert(factory.refs==0);
    std::cout<<"PASS SDK virtual calls: semantic devices guarded before callback, capture config refused, normal config forwarded, factory creation guarded, device Escape vs enumeration slots, QI escape/aggregation refused\n";
}
