// Triple screens for Gravel (UE 4.17) through the engine's own emulated stereo renderer.
//
// UE4 builds a fake stereo device when the command line has -emulatestereo
// (UEngine::InitializeHMDDevice) and then renders every frame as several views of one
// scene. This module, loaded with dinput8.dll before the game's entry point, turns that
// into three projected views (same technique and geometry as SonicTriple in
// dbce-mods-sonic-crossworlds, here without UE4SS):
//
//   1. command line: answers the exe's GetCommandLineW import with "-emulatestereo"
//      appended when the display is three screens wide (Triple = Auto), plus a borderless
//      window over the three monitors when they are separate displays (STD-015);
//   2. view count: UGameViewportClient::Draw renders 2 views in stereo, or 3 when the
//      family's monoscopic far field is on, which 4.17 allows only on mobile feature
//      levels. Two bytes there make it 3 whenever stereo is on, while the family itself
//      keeps the far field off, so the renderer treats the third view as a normal view;
//   3. device: three slots of the fake device's vtable (in the exe's .rdata) are swapped
//      before the device exists: AdjustViewRect (thirds), CalculateStereoViewOffset (turn
//      the side views by the panel angle, no eye offset) and GetStereoProjectionMatrix
//      (off-axis frustum per panel, Kooima, as the toolkit's TripleGeometry).
//
// Facts read from this build's code (gravel-Win64-Shipping.exe, ++UE4+Release-4.17):
// the fake device's vtable is stored by its constructor right before FOV = 100.0 and
// 640x480; slot 3 AdjustViewRect(pass, X&, Y&, SizeX&, SizeY&), 5 CalculateStereoViewOffset
// (pass, FRotator& rotation, float worldToMeters, FVector& location) - the caller reads the
// rotation back from memory after the call - and 6 GetStereoProjectionMatrix(pass, float
// FOV) returning FMatrix (float) through a hidden pointer. The FOV argument is the game
// camera's horizontal FOV; the stock fake device ignores it. Passes: 1 left eye (view 0,
// the primary view with the player's view state), 2 right eye, 3 monoscopic (view 2).
//
// Every pattern must match exactly once, or that part stays stock and the log says why.
#include "common.h"
#include <dxgi.h>
#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cwchar>
#include <cwctype>
#include <vector>

namespace {

constexpr double Pi = 3.14159265358979323846;

struct FMatrixF { float M[4][4]; };
struct FRotatorF { float Pitch, Yaw, Roll; };
struct Frustum { double yaw, l, r, b, t; };

enum class Mode { Off, Auto, On };
enum class FovSource { Game, Rig };

struct TripleConfig {
    Mode mode = Mode::Auto;
    FovSource fov = FovSource::Game;
    double panelW = 708.4, panelH = 398.5;   // mm, 32" 16:9 (GS32QCA)
    double eye = 0;                           // mm; 0 = from vfov
    double vfov = 58.715;                     // rig vertical FOV when eye is 0
    double side = 70;                         // panel angle, degrees
    double bezel = 8;                         // mm between panels
    int centre = 1, right = 2, left = 3;      // stereo pass drawn on each panel
    bool span = true;                         // borderless window over separate monitors
    bool uiCentre = true;                     // menus and HUD on the centre screen (STD-007/019)
    bool menuSidesBlack = true;               // side screens black outside gameplay (STD-019)
};
TripleConfig g_tc;

// DllMain runs before the log is configured; messages wait here until tripleReport.
std::string g_early;
void early(const char *fmt, ...)
{
    char b[256]; va_list ap; va_start(ap, fmt); vsnprintf(b, sizeof b, fmt, ap); va_end(ap);
    if (g_early.size() < 8192) { g_early += b; g_early += '\n'; }
}

std::atomic<bool> g_active{false};
Frustum g_panels[3]{};                        // 0 left, 1 centre, 2 right
std::atomic<float> g_panelFov{-1};            // game FOV the panels were computed for
SRWLOCK g_panelLock = SRWLOCK_INIT;

using AdjustFn = void (*)(const void *, int, int *, int *, uint32_t *, uint32_t *);
using OffsetFn = void (*)(void *, int, FRotatorF *, float, void *);
using ProjFn = FMatrixF *(*)(const void *, FMatrixF *, int, float);
ProjFn g_origProj = nullptr;
OffsetFn g_origOffset = nullptr;

// ------------------------------------------------------------- geometry
struct V3 { double x, y, z; };
V3 operator+(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
V3 operator-(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
V3 operator*(V3 a, double s) { return {a.x * s, a.y * s, a.z * s}; }
double dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
V3 cross(V3 a, V3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
V3 norm(V3 a) { double l = std::sqrt(dot(a, a)); return {a.x / l, a.y / l, a.z / l}; }

// Off-axis frustum of one panel (pa lower-left, pb lower-right, pc upper-left; eye at the
// origin, x right, y up, z forward). Port of the toolkit's TripleGeometry.Panel.
Frustum panel(V3 pa, V3 pb, V3 pc)
{
    V3 vr = norm(pb - pa), vu = norm(pc - pa), fwd = norm(cross(vr, vu));
    double d = dot(pa, fwd);
    return {std::atan2(fwd.x, fwd.z) * 180 / Pi, dot(vr, pa) / d, dot(vr, pb) / d, dot(vu, pa) / d, dot(vu, pc) / d};
}

// Eye distance: from the game's horizontal FOV on the centre panel (FovSource::Game), so
// the centre shows what the game shows on one screen and the sides continue it at the
// rig's panel angle; or from the rig (fixed eye distance / vertical FOV).
void computePanels(float gameFov)
{
    const double w = g_tc.panelW, h = g_tc.panelH, a = g_tc.side * Pi / 180;
    double d;
    if (g_tc.fov == FovSource::Game && gameFov > 5 && gameFov < 170)
        d = w / 2 / std::tan(gameFov * Pi / 360);
    else
        d = g_tc.eye > 0 ? g_tc.eye : h / 2 / std::tan(g_tc.vfov * Pi / 360);
    V3 up{0, h, 0};
    Frustum p[3];
    p[1] = panel({-w / 2, -h / 2, d}, {w / 2, -h / 2, d}, {-w / 2, h / 2, d});
    V3 toRight{std::cos(a), 0, -std::sin(a)}, rightInner = V3{w / 2, -h / 2, d} + toRight * g_tc.bezel;
    p[2] = panel(rightInner, rightInner + toRight * w, rightInner + up);
    V3 toLeft{-std::cos(a), 0, -std::sin(a)}, leftInner = V3{-w / 2, -h / 2, d} + toLeft * g_tc.bezel;
    V3 leftOuter = leftInner + toLeft * w;
    p[0] = panel(leftOuter, leftInner, leftOuter + up);
    AcquireSRWLockExclusive(&g_panelLock);
    for (int i = 0; i < 3; ++i) g_panels[i] = p[i];
    g_panelFov = gameFov;
    ReleaseSRWLockExclusive(&g_panelLock);
}

Frustum panelFor(int index)
{
    AcquireSRWLockShared(&g_panelLock);
    Frustum f = g_panels[index];
    ReleaseSRWLockShared(&g_panelLock);
    return f;
}

// Stereo pass -> panel (0 left, 1 centre, 2 right); -1 = not a pass we place.
int panelOf(int pass)
{
    if (pass == g_tc.left) return 0;
    if (pass == g_tc.centre) return 1;
    if (pass == g_tc.right) return 2;
    return -1;
}

// Gameplay = the mod is reading a live vehicle HUD (ue4.cpp). Outside it (front end,
// loading) the side views shrink to a 4x4 corner and the engine clears the rest of the
// window black, so menus sit on the centre screen alone (STD-019). Without the UE4 reader
// (telemetry or ue4 off) the side views stay on.
bool inGameplay()
{
    if (!g_cfg.enabled || !g_cfg.ue4Enabled || !g_tc.menuSidesBlack) return true;
    return g_ue4.live.load();
}

// ------------------------------------------------------------- camera keys
// STD-005/006: numpad 8/2 forward/back, 9/3 up/down, 4/6 left/right, 7/1 tilt down/up,
// +/- FOV, 5 reset; steps 0.02 m, 1 deg, 2 deg ([camera] in milestone_mod.ini). Offsets move
// the eye of all three views together, in a race only, and are saved in the INI.
struct CameraOffsets { float x = 0, y = 0, z = 0, tilt = 0, fov = 0; };
CameraOffsets g_cam;                        // read on the render path, written by cameraThread
SRWLOCK g_camLock = SRWLOCK_INIT;
float g_moveStep = 0.02f, g_tiltStep = 1.f, g_fovStep = 2.f;

CameraOffsets cameraNow()
{
    AcquireSRWLockShared(&g_camLock);
    CameraOffsets c = g_cam;
    ReleaseSRWLockShared(&g_camLock);
    return c;
}

// ---------------------------------------------------------- device hooks
void hookAdjustViewRect(const void *, int pass, int *x, int *, uint32_t *sizeX, uint32_t *sizeY)
{
    int p = panelOf(pass);
    if (p < 0) return;
    if (p != 1 && !inGameplay()) {
        if (p == 2) *x += (int)*sizeX - 4;
        *sizeX = 4; *sizeY = 4;
        return;
    }
    uint32_t third = *sizeX / 3;
    *x += (int)third * p;
    *sizeX = third;
}

struct FVectorF { float X, Y, Z; };

// Camera axes of a UE rotator (X forward, Y right, Z up; FRotationMatrix rows), and a turn about the camera's own
// up axis (yaw, + = right) and right axis (pitch, + = up) composed in the camera frame, back to a rotator as
// FMatrix::Rotator. Adding to world Euler yaw opened seams when the camera pitched or rolled (Astra's review of the
// same code in SonicTriple, 2026-10-06).
void cameraAxes(const FRotatorF &rot, V3 &f, V3 &rt, V3 &u)
{
    const double p = rot.Pitch * Pi / 180, y = rot.Yaw * Pi / 180, r = rot.Roll * Pi / 180;
    const double cp = std::cos(p), sp = std::sin(p), cy = std::cos(y), sy = std::sin(y), cr = std::cos(r), sr = std::sin(r);
    f = {cp * cy, cp * sy, sp};
    rt = {sr * sp * cy - cr * sy, sr * sp * sy + cr * cy, -sr * cp};
    u = {-(cr * sp * cy + sr * sy), cy * sr - cr * sp * sy, cr * cp};
}

void turnInCameraFrame(FRotatorF &rot, double yawDeg, double pitchUpDeg)
{
    V3 f, rt, u;
    cameraAxes(rot, f, rt, u);
    if (pitchUpDeg != 0) {
        const double a = pitchUpDeg * Pi / 180;
        const V3 f2 = f * std::cos(a) + u * std::sin(a), u2 = u * std::cos(a) - f * std::sin(a);
        f = f2; u = u2;
    }
    if (yawDeg != 0) {
        const double a = yawDeg * Pi / 180;
        const V3 f2 = f * std::cos(a) + rt * std::sin(a), r2 = rt * std::cos(a) - f * std::sin(a);
        f = f2; rt = r2;
    }
    const double pitch = std::atan2(f.z, std::sqrt(f.x * f.x + f.y * f.y)), yaw = std::atan2(f.y, f.x);
    const V3 flatRight{-std::sin(yaw), std::cos(yaw), 0};
    rot.Pitch = (float)(pitch * 180 / Pi);
    rot.Yaw = (float)(yaw * 180 / Pi);
    rot.Roll = (float)(std::atan2(dot(u, flatRight), dot(rt, flatRight)) * 180 / Pi);
}

void hookViewOffset(void *, int pass, FRotatorF *rotation, float worldToMeters, void *location)
{
    // The eye stays at the camera (plus the player's camera-key offsets in a race); the side
    // views turn by their panel's angle, both in the camera's own frame.
    int p = panelOf(pass);
    if (p < 0 || !rotation) return;
    double tiltUp = 0;
    if (inGameplay()) {
        const CameraOffsets c = cameraNow();
        if (location && (c.x || c.y || c.z)) {
            V3 f, rt, u;
            cameraAxes(*rotation, f, rt, u);
            const double s = worldToMeters > 0 ? worldToMeters : 100;
            const V3 d = (f * c.x + rt * c.y + u * c.z) * s;
            auto *v = (FVectorF *)location;
            v->X += (float)d.x; v->Y += (float)d.y; v->Z += (float)d.z;
        }
        tiltUp = -c.tilt;                     // tilt down = look down
    }
    const double yaw = panelFor(p).yaw;
    if (yaw != 0 || tiltUp != 0) turnInCameraFrame(*rotation, yaw, tiltUp);
}
FMatrixF *hookProjection(const void *self, FMatrixF *out, int pass, float fov)
{
    // The stock function gives the engine's near plane (M[3][2]) for a non-mono pass.
    g_origProj(self, out, 1, fov);
    float nearZ = out->M[3][2];
    int p = panelOf(pass);
    if (p < 0) return out;
    if (inGameplay()) fov = std::min(120.f, std::max(20.f, fov + cameraNow().fov));
    if (std::fabs(fov - g_panelFov.load()) > 0.01f) computePanels(fov);
    Frustum f = panelFor(p);
    FMatrixF m{};
    m.M[0][0] = (float)(2 / (f.r - f.l));
    m.M[1][1] = (float)(2 / (f.t - f.b));
    m.M[2][0] = (float)(-(f.r + f.l) / (f.r - f.l));
    m.M[2][1] = (float)(-(f.t + f.b) / (f.t - f.b));
    m.M[2][3] = 1;
    m.M[3][2] = nearZ;                    // reversed-Z, infinite far, as the engine builds it
    *out = m;
    return out;
}

// ------------------------------------------------------- image scanning
struct Section { BYTE *lo, *hi; };
Section g_text{}, g_rdata{};
BYTE *g_image = nullptr;

void findSections()
{
    g_image = (BYTE *)GetModuleHandleW(nullptr);
    auto *nt = (IMAGE_NT_HEADERS *)(g_image + ((IMAGE_DOS_HEADER *)g_image)->e_lfanew);
    auto *s = IMAGE_FIRST_SECTION(nt);
    for (int i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++s) {
        char name[9] = {}; memcpy(name, s->Name, 8);
        Section sec{g_image + s->VirtualAddress, g_image + s->VirtualAddress + s->Misc.VirtualSize};
        if (!strcmp(name, ".text")) g_text = sec;
        else if (!strcmp(name, ".rdata")) g_rdata = sec;
    }
}

// Pattern: hex bytes, "?" = any byte. Returns the single match, or nullptr (logs count).
BYTE *findUnique(const char *what, const char *pattern)
{
    std::vector<int> pat;
    for (const char *p = pattern; *p;) {
        while (*p == ' ') ++p;
        if (!*p) break;
        if (*p == '?') { pat.push_back(-1); ++p; if (*p == '?') ++p; continue; }
        pat.push_back((int)strtoul(std::string(p, 2).c_str(), nullptr, 16)); p += 2;
    }
    if (pat.empty() || !g_text.lo) return nullptr;
    BYTE *found = nullptr; int count = 0;
    const size_t n = pat.size();
    for (BYTE *b = g_text.lo; b + n <= g_text.hi; ++b) {
        if (b[0] != pat[0]) continue;
        size_t k = 1;
        for (; k < n; ++k) if (pat[k] >= 0 && b[k] != (BYTE)pat[k]) break;
        if (k == n) { if (!found) found = b; ++count; }
    }
    if (count != 1) { early("[triple] %s: %d matches, staying stock", what, count); return nullptr; }
    return found;
}

bool writeBytes(void *at, const void *bytes, size_t n)
{
    DWORD old;
    if (!VirtualProtect(at, n, PAGE_EXECUTE_READWRITE, &old)) return false;
    memcpy(at, bytes, n);
    VirtualProtect(at, n, old, &old);
    FlushInstructionCache(GetCurrentProcess(), at, n);
    return true;
}

// UGameViewportClient::Draw:  NumViews = stereo ? (monoFarField ? 3 : 2) : 1
//   cmp byte [rbp+mono.enabled],0 / je / cmp dword [rbp+mono.mode],0 / je /
//   mov eax,esi / mov cl,1 / test cl,cl / setne al / add eax,2 / jmp
//   xor ecx,ecx / mov eax,esi / test cl,cl / setne al / add eax,2 / jmp / mov eax,1
// The second "xor ecx,ecx" becomes "mov cl,1".
BYTE *findViewCount()
{
    BYTE *at = findUnique("view count",
        "80 BD ? ? ? ? 00 74 17 83 BD ? ? ? ? 00 74 0E 8B C6 B1 01 84 C9 0F 95 C0 83 C0 02 EB 13 "
        "33 C9 8B C6 84 C9 0F 95 C0 83 C0 02 EB 05 B8 01 00 00 00");
    return at ? at + 32 : nullptr;
}

// FFakeStereoRenderingDevice constructor: lea rax,[vtable] / mov [rcx],rax / xor ebp,ebp /
// mov rax,gs:[58h] / mov rbx,rcx / mov qword [rcx+10h],42C80000h (FOV 100) /
// mov dword [rcx+18h],280h / mov dword [rcx+1Ch],1E0h (640x480)
void **findFakeVtable()
{
    BYTE *at = findUnique("fake stereo device",
        "48 8D 05 ? ? ? ? 48 89 01 33 ED 65 48 8B 04 25 58 00 00 00 48 8B D9 "
        "48 C7 41 10 00 00 C8 42 C7 41 18 80 02 00 00 C7 41 1C E0 01 00 00");
    if (!at) return nullptr;
    int32_t rel; memcpy(&rel, at + 3, 4);
    void **vt = (void **)(at + 7 + rel);
    if ((BYTE *)vt < g_rdata.lo || (BYTE *)(vt + 8) > g_rdata.hi) { early("[triple] vtable %p outside .rdata", (void *)vt); return nullptr; }
    // IsStereoEnabled and EnableStereo: mov al,1 / ret. AdjustViewRect: mov rax,[rsp+28h] / shr dword [rax],1
    static const BYTE retTrue[3] = {0xB0, 0x01, 0xC3}, adjust[7] = {0x48, 0x8B, 0x44, 0x24, 0x28, 0xD1, 0x28};
    if (memcmp(vt[0], retTrue, 3) || memcmp(vt[2], retTrue, 3) || memcmp(vt[3], adjust, 7)) {
        early("[triple] vtable %p does not look like the fake stereo device", (void *)vt); return nullptr;
    }
    return vt;
}

// ------------------------------------------------------ UI on the centre
// All of the game's UI (UMG viewport and player widgets, tooltips) lives under the
// SDPIScaler of SGameLayerManager, which lays its child out over the whole window. Its
// OnArrangeChildren (vtable slot 64) gets a copy of the window geometry cut to the centre
// third, so menus and the HUD lay out as on one 2560x1440 screen. FGeometry (4.17):
// Size 0x00, Scale 0x08, AbsolutePosition 0x0C, Position 0x14, AccumulatedRenderTransform
// 0x1C (2x2 matrix, then translation at 0x2C), bHasRenderTransform 0x34.
constexpr int SlotArrange = 64, SlotLayoutScale = 44;
using ArrangeFn = void (*)(const void *, const void *, void *);
ArrangeFn g_origArrange = nullptr;

std::atomic<int> g_arrangeLogs{0};
void hookArrange(const void *self, const void *geometry, void *arranged)
{
    float g[14];
    memcpy(g, geometry, sizeof g);
    const float w = g[0], h = g[1];
    static float lastW = -1, lastH = -1;
    if ((w != lastW || h != lastH) && g_arrangeLogs < 12) {
        lastW = w; lastH = h; ++g_arrangeLogs;
        logf("[triple] dpi arrange %p: %.0fx%.0f scale %.3f abs %.0f,%.0f m %.3f %.3f t %.0f,%.0f", self, w, h, g[2], g[3], g[4], g[7], g[10], g[11], g[12]);
    }
    if (!(h > 0 && w >= 4 * h)) { g_origArrange(self, geometry, arranged); return; }
    const float dx = w / 3;                // local units
    g[0] = dx;                             // Size.X
    g[3] += dx * g[2];                     // AbsolutePosition.X (scale only, no rotation here)
    g[5] += dx;                            // Position.X
    g[11] += dx * g[7];                    // translation.X += dx * M00
    g[12] += dx * g[8];                    // translation.Y += dx * M01
    alignas(8) unsigned char copy[0x38];
    memcpy(copy, geometry, sizeof copy);
    memcpy(copy, g, sizeof g);
    g_origArrange(self, copy, arranged);
}

// SDPIScaler constructor: call SPanel ctor / xor esi,esi / lea rax,[vtable] / mov [rdi],rax /
// lea rcx,[rdi+388h] / mov [rdi+330h],esi
void **findDpiScalerVtable()
{
    BYTE *at = findUnique("dpi scaler", "E8 ? ? ? ? 33 F6 48 8D 05 ? ? ? ? 48 89 07 48 8D 8F 88 03 00 00 89 B7 30 03 00 00");
    if (!at) return nullptr;
    int32_t rel; memcpy(&rel, at + 10, 4);
    void **vt = (void **)(at + 14 + rel);
    if ((BYTE *)vt < g_rdata.lo || (BYTE *)(vt + SlotArrange + 1) > g_rdata.hi) { early("[triple] dpi vtable outside .rdata"); return nullptr; }
    static const BYTE arrange[18] = {0x40, 0x55, 0x56, 0x57, 0x41, 0x56, 0x48, 0x8D, 0x6C, 0x24, 0x98, 0x48, 0x81, 0xEC, 0x68, 0x01, 0x00, 0x00};
    static const BYTE scale[11] = {0x48, 0x83, 0xEC, 0x28, 0x48, 0x81, 0xC1, 0x30, 0x03, 0x00, 0x00};
    if (memcmp(vt[SlotArrange], arrange, sizeof arrange) || memcmp(vt[SlotLayoutScale], scale, sizeof scale)) {
        early("[triple] dpi vtable %p does not look like SDPIScaler", (void *)vt); return nullptr;
    }
    return vt;
}

bool patchDevice(void **vt)
{
    g_origOffset = (OffsetFn)vt[5];
    g_origProj = (ProjFn)vt[6];
    void *slots[3] = {(void *)&hookAdjustViewRect, (void *)&hookViewOffset, (void *)&hookProjection};
    return writeBytes(&vt[3], &slots[0], sizeof(void *)) &&
           writeBytes(&vt[5], &slots[1], sizeof(void *)) &&
           writeBytes(&vt[6], &slots[2], sizeof(void *));
}

// Inline hook: the first `len` bytes of `fn` (whole, position-independent instructions)
// move to a trampoline that continues at fn + len; fn itself jumps to `hook`.
void *inlineHook(BYTE *fn, size_t len, void *hook)
{
    BYTE *tramp = (BYTE *)VirtualAlloc(nullptr, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!tramp) return nullptr;
    BYTE jmp[14] = {0xFF, 0x25, 0, 0, 0, 0};           // jmp qword [rip+0]
    uint64_t to = (uint64_t)(fn + len);
    memcpy(tramp, fn, len);
    memcpy(jmp + 6, &to, 8); memcpy(tramp + len, jmp, 14);
    to = (uint64_t)hook; memcpy(jmp + 6, &to, 8);
    if (!writeBytes(fn, jmp, sizeof jmp)) return nullptr;
    return tramp;
}

// ------------------------------------------------------ display requests
// FSystemResolution::RequestResolutionChange(int32 ResX, int32 ResY, EWindowMode::Type Mode)
// is where every resolution/fullscreen request goes (saved settings, the video menu,
// Alt+Enter, r.SetRes). On a triple layout the request becomes the one the layout needs, so
// the game never changes a display mode there (STD-008): separate monitors -> the span size,
// windowed (spanThread makes that window borderless over the monitors); Surround -> the desktop
// size, with exclusive fullscreen turned into windowed fullscreen.
using ResFn = void (*)(int, int, int);
ResFn g_origRes = nullptr;
int g_resW = 0, g_resH = 0; bool g_resSeparate = false;
std::atomic<int> g_resLogs{0};
void hookRequestRes(int x, int y, int mode)
{
    int nx = g_resW, ny = g_resH, nm = g_resSeparate ? 2 : (mode == 0 ? 1 : mode);
    if (g_resLogs < 16) { ++g_resLogs; logf("[triple] resolution request %dx%d mode %d -> %dx%d mode %d", x, y, mode, nx, ny, nm); }
    g_origRes(nx, ny, nm);
}

// UGameViewportClient::Draw clears the window black where no view draws, except in stereo
// ("the HMD clears"): cmp dword [views[0]+StereoPass],0 / je / mov cl,1 ... With the side
// views shrunk in menus that leaves the side screens uncleared, so the je becomes a jmp.
// In gameplay the three thirds cover the window exactly and the clear never runs.
BYTE *findStereoClear()
{
    BYTE *at = findUnique("stereo clear", "48 8B 00 83 B8 50 08 00 00 00 74 04 B1 01 EB 02 32 C9 48 8B 44 24 ? F6 40 4C 04");
    return at ? at + 10 : nullptr;
}

BYTE *findRequestRes()
{
    return findUnique("resolution request",
        "48 89 5C 24 08 48 89 74 24 10 48 89 7C 24 18 4C 89 74 24 20 55 48 8B EC 48 83 EC 60 33 DB "
        "48 8D 3D ? ? ? ? 48 89 5D E0 8B F2 48 89 5D E8 44 8B F1 45 85 C0");
}

// ------------------------------------------------------------ command line
std::wstring g_commandLine;
LPWSTR WINAPI extendedCommandLine() { return &g_commandLine[0]; }

bool patchImport(const char *dll, const char *name, void *replacement, void **original = nullptr)
{
    auto *nt = (IMAGE_NT_HEADERS *)(g_image + ((IMAGE_DOS_HEADER *)g_image)->e_lfanew);
    const auto &dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dir.VirtualAddress) return false;
    for (auto *imp = (IMAGE_IMPORT_DESCRIPTOR *)(g_image + dir.VirtualAddress); imp->Name; ++imp) {
        if (_stricmp((const char *)(g_image + imp->Name), dll) || !imp->OriginalFirstThunk) continue;
        auto *names = (IMAGE_THUNK_DATA *)(g_image + imp->OriginalFirstThunk);
        auto *slots = (IMAGE_THUNK_DATA *)(g_image + imp->FirstThunk);
        for (; names->u1.AddressOfData; ++names, ++slots) {
            if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)) continue;
            if (strcmp((const char *)((IMAGE_IMPORT_BY_NAME *)(g_image + names->u1.AddressOfData))->Name, name)) continue;
            if (original) *original = (void *)slots->u1.Function;
            ULONGLONG fn = (ULONGLONG)replacement;
            return writeBytes(&slots->u1.Function, &fn, sizeof fn);
        }
    }
    return false;
}

// ------------------------------------------------------------- displays
struct Layout { bool triple = false, separate = false; RECT span{}; int w = 0, h = 0; };

BOOL CALLBACK collectMonitor(HMONITOR m, HDC, LPRECT, LPARAM p)
{
    MONITORINFO mi{sizeof mi};
    if (GetMonitorInfoW(m, &mi)) ((std::vector<RECT> *)p)->push_back(mi.rcMonitor);
    return TRUE;
}

// Surround/Eyefinity: the primary display alone is at least 4:1 (three 16:9 panels are
// 5.3:1; one 32:9 monitor is 3.6:1 and stays stock). Separate: three equal displays side
// by side on one row (the owner's "Sim Racing" layout), spanned by one borderless window.
Layout detectLayout()
{
    Layout l;
    l.w = GetSystemMetrics(SM_CXSCREEN); l.h = GetSystemMetrics(SM_CYSCREEN);
    if (l.h > 0 && l.w >= 4 * l.h) { l.triple = true; l.span = {0, 0, l.w, l.h}; return l; }
    std::vector<RECT> mons;
    EnumDisplayMonitors(nullptr, nullptr, collectMonitor, (LPARAM)&mons);
    if (mons.size() != 3) return l;
    std::sort(mons.begin(), mons.end(), [](const RECT &a, const RECT &b) { return a.left < b.left; });
    const int w0 = mons[0].right - mons[0].left, h0 = mons[0].bottom - mons[0].top;
    for (int i = 0; i < 3; ++i) {
        if (mons[i].right - mons[i].left != w0 || mons[i].bottom - mons[i].top != h0 || mons[i].top != mons[0].top) return l;
        if (i && mons[i].left != mons[i - 1].right) return l;
    }
    l.triple = l.separate = true;
    l.span = {mons[0].left, mons[0].top, mons[2].right, mons[2].bottom};
    return l;
}

// --------------------------------------------------------------- window
Layout g_layout;

// Separate monitors: the game's GetMonitorInfoW and GetSystemMetrics (its imports, not ours)
// report each of the three monitors, and the primary display size, as the whole span, as on
// Surround. Without this the engine clamps a windowed window to the primary monitor's work area
// at every race load (the window then flashes onto one monitor until spanThread moves it back).
// The mod's own calls see the real monitors.
BOOL WINAPI hookGetMonitorInfoW(HMONITOR monitor, LPMONITORINFO info)
{
    BOOL ok = GetMonitorInfoW(monitor, info);
    if (ok && info && info->cbSize >= sizeof(MONITORINFO)) {
        const RECT &m = info->rcMonitor, &s = g_layout.span;
        if (m.left >= s.left && m.right <= s.right && m.top >= s.top && m.bottom <= s.bottom) {
            info->rcMonitor = s;
            info->rcWork = s;
        }
    }
    return ok;
}
int WINAPI hookGetSystemMetrics(int index)
{
    if (index == SM_CXSCREEN) return g_layout.span.right - g_layout.span.left;
    if (index == SM_CYSCREEN) return g_layout.span.bottom - g_layout.span.top;
    return GetSystemMetrics(index);
}
// ----------------------------------------------------- exclusive fullscreen
// On a triple layout the game must not take exclusive fullscreen: DXGI would move the window
// onto one monitor and the engine would keep a one-monitor back buffer (seen at race load on
// separate monitors). The game's DXGI factory is wrapped so every swap chain it creates is
// windowed and SetFullscreenState(TRUE) is refused but reported as granted (GetFullscreenState
// answers what the engine asked for), so the engine's own state stays consistent (STD-008).
using CreateFactoryFn = HRESULT(WINAPI *)(REFIID, void **);
using CreateSwapChainFn = HRESULT(STDMETHODCALLTYPE *)(void *, IUnknown *, DXGI_SWAP_CHAIN_DESC *, void **);
using SetFsFn = HRESULT(STDMETHODCALLTYPE *)(void *, BOOL, void *);
using GetFsFn = HRESULT(STDMETHODCALLTYPE *)(void *, BOOL *, void **);
CreateFactoryFn g_origFactory = nullptr, g_origFactory1 = nullptr;
CreateSwapChainFn g_origCreateSwapChain = nullptr;
SetFsFn g_origSetFs = nullptr;
GetFsFn g_origGetFs = nullptr;
std::atomic<int> g_fsWanted{0}, g_fsLogs{0};

bool patchSlot(void *object, int slot, void *hook, void **original)
{
    void **vt = *(void ***)object;
    if (vt[slot] == hook) return true;
    if (original && !*original) *original = vt[slot];
    return writeBytes(&vt[slot], &hook, sizeof hook);
}

HRESULT STDMETHODCALLTYPE hookSetFs(void *swapChain, BOOL fullscreen, void *output)
{
    if (fullscreen) {
        g_fsWanted = 1;
        if (g_fsLogs++ < 8) logf("[triple] exclusive fullscreen refused (window stays over the triple layout)");
        return S_OK;
    }
    g_fsWanted = 0;
    return g_origSetFs(swapChain, FALSE, output);
}

HRESULT STDMETHODCALLTYPE hookGetFs(void *swapChain, BOOL *fullscreen, void **output)
{
    HRESULT hr = g_origGetFs(swapChain, fullscreen, output);
    if (SUCCEEDED(hr) && fullscreen && g_fsWanted) *fullscreen = TRUE;
    return hr;
}

// IDXGIFactory::CreateSwapChain (slot 10); IDXGISwapChain::SetFullscreenState 10, GetFullscreenState 11.
HRESULT STDMETHODCALLTYPE hookCreateSwapChain(void *factory, IUnknown *device, DXGI_SWAP_CHAIN_DESC *desc, void **swapChain)
{
    if (desc && !desc->Windowed) { desc->Windowed = TRUE; logf("[triple] swap chain created windowed (game asked for fullscreen)"); }
    HRESULT hr = g_origCreateSwapChain(factory, device, desc, swapChain);
    if (SUCCEEDED(hr) && swapChain && *swapChain) {
        patchSlot(*swapChain, 10, (void *)&hookSetFs, (void **)&g_origSetFs);
        patchSlot(*swapChain, 11, (void *)&hookGetFs, (void **)&g_origGetFs);
    }
    return hr;
}

HRESULT wrapFactory(HRESULT hr, void **factory)
{
    if (SUCCEEDED(hr) && factory && *factory) patchSlot(*factory, 10, (void *)&hookCreateSwapChain, (void **)&g_origCreateSwapChain);
    return hr;
}
HRESULT WINAPI hookCreateFactory(REFIID riid, void **factory) { return wrapFactory(g_origFactory(riid, factory), factory); }
HRESULT WINAPI hookCreateFactory1(REFIID riid, void **factory) { return wrapFactory(g_origFactory1(riid, factory), factory); }
// No display mode changes on a triple layout (STD-008): every request is already windowed
// fullscreen, so a mode change here would be unexpected; it is refused and logged.
std::atomic<int> g_modeLogs{0};
LONG WINAPI hookChangeDisplaySettingsW(DEVMODEW *mode, DWORD flags)
{
    if (!mode) return DISP_CHANGE_SUCCESSFUL;   // "apply the registry settings" is a display change too: refused
    if (g_modeLogs++ < 8) logf("[triple] refused display mode change %lux%lu (flags %lx)", mode->dmPelsWidth, mode->dmPelsHeight, flags);
    return DISP_CHANGE_FAILED;
}
LONG WINAPI hookChangeDisplaySettingsExW(LPCWSTR device, DEVMODEW *mode, HWND hwnd, DWORD flags, LPVOID param)
{
    if (!mode) return DISP_CHANGE_SUCCESSFUL;
    if (g_modeLogs++ < 8) logf("[triple] refused display mode change %lux%lu on %ls (flags %lx)", mode->dmPelsWidth, mode->dmPelsHeight, device ? device : L"primary", flags);
    return DISP_CHANGE_FAILED;
}

struct FindCtx { DWORD pid; HWND hwnd; };
BOOL CALLBACK findGameWindow(HWND h, LPARAM p)
{
    auto *c = (FindCtx *)p;
    DWORD pid = 0; GetWindowThreadProcessId(h, &pid);
    if (pid != c->pid || !IsWindowVisible(h)) return TRUE;
    wchar_t cls[64] = {}; GetClassNameW(h, cls, 64);
    if (wcscmp(cls, L"UnrealWindow")) return TRUE;
    c->hwnd = h; return FALSE;
}

// Separate monitors: every resolution request is already the span size windowed (see
// hookRequestRes); this keeps that window borderless over the three monitors, without
// activating it. It acts only when the window differs, at most once every 2 s.
DWORD WINAPI spanThread(LPVOID)
{
    const RECT s = g_layout.span;
    int fixes = 0;
    ULONGLONG lastFix = 0;
    for (;;) {
        Sleep(500);
        if (GetTickCount64() - lastFix < 2000) continue;
        FindCtx c{GetCurrentProcessId(), nullptr};
        EnumWindows(findGameWindow, (LPARAM)&c);
        if (!c.hwnd) continue;
        RECT r; GetWindowRect(c.hwnd, &r);
        LONG style = GetWindowLongW(c.hwnd, GWL_STYLE);
        bool bordered = (style & (WS_CAPTION | WS_THICKFRAME)) != 0;
        if (!bordered && r.left == s.left && r.top == s.top && r.right == s.right && r.bottom == s.bottom) continue;
        if (IsIconic(c.hwnd)) continue;
        SetWindowLongW(c.hwnd, GWL_STYLE, (style & ~(WS_CAPTION | WS_THICKFRAME | WS_SYSMENU | WS_MAXIMIZEBOX | WS_MINIMIZEBOX)) | WS_POPUP);
        SetWindowPos(c.hwnd, nullptr, s.left, s.top, s.right - s.left, s.bottom - s.top,
                     SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
        ++fixes; lastFix = GetTickCount64();
        if (fixes <= 10) logf("[triple] window %p %ld,%ld %ldx%ld%s -> borderless %ld,%ld %ldx%ld (fix %d)", (void *)c.hwnd,
             r.left, r.top, r.right - r.left, r.bottom - r.top, bordered ? " bordered" : "",
             s.left, s.top, s.right - s.left, s.bottom - s.top, fixes);
    }
    return 0;
}

// ------------------------------------------------------- camera key thread
// Numpad keys while the game window is in front (GetAsyncKeyState edges, auto-repeat after
// 400 ms), plus a dev command file beside the exe for unattended tests: dbce-camera-cmd.txt,
// one "forward|back|up|down|left|right|tiltdown|tiltup|wider|narrower|reset [count]" per line,
// consumed and deleted. Changes are saved to [camera] 2 s after the last one.
enum CamAction { CamForward, CamBack, CamUp, CamDown, CamLeft, CamRight, CamTiltDown, CamTiltUp, CamWider, CamNarrower, CamReset, CamCount };
const int g_camKeys[CamCount] = {VK_NUMPAD8, VK_NUMPAD2, VK_NUMPAD9, VK_NUMPAD3, VK_NUMPAD4, VK_NUMPAD6, VK_NUMPAD7, VK_NUMPAD1, VK_ADD, VK_SUBTRACT, VK_NUMPAD5};
const char *g_camNames[CamCount] = {"forward", "back", "up", "down", "left", "right", "tiltdown", "tiltup", "wider", "narrower", "reset"};

void applyCamera(int action, int count)
{
    AcquireSRWLockExclusive(&g_camLock);
    CameraOffsets &c = g_cam;
    for (int i = 0; i < count; ++i) {
        switch (action) {
        case CamForward: c.x += g_moveStep; break;
        case CamBack: c.x -= g_moveStep; break;
        case CamUp: c.z += g_moveStep; break;
        case CamDown: c.z -= g_moveStep; break;
        case CamLeft: c.y -= g_moveStep; break;
        case CamRight: c.y += g_moveStep; break;
        case CamTiltDown: c.tilt += g_tiltStep; break;
        case CamTiltUp: c.tilt -= g_tiltStep; break;
        case CamWider: c.fov += g_fovStep; break;
        case CamNarrower: c.fov -= g_fovStep; break;
        case CamReset: c = CameraOffsets{}; break;
        }
    }
    c.x = std::max(-1.f, std::min(1.f, c.x)); c.y = std::max(-1.f, std::min(1.f, c.y)); c.z = std::max(-1.f, std::min(1.f, c.z));
    c.tilt = std::max(-30.f, std::min(30.f, c.tilt)); c.fov = std::max(-40.f, std::min(40.f, c.fov));
    ReleaseSRWLockExclusive(&g_camLock);
}

void saveCamera()
{
    char ini[MAX_PATH]; sidecarPath(ini, "milestone_mod.ini");
    const CameraOffsets c = cameraNow();
    char b[32];
    snprintf(b, sizeof b, "%.3f", c.x); WritePrivateProfileStringA("camera", "forward_m", b, ini);
    snprintf(b, sizeof b, "%.3f", c.y); WritePrivateProfileStringA("camera", "right_m", b, ini);
    snprintf(b, sizeof b, "%.3f", c.z); WritePrivateProfileStringA("camera", "up_m", b, ini);
    snprintf(b, sizeof b, "%.1f", c.tilt); WritePrivateProfileStringA("camera", "tilt_down_deg", b, ini);
    snprintf(b, sizeof b, "%.1f", c.fov); WritePrivateProfileStringA("camera", "fov_offset_deg", b, ini);
    logf("[camera] saved forward %.2f right %.2f up %.2f m, tilt %.1f, fov %+.1f", c.x, c.y, c.z, c.tilt, c.fov);
}

bool gameInFront()
{
    DWORD pid = 0;
    HWND h = GetForegroundWindow();
    return h && GetWindowThreadProcessId(h, &pid) && pid == GetCurrentProcessId();
}

DWORD WINAPI cameraThread(LPVOID)
{
    char cmdPath[MAX_PATH]; sidecarPath(cmdPath, "dbce-camera-cmd.txt");
    ULONGLONG down[CamCount] = {}, saveAt = 0;
    for (;;) {
        Sleep(30);
        const ULONGLONG now = GetTickCount64();
        bool changed = false;
        if (gameInFront() && inGameplay()) {
            for (int a = 0; a < CamCount; ++a) {
                const bool held = (GetAsyncKeyState(g_camKeys[a]) & 0x8000) != 0;
                if (!held) { down[a] = 0; continue; }
                if (!down[a]) { down[a] = now; applyCamera(a, 1); changed = true; }
                else if (a != CamReset && now - down[a] > 400) { applyCamera(a, 1); changed = true; down[a] = now - 340; }
            }
        }
        if (GetFileAttributesA(cmdPath) != INVALID_FILE_ATTRIBUTES) {
            if (FILE *f = fopen(cmdPath, "r")) {
                char line[96];
                while (fgets(line, sizeof line, f)) {
                    char word[32] = ""; int count = 1;
                    if (sscanf(line, "%31s %d", word, &count) < 1) continue;
                    for (int a = 0; a < CamCount; ++a)
                        if (!_stricmp(word, g_camNames[a])) { applyCamera(a, std::max(1, std::min(100, count))); changed = true; logf("[camera] command %s %d", word, count); }
                }
                fclose(f);
            }
            DeleteFileA(cmdPath);
        }
        if (changed) saveAt = now + 2000;
        if (saveAt && now >= saveAt) { saveAt = 0; saveCamera(); }
    }
}

// ---------------------------------------------------------------- config
void readTripleConfig()
{
    char ini[MAX_PATH]; sidecarPath(ini, "milestone_mod.ini");
    char v[32] = "";
    GetPrivateProfileStringA("triple", "mode", "auto", v, sizeof v, ini);
    g_tc.mode = !_stricmp(v, "on") ? Mode::On : !_stricmp(v, "off") ? Mode::Off : Mode::Auto;
    GetPrivateProfileStringA("triple", "fov", "game", v, sizeof v, ini);
    g_tc.fov = !_stricmp(v, "rig") ? FovSource::Rig : FovSource::Game;
    auto num = [&](const char *key, double def) {
        char b[32] = ""; GetPrivateProfileStringA("triple", key, "", b, sizeof b, ini);
        return b[0] ? atof(b) : def;
    };
    g_tc.panelW = num("panel_width_mm", g_tc.panelW);
    g_tc.panelH = num("panel_height_mm", g_tc.panelH);
    g_tc.eye = num("eye_distance_mm", g_tc.eye);
    g_tc.vfov = num("vertical_fov", g_tc.vfov);
    g_tc.side = num("side_angle", g_tc.side);
    g_tc.bezel = num("bezel_mm", g_tc.bezel);
    g_tc.centre = (int)num("centre_pass", g_tc.centre);
    g_tc.right = (int)num("right_pass", g_tc.right);
    g_tc.left = (int)num("left_pass", g_tc.left);
    g_tc.span = num("span_window", 1) != 0;
    g_tc.uiCentre = num("ui_centre", 1) != 0;
    g_tc.menuSidesBlack = num("menu_sides_black", 1) != 0;
    auto cam = [&](const char *key, double def) {
        char b[32] = ""; GetPrivateProfileStringA("camera", key, "", b, sizeof b, ini);
        return b[0] ? (float)atof(b) : (float)def;
    };
    g_moveStep = cam("move_step_m", 0.02); g_tiltStep = cam("tilt_step_deg", 1); g_fovStep = cam("fov_step_deg", 2);
    g_cam.x = cam("forward_m", 0); g_cam.y = cam("right_m", 0); g_cam.z = cam("up_m", 0);
    g_cam.tilt = cam("tilt_down_deg", 0); g_cam.fov = cam("fov_offset_deg", 0);
}

} // namespace

// Called from DllMain (DLL_PROCESS_ATTACH): dinput8.dll is a static import of the game, so
// this runs before the game's entry point reads its command line. Only memory writes and
// kernel32/user32 queries here - no LoadLibrary, no waiting.
void tripleAttach()
{
    findSections();
    readTripleConfig();
    if (g_tc.mode == Mode::Off) return;
    g_layout = detectLayout();
    if (g_tc.mode == Mode::Auto && !g_layout.triple) return;
    std::wstring line = GetCommandLineW(), lower = line;
    for (auto &ch : lower) ch = (wchar_t)towlower(ch);
    bool hasStereo = lower.find(L"-emulatestereo") != std::wstring::npos;
    // Find everything before writing anything, so a changed exe stays fully stock.
    BYTE *count = findViewCount();
    void **vt = findFakeVtable();
    if (!count || !vt) return;
    BYTE *res = findRequestRes();
    if (!res) return;
    static const BYTE movCl1[2] = {0xB1, 0x01};
    if (!patchDevice(vt) || !writeBytes(count, movCl1, 2)) { early("[triple] patch write failed"); return; }
    g_resW = g_layout.span.right - g_layout.span.left; g_resH = g_layout.span.bottom - g_layout.span.top; g_resSeparate = g_layout.separate;
    if (g_layout.triple && g_resW > 0 && g_resH > 0 && !(g_origRes = (ResFn)inlineHook(res, 15, (void *)&hookRequestRes)))
        early("[triple] resolution hook failed");
    if (g_tc.menuSidesBlack) {
        static const BYTE jmp = 0xEB;
        BYTE *clear = findStereoClear();
        if (clear && *clear == 0x74) writeBytes(clear, &jmp, 1);
        else g_tc.menuSidesBlack = false;           // without the clear the sides would show garbage
    }
    if (g_tc.uiCentre) {
        if (void **dpi = findDpiScalerVtable()) {
            g_origArrange = (ArrangeFn)dpi[SlotArrange];
            void *hook = (void *)&hookArrange;
            if (!writeBytes(&dpi[SlotArrange], &hook, sizeof hook)) early("[triple] ui hook write failed");
        }
    }
    if (!hasStereo) line += L" -emulatestereo";
    bool spanWindow = g_layout.separate && g_tc.span && lower.find(L"-resx=") == std::wstring::npos;
    if (spanWindow) {
        wchar_t extra[96];
        swprintf(extra, 96, L" -windowed -ResX=%ld -ResY=%ld", g_layout.span.right - g_layout.span.left, g_layout.span.bottom - g_layout.span.top);
        line += extra;
    }
    g_commandLine = line;
    if (!patchImport("KERNEL32.dll", "GetCommandLineW", (void *)&extendedCommandLine)) return;
    if (g_layout.triple) {
        patchImport("USER32.dll", "ChangeDisplaySettingsW", (void *)&hookChangeDisplaySettingsW);
        patchImport("USER32.dll", "ChangeDisplaySettingsExW", (void *)&hookChangeDisplaySettingsExW);
        patchImport("dxgi.dll", "CreateDXGIFactory", (void *)&hookCreateFactory, (void **)&g_origFactory);
        patchImport("dxgi.dll", "CreateDXGIFactory1", (void *)&hookCreateFactory1, (void **)&g_origFactory1);
    }
    if (g_layout.separate && (!patchImport("USER32.dll", "GetMonitorInfoW", (void *)&hookGetMonitorInfoW) ||
                              !patchImport("USER32.dll", "GetSystemMetrics", (void *)&hookGetSystemMetrics)))
        early("[triple] monitor hooks failed");
    g_active = true;
    computePanels(90.f);
    CreateThread(nullptr, 0, cameraThread, nullptr, 0, nullptr);
    if (spanWindow) CreateThread(nullptr, 0, spanThread, nullptr, 0, nullptr);
}

// Logged once logging is up (proxyInit), since DllMain runs before the log exists.
void tripleReport()
{
    size_t from = 0;
    for (size_t nl; (nl = g_early.find('\n', from)) != std::string::npos; from = nl + 1)
        logf("%s", g_early.substr(from, nl - from).c_str());
    g_early.clear();
    if (!g_active) {
        logf("[triple] off (mode %s, layout %s)", g_tc.mode == Mode::Off ? "off" : g_tc.mode == Mode::On ? "on" : "auto",
             g_layout.triple ? (g_layout.separate ? "separate triple" : "surround") : "single");
        return;
    }
    Frustum c = panelFor(1), r = panelFor(2);
    logf("[triple] on: %s %ldx%ld; passes centre %d right %d left %d; fov %s; panel %.1fx%.1f mm, side %.1f deg, bezel %.1f mm",
         g_layout.separate ? "separate monitors, borderless span" : "surround",
         g_layout.span.right - g_layout.span.left, g_layout.span.bottom - g_layout.span.top,
         g_tc.centre, g_tc.right, g_tc.left, g_tc.fov == FovSource::Game ? "game" : "rig", g_tc.panelW, g_tc.panelH, g_tc.side, g_tc.bezel);
    logf("[triple] ui %s; menus %s", g_origArrange ? "on the centre screen" : "across the window (stock)", g_tc.menuSidesBlack ? "centre only (sides black outside gameplay)" : "three views");
    { const CameraOffsets c = cameraNow(); logf("[camera] numpad keys in a race; offsets forward %.2f right %.2f up %.2f m, tilt %.1f, fov %+.1f; steps %.3f m, %.1f, %.1f deg", c.x, c.y, c.z, c.tilt, c.fov, g_moveStep, g_tiltStep, g_fovStep); }
    logf("[triple] at fov 90: centre l%.3f r%.3f b%.3f t%.3f; right yaw %.1f l%.3f r%.3f", c.l, c.r, c.b, c.t, r.yaw, r.l, r.r);
}
