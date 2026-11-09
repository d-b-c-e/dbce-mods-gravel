// See dbce_overlay_d3d11.h (and dbce_overlay_d3d12.h for D3D12 swap chains).
#include "dbce_overlay_d3d11.h"
#include "dbce_overlay_internal.h"
#include "dbce_panel.h"
#include "dbce_panel_input.h"
#include "../third_party/imgui/imgui.h"
#include "../third_party/imgui/backends/imgui_impl_dx11.h"
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

namespace dbce { namespace overlay {

namespace detail {
void (*g_log)(const char *);
bool (*render12)(IDXGISwapChain *, bool);
void (*resize12)(IDXGISwapChain *);
void (*probe12)();
void (*before12)(IDXGISwapChain *);
void logf(const char *fmt, ...)
{
    if (!g_log) return;
    char b[512];
    va_list ap;
    va_start(ap, fmt);
    _vsnprintf_s(b, sizeof b, _TRUNCATE, fmt, ap);
    va_end(ap);
    g_log(b);
}

// One ImGui context for the process, whichever renderer draws it.
static LARGE_INTEGER g_freq, g_last;
void ensureImGui()
{
    if (ImGui::GetCurrentContext()) return;
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NoMouseCursorChange;
    io.MouseDrawCursor = dbce::panel::IsOpen();
    QueryPerformanceFrequency(&g_freq);
}
// After the renderer's NewFrame: size, time, queued input, the panel, Render.
void drawFrame(int w, int h)
{
    ImGuiIO &io = ImGui::GetIO();
    io.DisplaySize = ImVec2((float)w, (float)h);
    io.MouseDrawCursor = dbce::panel::IsOpen(); // passive status never draws or captures a cursor
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    io.DeltaTime = g_last.QuadPart ? (float)((double)(now.QuadPart - g_last.QuadPart) / (double)g_freq.QuadPart) : 1.0f / 60.0f;
    if (io.DeltaTime <= 0.0f || io.DeltaTime > 1.0f) io.DeltaTime = 1.0f / 60.0f;
    g_last = now;
    dbce::panel::DrainInput(w, h);
    ImGui::NewFrame();
    dbce::panel::Draw(w, h);
    ImGui::Render();
}

// ImGui draws in sRGB (gamma) space. A plain 8-bit UNORM back buffer holds those values as-is, so no conversion is
// needed. An _SRGB back buffer encodes on render-target write (linear -> sRGB) and a float back buffer holds linear
// values, so for those the panel's sRGB colours are linearized first, keeping its contrast (the washed-out panel found
// on art of rally's single-screen fullscreen window; UI-9S).
const char *formatName(DXGI_FORMAT f)
{
    switch (f) {
    case DXGI_FORMAT_R8G8B8A8_UNORM: return "R8G8B8A8_UNORM";
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: return "R8G8B8A8_UNORM_SRGB";
    case DXGI_FORMAT_B8G8R8A8_UNORM: return "B8G8R8A8_UNORM";
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB: return "B8G8R8A8_UNORM_SRGB";
    case DXGI_FORMAT_R10G10B10A2_UNORM: return "R10G10B10A2_UNORM";
    case DXGI_FORMAT_R16G16B16A16_FLOAT: return "R16G16B16A16_FLOAT";
    case DXGI_FORMAT_R11G11B10_FLOAT: return "R11G11B10_FLOAT";
    case DXGI_FORMAT_R32G32B32A32_FLOAT: return "R32G32B32A32_FLOAT";
    default: return "other";
    }
}
bool linearColors(DXGI_FORMAT f)
{
    switch (f) {
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
    case DXGI_FORMAT_R16G16B16A16_FLOAT:
    case DXGI_FORMAT_R11G11B10_FLOAT:
    case DXGI_FORMAT_R32G32B32A32_FLOAT:
        return true;
    default:
        return false;
    }
}
// 8-bit sRGB -> 8-bit linear byte (white stays white), the inverse of the hardware's render-target encode.
unsigned char linearByte(unsigned char c)
{
    const float s = (float)c / 255.0f;
    const float lin = s <= 0.04045f ? s / 12.92f : powf((s + 0.055f) / 1.055f, 2.4f);
    const float v = lin * 255.0f + 0.5f;
    return v >= 255.0f ? 255u : (unsigned char)v;
}
// Rewrite every vertex colour from sRGB to its linear 8-bit value (alpha untouched), so the _SRGB render-target
// encode, or a float buffer holding linear values, reproduces the panel's original sRGB colours.
void linearizeDrawData(ImDrawData *dd)
{
    struct Lut { unsigned char b[256]; Lut() { for (int i = 0; i < 256; ++i) b[i] = linearByte((unsigned char)i); } };
    static const Lut lut;
    for (int n = 0; n < dd->CmdListsCount; ++n) {
        ImDrawList *dl = dd->CmdLists[n];
        for (int i = 0; i < dl->VtxBuffer.Size; ++i) {
            ImDrawVert &v = dl->VtxBuffer[i];
            unsigned char *c = (unsigned char *)&v.col;
            c[0] = lut.b[c[0]];
            c[1] = lut.b[c[1]];
            c[2] = lut.b[c[2]];
        }
    }
}

static HMODULE moduleOf(const void *p)
{
    HMODULE m = nullptr;
    return p && GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)p, &m) ? m : nullptr;
}

static const char *moduleName(const void *p)
{
    static thread_local char name[MAX_PATH];
    HMODULE m = moduleOf(p);
    if (!m || !GetModuleFileNameA(m, name, MAX_PATH)) return "memory outside any module";
    const char *slash = strrchr(name, '\\');
    return slash ? slash + 1 : name;
}

static const IMAGE_NT_HEADERS *ntOf(HMODULE m) { return (const IMAGE_NT_HEADERS *)((const BYTE *)m + ((const IMAGE_DOS_HEADER *)m)->e_lfanew); }

static bool inCode(HMODULE m, const void *p)
{
    const BYTE *base = (const BYTE *)m;
    const IMAGE_NT_HEADERS *nt = ntOf(m);
    if ((const BYTE *)p < base) return false;
    const ULONG_PTR rva = (ULONG_PTR)((const BYTE *)p - base);
    if (rva >= nt->OptionalHeader.SizeOfImage) return false;
    const IMAGE_SECTION_HEADER *s = IMAGE_FIRST_SECTION(nt);
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++s)
        if ((s->Characteristics & IMAGE_SCN_MEM_EXECUTE) && rva >= s->VirtualAddress && rva < s->VirtualAddress + s->Misc.VirtualSize) return true;
    return false;
}

// n bytes at p (inside module m) as the module's file on disk has them, placed the way the loader places them at the
// module's current base (base relocations in the window applied: x86 code holds absolute addresses). False when the
// file cannot be read or a relocation crosses the window's edge.
static bool fileBytes(HMODULE m, const void *p, void *out, DWORD n)
{
    wchar_t path[MAX_PATH];
    const DWORD len = GetModuleFileNameW(m, path, MAX_PATH);
    if (!len || len >= MAX_PATH) return false;
    const BYTE *base = (const BYTE *)m;
    const IMAGE_NT_HEADERS *nt = ntOf(m);
    const ULONG_PTR rva = (ULONG_PTR)((const BYTE *)p - base);
    HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    BYTE head[4096];
    DWORD got = 0;
    bool ok = false;
    ULONG_PTR fileBase = 0;
    if (ReadFile(f, head, sizeof head, &got, nullptr) && got >= sizeof(IMAGE_DOS_HEADER)) {
        const LONG lfanew = ((const IMAGE_DOS_HEADER *)head)->e_lfanew;
        const IMAGE_NT_HEADERS *fnt = lfanew > 0 && (DWORD)lfanew + sizeof(IMAGE_NT_HEADERS) <= got ? (const IMAGE_NT_HEADERS *)(head + lfanew) : nullptr;
        if (fnt && fnt->Signature == IMAGE_NT_SIGNATURE && fnt->OptionalHeader.Magic == nt->OptionalHeader.Magic) {
            fileBase = (ULONG_PTR)fnt->OptionalHeader.ImageBase;
            const IMAGE_SECTION_HEADER *s = IMAGE_FIRST_SECTION(fnt);
            const WORD count = fnt->FileHeader.NumberOfSections;
            if ((const BYTE *)(s + count) <= head + got) {
                for (WORD i = 0; i < count; ++i, ++s) {
                    if (rva < s->VirtualAddress || rva + n > (ULONG_PTR)s->VirtualAddress + s->SizeOfRawData) continue;
                    LARGE_INTEGER at;
                    at.QuadPart = (LONGLONG)s->PointerToRawData + (LONGLONG)(rva - s->VirtualAddress);
                    DWORD r = 0;
                    ok = SetFilePointerEx(f, at, nullptr, FILE_BEGIN) && ReadFile(f, out, n, &r, nullptr) && r == n;
                    break;
                }
            }
        }
    }
    CloseHandle(f);
    if (!ok) return false;
    const ULONG_PTR delta = (ULONG_PTR)base - fileBase;
    const IMAGE_DATA_DIRECTORY &dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
    if (!delta || !dir.VirtualAddress || !dir.Size) return true;
    const BYTE *q = base + dir.VirtualAddress, *end = q + dir.Size;
    while (q + sizeof(IMAGE_BASE_RELOCATION) <= end) {
        const IMAGE_BASE_RELOCATION *b = (const IMAGE_BASE_RELOCATION *)q;
        if (b->SizeOfBlock < sizeof *b || q + b->SizeOfBlock > end) break;
        if (b->VirtualAddress + 0x1000 > rva && b->VirtualAddress < rva + n) {
            const WORD *e = (const WORD *)(b + 1);
            const DWORD count = (b->SizeOfBlock - (DWORD)sizeof *b) / (DWORD)sizeof(WORD);
            for (DWORD i = 0; i < count; ++i) {
                const int type = e[i] >> 12;
                const DWORD size = type == IMAGE_REL_BASED_DIR64 ? 8 : type == IMAGE_REL_BASED_HIGHLOW ? 4 : 0;
                const ULONG_PTR at = (ULONG_PTR)b->VirtualAddress + (e[i] & 0xFFF);
                if (!size || at + size <= rva || at >= rva + n) continue;
                if (at < rva || at + size > rva + n) return false;
                BYTE *v = (BYTE *)out + (at - rva);
                if (size == 8) { ULONGLONG x; memcpy(&x, v, 8); x += (ULONGLONG)delta; memcpy(v, &x, 8); }
                else { DWORD x; memcpy(&x, v, 4); x += (DWORD)delta; memcpy(v, &x, 4); }
            }
        }
        q += b->SizeOfBlock;
    }
    return true;
}

// The slot's value before anyone patched it. A slot still pointing into its own module's code is that (the file can be
// newer than the loaded copy after an update that waits for a restart); otherwise the module file's entry.
static void *systemSlot(void **vt, int index)
{
    HMODULE m = moduleOf(&vt[index]);
    if (!m) return nullptr;
    if (inCode(m, vt[index])) return vt[index];
    void *v = nullptr;
    return fileBytes(m, &vt[index], &v, sizeof v) && inCode(m, v) ? v : nullptr;
}

// Where a patched function entry jumps (the usual hook shapes), or null.
static const void *jumpTarget(const BYTE *p)
{
    __try {
        if (p[0] == 0xE9) { INT32 rel; memcpy(&rel, p + 1, 4); return p + 5 + rel; }
#ifdef _WIN64
        if (p[0] == 0xFF && p[1] == 0x25) { INT32 rel; memcpy(&rel, p + 2, 4); const void *t; memcpy(&t, p + 6 + rel, 8); return t; }
        if (p[0] == 0x48 && p[1] == 0xB8 && p[10] == 0xFF && p[11] == 0xE0) { const void *t; memcpy(&t, p + 2, 8); return t; }
#else
        if (p[0] == 0xFF && p[1] == 0x25) { const void *const *at; memcpy(&at, p + 2, 4); return *at; }
        if (p[0] == 0x68 && p[5] == 0xC3) { const void *t; memcpy(&t, p + 1, 4); return t; }
#endif
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    return nullptr;
}

// Length of one instruction from the few that compilers put at the start of these functions: register moves to and
// from the stack, lea, push/pop, stack adjustment with an immediate, mov edi,edi. 0 for anything else, including
// branches and (x64) RIP-relative operands, which could not run from another address unchanged.
static int prologueLength(const BYTE *p)
{
    int n = 0;
#ifdef _WIN64
    if ((p[0] & 0xF0) == 0x40) ++n;   // REX
#endif
    const BYTE op = p[n++];
    if (op >= 0x50 && op <= 0x5F) return n;   // push / pop register
    int imm = 0;
    switch (op) {
    case 0x89: case 0x8B: case 0x8D: case 0x33: case 0x2B: case 0x03: case 0x85: break;   // mov, lea, xor, sub, add, test
    case 0x83: imm = 1; break;
    case 0x81: imm = 4; break;
    default: return 0;
    }
    const BYTE modrm = p[n++];
    const int mod = modrm >> 6, rm = modrm & 7;
    if (op == 0x8D && mod == 3) return 0;
    if (mod != 3 && rm == 4) {
        const BYTE sib = p[n++];
        if (mod == 0 && (sib & 7) == 5) n += 4;
    }
    if (mod == 1) n += 1;
    else if (mod == 2) n += 4;
    else if (mod == 0 && rm == 5) {
#ifdef _WIN64
        return 0;
#else
        n += 4;   // an absolute address, already moved to the loaded base in the file copy
#endif
    }
    return n + imm;
}

#ifdef _WIN64
static const int kCopyAtLeast = 16;   // covers the usual patches: 5-byte relative, 12- and 14-byte absolute jumps
#else
static const int kCopyAtLeast = 8;    // 5-byte relative jump, 6-byte push/ret
#endif

// A copy of the system function's first instructions (from the module file) followed by a jump to the rest of it in
// place: calling it runs the function as if its entry were untouched, whatever another overlay wrote there. Nothing of
// the live code is written.
static void *buildTrampoline(const BYTE *entry, const BYTE *original, int *copied)
{
    int k = 0;
    while (k < kCopyAtLeast) {
        const int len = prologueLength(original + k);
        if (!len || k + len > 24) return nullptr;
        k += len;
    }
    BYTE *t = (BYTE *)VirtualAlloc(nullptr, 64, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!t) return nullptr;
    memcpy(t, original, (size_t)k);
#ifdef _WIN64
    const BYTE jmp[6] = {0xFF, 0x25, 0, 0, 0, 0};   // jmp [rip+0] ; dq entry+k
    memcpy(t + k, jmp, sizeof jmp);
    const void *back = entry + k;
    memcpy(t + k + 6, &back, 8);
#else
    t[k] = 0xE9;
    const INT32 rel = (INT32)((entry + k) - (t + k + 5));
    memcpy(t + k + 1, &rel, 4);
#endif
    DWORD old;
    if (!VirtualProtect(t, 64, PAGE_EXECUTE_READ, &old)) { VirtualFree(t, 0, MEM_RELEASE); return nullptr; }
    FlushInstructionCache(GetCurrentProcess(), t, 64);
    *copied = k;
    return t;
}

bool PatchSlot(void **vt, int index, void *hook, SlotHook &h)
{
    h.system = systemSlot(vt, index);
    DWORD old;
    if (!VirtualProtect(&vt[index], sizeof(void *), PAGE_EXECUTE_READWRITE, &old)) return false;
    h.next = vt[index];
    vt[index] = hook;
    VirtualProtect(&vt[index], sizeof(void *), old, &old);
    if (h.next != h.system)
        logf("overlay: %s was already hooked by %s; ours runs first, then it%s", h.name, moduleName(h.next),
             h.system ? "" : " (the system's function is unknown: a hook loop could not be broken)");
    return true;
}

static void bytesText(const BYTE *p, int n, char *out, size_t size)
{
    out[0] = 0;
    for (int i = 0; i < n && 3 * (size_t)i + 3 < size; ++i) sprintf_s(out + 3 * i, size - 3 * (size_t)i, "%02x ", p[i]);
}

// First re-entry: the trampoline to the system function (or a direct call when that cannot be built and its entry is
// as in the file).
static void noteLoop(SlotHook &h)
{
    if (InterlockedCompareExchange(&h.preparing, 1, 0) != 0) { while (!h.looped) Sleep(0); return; }
    char otherName[MAX_PATH];
    strncpy_s(otherName, moduleName(h.next), _TRUNCATE);
    HMODULE m = h.system ? moduleOf(h.system) : nullptr;
    if (!h.system) {
        logf("overlay: %s: ours and %s call each other and the system's function is unknown; the looping calls are skipped", h.name, otherName);
    } else if (!m || !fileBytes(m, h.system, h.original, sizeof h.original)) {
        logf("overlay: %s: ours and %s call each other; the system function's file could not be read, so it is called as it is", h.name, otherName);
    } else {
        const BYTE *now = (const BYTE *)h.system;
        int last = -1;
        for (int i = 0; i < (int)sizeof h.original; ++i) if (now[i] != h.original[i]) last = i;
        h.copied = 0;
        h.trampoline = buildTrampoline(now, h.original, &h.copied);
        h.fileOk = true;
        char bytes[3 * 16 + 1], first[3 * 24 + 1];
        bytesText(now, last + 1 < 16 ? last + 1 : 16, bytes, sizeof bytes);
        bytesText(h.original, 24, first, sizeof first);
        const void *to = last >= 0 ? jumpTarget(now) : nullptr;
        if (last >= 0)
            logf("overlay: %s: ours and %s call each other; %s's first %d bytes jump there (%s); %s",
                 h.name, to ? moduleName(to) : "another module", otherName, last + 1, bytes,
                 h.trampoline ? "ours runs a copy of its first instructions, then the rest in place" : "its first instructions could not be copied: the looping calls are skipped");
        else
            logf("overlay: %s was hooked again over ours by %s, which now calls ours while ours called it; ours calls the system's function from now on%s",
                 h.name, otherName, h.trampoline ? " (through a copy of its first instructions)" : "");
        if (!h.trampoline) logf("overlay: %s: first bytes in the file: %s", h.name, first);
    }
    InterlockedExchange(&h.looped, 1);
}

bool Reentry(SlotHook &h, int depth)
{
    if (depth == 0) return true;
    if (depth == 1) { noteLoop(h); return SystemFunction(h) != nullptr; }
    if (!InterlockedExchange(&h.skipNoted, 1)) logf("overlay: %s: the loop went on through the system's function; those calls are skipped", h.name);
    return false;
}

void *SystemFunction(SlotHook &h)
{
    if (!h.system) return nullptr;
    if (!h.fileOk) return h.system;                     // nothing to compare with: as it is
    const BYTE *now = (const BYTE *)h.system;
    if (h.trampoline) {
        // The copy jumps back to entry+copied: those bytes must still be the file's (a longer patch would cut them).
        if (memcmp(now + h.copied, h.original + h.copied, sizeof h.original - (size_t)h.copied) == 0) return h.trampoline;
        if (!InterlockedExchange(&h.tailNoted, 1)) logf("overlay: %s: the system function was changed past the copied instructions; those calls are skipped", h.name);
        return nullptr;
    }
    return memcmp(now, h.original, sizeof h.original) == 0 ? h.system : nullptr;   // unchanged entry: call it
}
} // namespace detail

namespace {
using detail::logf;
typedef HRESULT(STDMETHODCALLTYPE *PresentFn)(IDXGISwapChain *, UINT, UINT);
typedef HRESULT(STDMETHODCALLTYPE *Present1Fn)(IDXGISwapChain1 *, UINT, UINT, const DXGI_PRESENT_PARAMETERS *);
typedef HRESULT(STDMETHODCALLTYPE *ResizeFn)(IDXGISwapChain *, UINT, UINT, UINT, DXGI_FORMAT, UINT);
detail::SlotHook g_present = {"Present"}, g_present1 = {"Present1"}, g_resize = {"ResizeBuffers"};

CRITICAL_SECTION g_cs;              // the render state below: Present and ResizeBuffers can come from different threads
IDXGISwapChain *g_chain;            // the swap chain the panel draws on; not owned
HWND g_window;                      // its window
ID3D11Device *g_device;             // owned reference while ImGui's DX11 backend uses it
ID3D11DeviceContext *g_context;
ID3D11RenderTargetView *g_rtv;
bool g_backend;
DXGI_FORMAT g_backFormat = DXGI_FORMAT_UNKNOWN;   // logged when the adopted back buffer's format changes
bool g_inputOn;                     // the panel's input hook is on g_window: only while g_chain can be drawn on
HWND g_attachFailed;                // a window whose input hook could not be set (not tried again)
IDXGISwapChain *g_undrawableNoted;  // the last chain logged as one the panel cannot draw on

void releaseTarget() { if (g_rtv) { g_rtv->Release(); g_rtv = nullptr; } }

void shutdownBackend()
{
    releaseTarget();
    if (g_backend) { ImGui_ImplDX11_Shutdown(); g_backend = false; }
    if (g_context) { g_context->Release(); g_context = nullptr; }
    if (g_device) { g_device->Release(); g_device = nullptr; }
}

// The game window: the largest swap chain output wins, so a launcher or splash window does not keep the hook. Any API.
bool adopt(IDXGISwapChain *sc)
{
    if (sc == g_chain) return true;
    DXGI_SWAP_CHAIN_DESC d;
    if (FAILED(sc->GetDesc(&d)) || !d.OutputWindow) return false;
    RECT r;
    LONG area = GetClientRect(d.OutputWindow, &r) ? (r.right - r.left) * (r.bottom - r.top) : 0;
    LONG current = 0;   // the adopted chain's window, kept from its own description (input may be off)
    if (g_chain && g_window && IsWindow(g_window) && GetClientRect(g_window, &r)) current = (r.right - r.left) * (r.bottom - r.top);
    // A new swap chain on the panel's own window replaces the old one (the game recreated it); elsewhere only a larger one.
    if (g_chain && d.OutputWindow != g_window && area <= current) return false;
    shutdownBackend();
    g_chain = sc;
    g_window = d.OutputWindow;   // input follows in render(), once this chain is known to be drawable
    logf("overlay: drawing on the swap chain of window %p (%ldx%ld)", (void *)d.OutputWindow, (long)d.BufferDesc.Width,
         (long)d.BufferDesc.Height);
    return true;
}

void render11(IDXGISwapChain *sc, ID3D11Device *dev)
{
    if (dev != g_device) {   // first frame, or the game recreated its device
        shutdownBackend();
        g_device = dev;
        g_device->AddRef();
        g_device->GetImmediateContext(&g_context);
        detail::ensureImGui();
        g_backend = ImGui_ImplDX11_Init(g_device, g_context);
        if (!g_backend) logf("overlay: ImGui DX11 backend could not start");
    }
    if (!g_backend) return;
    ID3D11Texture2D *back = nullptr;
    if (FAILED(sc->GetBuffer(0, __uuidof(ID3D11Texture2D), (void **)&back)) || !back) return;
    D3D11_TEXTURE2D_DESC td;
    back->GetDesc(&td);
    if (td.Format != g_backFormat) {
        g_backFormat = td.Format;
        logf("overlay: back buffer format %s%s", detail::formatName(td.Format),
             detail::linearColors(td.Format) ? " (sRGB/float: linearizing ImGui colours)" : "");
    }
    if (!g_rtv && FAILED(g_device->CreateRenderTargetView(back, nullptr, &g_rtv))) g_rtv = nullptr;
    back->Release();
    if (!g_rtv) return;
    const int w = (int)td.Width, h = (int)td.Height;
    if (dbce::panel::Prepare(w, h)) { dbce::panel::BuildFonts(); ImGui_ImplDX11_InvalidateDeviceObjects(); }
    ImGui_ImplDX11_NewFrame();
    detail::drawFrame(w, h);
    if (detail::linearColors(td.Format)) detail::linearizeDrawData(ImGui::GetDrawData());
    // Draw on the back buffer and give the game its render targets back.
    ID3D11RenderTargetView *saved[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
    ID3D11DepthStencilView *savedDepth = nullptr;
    g_context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, saved, &savedDepth);
    g_context->OMSetRenderTargets(1, &g_rtv, nullptr);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    g_context->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, saved, savedDepth);
    for (auto *v : saved) if (v) v->Release();
    if (savedDepth) savedDepth->Release();
    // No view of the game's back buffer outlives its Present: one would keep a released swap chain alive, and a
    // flip-model replacement on the same window then fails (test_overlay_recreate.cpp).
    releaseTarget();
}

void (*volatile g_beforePanel)(IDXGISwapChain *, ID3D11Device *);
void (*volatile g_afterPanel)(IDXGISwapChain *, ID3D11Device *);

void render(IDXGISwapChain *sc)
{
    if (sc == g_chain) { dbce::panel::NoteFrame(); dbce::panel::PollDev(); }
    if (auto *before = g_beforePanel; before && sc == g_chain) {
        ID3D11Device *dev = nullptr;
        if (SUCCEEDED(sc->GetDevice(__uuidof(ID3D11Device), (void **)&dev)) && dev) { before(sc, dev); dev->Release(); }
    }
    if (sc == g_chain && detail::before12) detail::before12(sc);
    const bool wanted = dbce::panel::DrawWanted();
    if (sc == g_chain && g_inputOn && !wanted) return;   // the usual closed frame; otherwise drawability is checked
    // A chain not drawable yet (a D3D12 renderer that has not started) is looked at again every 30th Present.
    static int sinceProbe;
    if (sc == g_chain && !g_inputOn && !wanted && ++sinceProbe < 30) return;
    sinceProbe = 0;
    EnterCriticalSection(&g_cs);
    if (!adopt(sc) || sc != g_chain) { LeaveCriticalSection(&g_cs); return; }
    bool drawable = false;
    ID3D11Device *dev = nullptr;
    if (SUCCEEDED(sc->GetDevice(__uuidof(ID3D11Device), (void **)&dev)) && dev) {
        drawable = true;
        if (wanted) render11(sc, dev);
        if (auto *after = g_afterPanel) after(sc, dev);   // the finished frame, panel included
        dev->Release();
    } else if (detail::render12) {
        drawable = detail::render12(sc, wanted);   // also while closed: a renderer that can start gets input back
    }
    // The input is the panel's only while it can be drawn on this chain: F6 must never open a panel nobody sees.
    if (drawable && (!g_inputOn || dbce::panel::InputWindow() != g_window) && g_attachFailed != g_window) {
        g_inputOn = dbce::panel::AttachInput(g_window);
        if (!g_inputOn) g_attachFailed = g_window;
        logf(g_inputOn ? "overlay: F6 input attached to window %p" : "overlay: F6 input hook FAILED on window %p", (void *)g_window);
    } else if (!drawable && (g_inputOn || dbce::panel::IsOpen())) {
        dbce::panel::DetachInput();   // closes it as well
        g_inputOn = false;
    }
    if (!drawable && g_undrawableNoted != sc) {
        g_undrawableNoted = sc;
        logf("overlay: the game's swap chain is neither D3D11 nor an enabled D3D12 one it can draw; panel not drawn, input left to the game");
    }
    LeaveCriticalSection(&g_cs);
}

// Each hook: entered again on the same thread means another overlay's hook and ours are calling each other (see
// detail::SlotHook); from then on ours calls the system's function, and only the outermost call does the panel's work.
HRESULT STDMETHODCALLTYPE hkPresent(IDXGISwapChain *sc, UINT sync, UINT flags)
{
    static thread_local int depth;
    if (!detail::Reentry(g_present, depth)) return S_OK;
    detail::Depth in(depth);
    if (depth == 1 && !(flags & DXGI_PRESENT_TEST)) render(sc);
    if (g_present.looped) { auto fn = (PresentFn)detail::SystemFunction(g_present); return fn ? fn(sc, sync, flags) : S_OK; }
    return ((PresentFn)g_present.next)(sc, sync, flags);
}
HRESULT STDMETHODCALLTYPE hkPresent1(IDXGISwapChain1 *sc, UINT sync, UINT flags, const DXGI_PRESENT_PARAMETERS *p)
{
    static thread_local int depth;
    if (!detail::Reentry(g_present1, depth)) return S_OK;
    detail::Depth in(depth);
    if (depth == 1 && !(flags & DXGI_PRESENT_TEST)) render(sc);
    if (g_present1.looped) { auto fn = (Present1Fn)detail::SystemFunction(g_present1); return fn ? fn(sc, sync, flags, p) : S_OK; }
    return ((Present1Fn)g_present1.next)(sc, sync, flags, p);
}
HRESULT STDMETHODCALLTYPE hkResize(IDXGISwapChain *sc, UINT n, UINT w, UINT h, DXGI_FORMAT f, UINT flags)
{
    static thread_local int depth;
    if (!detail::Reentry(g_resize, depth)) return DXGI_ERROR_INVALID_CALL;
    detail::Depth in(depth);
    if (depth == 1) {
        // The panel's references to the back buffers must go before the buffers can be resized.
        EnterCriticalSection(&g_cs);
        if (sc == g_chain) { releaseTarget(); if (detail::resize12) detail::resize12(sc); }
        LeaveCriticalSection(&g_cs);
    }
    if (g_resize.looped) { auto fn = (ResizeFn)detail::SystemFunction(g_resize); return fn ? fn(sc, n, w, h, f, flags) : DXGI_ERROR_INVALID_CALL; }
    return ((ResizeFn)g_resize.next)(sc, n, w, h, f, flags);
}

DWORD WINAPI worker(LPVOID)
{
    HMODULE d3d = LoadLibraryW(L"d3d11.dll");
    auto create = d3d ? (PFN_D3D11_CREATE_DEVICE_AND_SWAP_CHAIN)GetProcAddress(d3d, "D3D11CreateDeviceAndSwapChain") : nullptr;
    if (!create) { logf("overlay: d3d11.dll not available; no panel"); return 0; }
    WNDCLASSEXW wc = {sizeof wc};
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"dbce-overlay-probe";
    RegisterClassExW(&wc);
    HWND w = CreateWindowExW(0, wc.lpszClassName, L"", WS_OVERLAPPED, 0, 0, 64, 64, nullptr, nullptr, wc.hInstance, nullptr);
    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount = 1;
    sd.BufferDesc.Width = 64;
    sd.BufferDesc.Height = 64;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = w;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    IDXGISwapChain *sc = nullptr;
    ID3D11Device *dev = nullptr;
    ID3D11DeviceContext *ctx = nullptr;
    HRESULT hr = E_FAIL;
    const D3D_DRIVER_TYPE types[2] = {D3D_DRIVER_TYPE_HARDWARE, D3D_DRIVER_TYPE_WARP};
    for (D3D_DRIVER_TYPE t : types) {
        hr = create(nullptr, t, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &sd, &sc, &dev, nullptr, &ctx);
        if (SUCCEEDED(hr)) break;
    }
    if (FAILED(hr) || !sc) { logf("overlay: probe swap chain failed (0x%08lx); no panel", (unsigned long)hr); if (w) DestroyWindow(w); return 0; }
    void **vt = *(void ***)sc;
    EnterCriticalSection(&g_cs);
    bool ok = detail::PatchSlot(vt, 8, (void *)&hkPresent, g_present) && detail::PatchSlot(vt, 13, (void *)&hkResize, g_resize);
    IDXGISwapChain1 *sc1 = nullptr;
    bool ok1 = false;
    if (ok && SUCCEEDED(sc->QueryInterface(__uuidof(IDXGISwapChain1), (void **)&sc1)) && sc1) {
        void **vt1 = *(void ***)sc1;
        ok1 = vt1 == vt && detail::PatchSlot(vt, 22, (void *)&hkPresent1, g_present1);
        sc1->Release();
    }
    LeaveCriticalSection(&g_cs);
    sc->Release(); ctx->Release(); dev->Release();
    DestroyWindow(w);
    logf(ok ? "overlay: DXGI present hook installed%s (F6 opens the panel)" : "overlay: present hook could not be installed%s",
         ok1 ? ", Present1 too" : "");
    if (ok && detail::probe12) detail::probe12();
    return 0;
}
INIT_ONCE g_once = INIT_ONCE_STATIC_INIT;
BOOL CALLBACK initOnce(PINIT_ONCE, PVOID, PVOID *) { InitializeCriticalSection(&g_cs); return TRUE; }
} // namespace

void SetBeforePanelD3D11(void (*hook)(IDXGISwapChain *, ID3D11Device *)) { g_beforePanel = hook; }
void SetAfterPanelD3D11(void (*hook)(IDXGISwapChain *, ID3D11Device *)) { g_afterPanel = hook; }

bool StartD3D11(void (*log)(const char *))
{
    InitOnceExecuteOnce(&g_once, initOnce, nullptr, nullptr);
    detail::g_log = log;
    HANDLE t = CreateThread(nullptr, 0, worker, nullptr, 0, nullptr);
    if (!t) { logf("overlay: worker could not start (%lu)", GetLastError()); return false; }
    CloseHandle(t);
    return true;
}

}} // namespace dbce::overlay
