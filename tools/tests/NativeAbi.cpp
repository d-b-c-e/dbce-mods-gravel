// Exercise actual SDK interfaces, not hand-counted test vtables. No hardware.
#include "../../src/proxy.cpp"
#include <cassert>
#include <iostream>
Config g_cfg; InputState g_input; FfbState g_ffb; FmodState g_fmod; Ue4State g_ue4;
bool recordingMuted() { return true; }
void recordingInit() {} void recordingPoll() {} void loadConfig() {} void tripleReport() {}
void tripleAttach() {} void fmodTapInstall() {} void telemetryStart() {}
void logf(const char*, ...) {}
bool safeRead(const void* p, void* out, size_t n) { memcpy(out,p,n); return true; }

struct Device : IDirectInputDevice8W {
    int escapes=0, enumerations=0, queryCalls=0; ULONG refs=1;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID,void**) override { ++queryCalls; return E_NOINTERFACE; }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
    ULONG STDMETHODCALLTYPE Release() override { return --refs; }
    HRESULT STDMETHODCALLTYPE GetCapabilities(LPDIDEVCAPS c) override { c->dwFlags=DIDC_FORCEFEEDBACK; return S_OK; }
    HRESULT STDMETHODCALLTYPE EnumObjects(LPDIENUMDEVICEOBJECTSCALLBACKW,LPVOID,DWORD) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE GetProperty(REFGUID,LPDIPROPHEADER) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE SetProperty(REFGUID,LPCDIPROPHEADER) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE Acquire() override { return S_OK; }
    HRESULT STDMETHODCALLTYPE Unacquire() override { return S_OK; }
    HRESULT STDMETHODCALLTYPE GetDeviceState(DWORD,LPVOID) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE GetDeviceData(DWORD,LPDIDEVICEOBJECTDATA,LPDWORD,DWORD) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE SetDataFormat(LPCDIDATAFORMAT) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE SetEventNotification(HANDLE) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE SetCooperativeLevel(HWND,DWORD) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE GetObjectInfo(LPDIDEVICEOBJECTINSTANCEW,DWORD,DWORD) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE GetDeviceInfo(LPDIDEVICEINSTANCEW i) override { i->guidProduct.Data1=g_cfg.product; return S_OK; }
    HRESULT STDMETHODCALLTYPE RunControlPanel(HWND,DWORD) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE Initialize(HINSTANCE,DWORD,REFGUID) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE CreateEffect(REFGUID,LPCDIEFFECT,LPDIRECTINPUTEFFECT*,LPUNKNOWN) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE EnumEffects(LPDIENUMEFFECTSCALLBACKW,LPVOID,DWORD) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE GetEffectInfo(LPDIEFFECTINFOW,REFGUID) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE GetForceFeedbackState(LPDWORD) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE SendForceFeedbackCommand(DWORD) override { assert(false); return S_OK; }
    HRESULT STDMETHODCALLTYPE EnumCreatedEffectObjects(LPDIENUMCREATEDEFFECTOBJECTSCALLBACK,LPVOID,DWORD) override { ++enumerations; return S_OK; }
    HRESULT STDMETHODCALLTYPE Escape(LPDIEFFESCAPE) override { ++escapes; return S_OK; }
    HRESULT STDMETHODCALLTYPE Poll() override { return S_OK; }
    HRESULT STDMETHODCALLTYPE SendDeviceData(DWORD,LPCDIDEVICEOBJECTDATA,LPDWORD,DWORD) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE EnumEffectsInFile(LPCWSTR,LPDIENUMEFFECTSINFILECALLBACK,LPVOID,DWORD) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE WriteEffectToFile(LPCWSTR,DWORD,LPDIFILEEFFECT,DWORD) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE BuildActionMap(LPDIACTIONFORMATW,LPCWSTR,DWORD) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE SetActionMap(LPDIACTIONFORMATW,LPCWSTR,DWORD) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE GetImageInfo(LPDIDEVICEIMAGEINFOHEADERW) override { return S_OK; }
} device;
struct Input : IDirectInput8W {
    ULONG refs=1;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID,void**) override { assert(false); return E_NOINTERFACE; }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
    ULONG STDMETHODCALLTYPE Release() override { return --refs; }
    HRESULT STDMETHODCALLTYPE CreateDevice(REFGUID,LPDIRECTINPUTDEVICE8W* out,LPUNKNOWN) override { *out=&device; return S_OK; }
    HRESULT STDMETHODCALLTYPE EnumDevices(DWORD,LPDIENUMDEVICESCALLBACKW,LPVOID,DWORD) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE GetDeviceStatus(REFGUID) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE RunControlPanel(HWND,DWORD) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE Initialize(HINSTANCE,DWORD) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE FindDevice(REFGUID,LPCWSTR,LPGUID) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE EnumDevicesBySemantics(LPCWSTR,LPDIACTIONFORMATW,LPDIENUMDEVICESBYSEMANTICSCBW,LPVOID,DWORD) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE ConfigureDevices(LPDICONFIGUREDEVICESCALLBACK,LPDICONFIGUREDEVICESPARAMSW,DWORD,LPVOID) override { return S_OK; }
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
    IDirectInput8W* di=nullptr;
    assert(SUCCEEDED(wrapped->CreateInstance(nullptr,IID_IDirectInput8W,(void**)&di)) && factory.creations==1);
    IDirectInputDevice8W* dev=nullptr;
    assert(SUCCEEDED(di->CreateDevice(GUID_NULL,&dev,nullptr)));
    assert(FAILED(dev->Escape(nullptr)) && device.escapes==0);
    assert(SUCCEEDED(dev->EnumCreatedEffectObjects(nullptr,nullptr,0)) && device.enumerations==1);
    assert(SUCCEEDED(dev->SendForceFeedbackCommand(DISFFC_SETACTUATORSON)));
    void* queried=(void*)0x1234;
    assert(FAILED(dev->QueryInterface(IID_IDirectInputDevice7W,&queried)) && !queried && device.queryCalls==0);
    assert(SUCCEEDED(dev->QueryInterface(IID_IUnknown,&queried)) && queried==dev);
    ((IUnknown*)queried)->Release();
    assert(FAILED(di->QueryInterface(IID_IDirectInput7W,&queried)) && !queried);
    assert(SUCCEEDED(di->QueryInterface(IID_IUnknown,&queried)) && queried==di);
    ((IUnknown*)queried)->Release();
    assert(FAILED(wrapped->CreateInstance(nullptr,IID_IDirectInput7W,&queried)) && !queried && factory.creations==1);
    assert(FAILED(wrapped->CreateInstance(&input,IID_IDirectInput8W,&queried)) && !queried && factory.creations==1);
    wrapped->Release(); assert(factory.refs==0);
    std::cout<<"PASS SDK virtual calls: factory creation guarded, device Escape vs enumeration slots, QI escape/aggregation refused\n";
}
