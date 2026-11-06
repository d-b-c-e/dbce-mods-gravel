// Live vehicle values through UE4 reflection - no signatures, no SDK.
//
// UE4 keeps every object in one global array (GUObjectArray) and every name
// in another (GNames). Both are found by scanning the exe's .data section for
// a structure that validates, which survives across builds because it keys
// on the shape of the data, not on code bytes:
//   GNames    a pointer to a chunk table whose first entry is "None" with
//             index 0, followed by "ByteProperty" with index 1.
//   GObjects  an FUObjectItem array whose first items carry InternalIndex
//             0, 1, 2 in order.
//
// A UProperty is itself a UObject named after the field, so the game's own
// HUD variables ("VehicleCurrentRPM", ...) are found by name, their owning
// struct is their Outer, and Offset_Internal says where the value lives
// inside an instance. The HUD is fed the player's vehicle, so its single
// live instance is exactly the telemetry source we want - no need to work
// out which of several vehicles is the player's.
//
// Layouts below are UE 4.17-4.24 (Gravel is 4.17). Chunked GObjects (4.20+)
// is handled too. Every read goes through safeRead, so a wrong guess costs
// a failed validation rather than a crash on the game thread.
#include "common.h"
#include <vector>
#include <algorithm>
#include <cmath>

namespace {

// ---- UObject / UProperty layout (4.17-4.24, x64) ----
const int O_FLAGS = 0x08, O_INDEX = 0x0C, O_CLASS = 0x10, O_NAME = 0x18, O_OUTER = 0x20;
const int S_SUPER = 0x30;                       // UStruct::SuperStruct
const int P_ELEMSIZE = 0x34;                    // UProperty
// Offset_Internal moved when BlueprintReplicationCondition was added in 4.19:
// 4.19+ -> 0x44, 4.11-4.18 -> 0x4C (0x44 there is RepNotifyFunc, usually None=0).
// Gravel (4.17) measured: 0x50. Probed with two sibling properties, which must
// read distinct in-range values - a lone value can pass by accident.
const int P_OFFSET_CANDIDATES[3] = {0x50, 0x4C, 0x44};
const int S_PROPSIZE = 0x40;                    // UStruct::PropertiesSize
int g_pOffset = 0;                              // resolved once, per game
const uint32_t RF_CDO = 0x10, RF_ARCHETYPE = 0x20;
const int NAME_CHUNK = 16384;

BYTE *g_base = nullptr; size_t g_dataOff = 0, g_dataLen = 0;
BYTE *g_names = nullptr;      // TNameEntryArray: FNameEntry** Chunks[]
BYTE *g_objs = nullptr;       // FUObjectItem* (fixed) or FUObjectItem** (chunked)
const int32_t *g_numPtr = nullptr;   // &NumElements inside the descriptor, re-read each poll
int   g_objNum = 0; bool g_chunked = false;
const int ITEM = 24;          // sizeof(FUObjectItem)

std::string nameOfUncached(int32_t idx);
std::string nameOf(int32_t idx)
{
    static std::vector<std::string> cache; static std::vector<uint8_t> have;
    if (idx < 0 || !g_names) return "";
    if (idx < 4000000) {
        if ((size_t)idx >= have.size()) { have.resize(idx + 65536, 0); cache.resize(idx + 65536); }
        if (have[idx]) return cache[idx];
        cache[idx] = nameOfUncached(idx); have[idx] = 1; return cache[idx];
    }
    return nameOfUncached(idx);
}
std::string nameOfUncached(int32_t idx)
{
    if (idx < 0 || !g_names) return "";
    BYTE *chunk; if (!rd(g_names + (idx / NAME_CHUNK) * 8, chunk) || !chunk) return "";
    BYTE *entry; if (!rd(chunk + (idx % NAME_CHUNK) * 8, entry) || !entry) return "";
    int32_t hdr; if (!rd(entry, hdr)) return "";
    if (hdr & 1) {                                     // wide
        wchar_t w[64]; if (!safeRead(entry + 0x10, w, sizeof w)) return "";
        w[63] = 0; char a[64]; for (int i = 0; i < 64; ++i) a[i] = (char)w[i]; return a;
    }
    char a[64]; if (!safeRead(entry + 0x10, a, sizeof a)) return ""; a[63] = 0; return a;
}
std::string objName(BYTE *o)
{
    int32_t n; return (o && rd(o + O_NAME, n)) ? nameOf(n) : "";
}
BYTE *objClass(BYTE *o) { BYTE *c; return (o && rd(o + O_CLASS, c)) ? c : nullptr; }
BYTE *itemAt(int i)
{
    if (i < 0 || i >= g_objNum) return nullptr;
    if (g_chunked) { BYTE *chunk; if (!rd(g_objs + (i / 65536) * 8, chunk) || !chunk) return nullptr; return chunk + (i % 65536) * ITEM; }
    return g_objs + i * ITEM;
}
BYTE *objAt(int i)
{
    BYTE *item = itemAt(i);
    BYTE *o; return (item && rd(item, o)) ? o : nullptr;
}

bool section(const char *want, size_t &off, size_t &len)
{
    auto *dos = (IMAGE_DOS_HEADER *)g_base;
    auto *nt = (IMAGE_NT_HEADERS64 *)(g_base + dos->e_lfanew);
    auto *s = IMAGE_FIRST_SECTION(nt);
    for (int i = 0; i < nt->FileHeader.NumberOfSections; ++i)
        if (strncmp((const char *)s[i].Name, want, 8) == 0) { off = s[i].VirtualAddress; len = s[i].Misc.VirtualSize; return true; }
    return false;
}
bool plausiblePtr(uint64_t v) { return v > 0x10000 && v < 0x7FFFFFFFFFFFull; }

bool findNames(const uint64_t *data, size_t n)
{
    for (size_t i = 0; i < n; ++i) {
        if (!plausiblePtr(data[i])) continue;
        BYTE *G = (BYTE *)data[i], *C, *E; int32_t hdr; char nm[16];
        if (!rd(G, C) || !plausiblePtr((uint64_t)C)) continue;
        if (!rd(C, E) || !plausiblePtr((uint64_t)E)) continue;
        if (!rd(E, hdr) || hdr != 0) continue;
        if (!safeRead(E + 0x10, nm, 5) || memcmp(nm, "None\0", 5) != 0) continue;
        if (!rd(C + 8, E) || !rd(E, hdr) || hdr != 2) continue;
        if (!safeRead(E + 0x10, nm, 13) || memcmp(nm, "ByteProperty\0", 13) != 0) continue;
        g_names = G;
        logf("[ue4] GNames at %p (.data+0x%zx)", (void *)G, i * 8);
        return true;
    }
    return false;
}
bool validItems(BYTE *items, int count)
{
    for (int i = 0; i < count; ++i) {
        BYTE *o; int32_t idx;
        if (!rd(items + i * ITEM, o) || !plausiblePtr((uint64_t)o) || !rd(o + O_INDEX, idx) || idx != i) return false;
    }
    return true;
}
bool findObjects(const uint64_t *data, size_t n)
{
    for (size_t i = 0; i + 2 < n; ++i) {
        if (!plausiblePtr(data[i])) continue;
        // fixed: {Items*, int32 Max, int32 Num}
        int32_t mx = (int32_t)(data[i + 1] & 0xFFFFFFFF), num = (int32_t)(data[i + 1] >> 32);
        if (num > 1000 && num <= mx && mx <= 20000000 && validItems((BYTE *)data[i], 3)) {
            g_objs = (BYTE *)data[i]; g_objNum = num; g_chunked = false;
            g_numPtr = (const int32_t *)(g_base + g_dataOff + (i + 1) * 8 + 4);
            logf("[ue4] GObjects (fixed) at .data+0x%zx: %d/%d objects", i * 8, num, mx);
            return true;
        }
        // chunked: {Items**, PreAllocated*, int32 Max, int32 Num, int32 MaxChunks, int32 NumChunks}
        mx = (int32_t)(data[i + 2] & 0xFFFFFFFF); num = (int32_t)(data[i + 2] >> 32);
        BYTE *chunk0;
        if (num > 1000 && num <= mx && mx <= 20000000 && rd((BYTE *)data[i], chunk0) && plausiblePtr((uint64_t)chunk0) &&
            validItems(chunk0, 3)) {
            g_objs = (BYTE *)data[i]; g_objNum = num; g_chunked = true;
            g_numPtr = (const int32_t *)(g_base + g_dataOff + (i + 2) * 8 + 4);
            logf("[ue4] GObjects (chunked) at .data+0x%zx: %d/%d objects", i * 8, num, mx);
            return true;
        }
    }
    return false;
}
// One bulk read of the item array per scan instead of one syscall per object.
std::vector<BYTE *> snapshot()
{
    std::vector<BYTE *> out;
    int32_t num; if (g_numPtr && rd(g_numPtr, num) && num > 0 && num < 20000000) g_objNum = num;
    out.resize(g_objNum, nullptr);
    std::vector<BYTE> buf;
    if (!g_chunked) {
        buf.resize((size_t)g_objNum * ITEM);
        size_t got = 0;
        while (got < buf.size()) {
            size_t n = std::min<size_t>(65536 * ITEM, buf.size() - got);
            if (!safeRead(g_objs + got, &buf[got], n)) break;
            got += n;
        }
        for (int i = 0; i < g_objNum && (size_t)(i + 1) * ITEM <= got; ++i) out[i] = *(BYTE **)&buf[(size_t)i * ITEM];
    } else {
        for (int c = 0; c * 65536 < g_objNum; ++c) {
            BYTE *chunk; if (!rd(g_objs + c * 8, chunk) || !chunk) break;
            int n = std::min(65536, g_objNum - c * 65536);
            buf.resize((size_t)n * ITEM);
            if (!safeRead(chunk, buf.data(), buf.size())) break;
            for (int i = 0; i < n; ++i) out[c * 65536 + i] = *(BYTE **)&buf[(size_t)i * ITEM];
        }
    }
    return out;
}

// ---- property resolution ----
// Find a second UProperty with the same Outer and pick the slot where both
// carry distinct offsets inside the struct. 0 if nothing fits.
int probeOffsetSlot(BYTE *prop, BYTE *outer, int32_t propsSize)
{
    std::vector<BYTE *> objs = snapshot();
    BYTE *sibling = nullptr;
    for (BYTE *o : objs) {
        if (!o || o == prop) continue;
        BYTE *ou; if (!rd(o + O_OUTER, ou) || ou != outer) continue;
        std::string cls = objName(objClass(o));
        if (cls.size() < 8 || cls.compare(cls.size() - 8, 8, "Property") != 0) continue;
        sibling = o; break;
    }
    for (int c : P_OFFSET_CANDIDATES) {
        int32_t a, b;
        if (!rd(prop + c, a) || a < 0x28 || (propsSize > 0 && a >= propsSize)) continue;
        if (!sibling) return c;
        if (!rd(sibling + c, b) || b < 0x28 || (propsSize > 0 && b >= propsSize) || b == a) continue;
        logf("[ue4] offset slot +0x%x: %s=+0x%x sibling %s=+0x%x", c, objName(prop).c_str(), a, objName(sibling).c_str(), b);
        return c;
    }
    return 0;
}
struct Prop { BYTE *owner = nullptr; int offset = -1; int size = 0; std::string type, ownerName; };
// owner: exact name of the declaring class/struct (no U/A/F prefix), or null for the first match anywhere.
Prop findProp(const char *name, const char *owner = nullptr)
{
    Prop p;
    if (!name[0]) return p;
    std::vector<BYTE *> objs = snapshot();
    for (int i = 0; i < (int)objs.size(); ++i) {
        BYTE *o = objs[i]; if (!o) continue;
        int32_t n; if (!rd(o + O_NAME, n)) continue;
        std::string nm = nameOf(n); if (nm != name) continue;
        std::string cls = objName(objClass(o));
        if (cls.size() < 8 || cls.compare(cls.size() - 8, 8, "Property") != 0) continue;
        int32_t off = -1, sz; BYTE *outer;
        if (!rd(o + P_ELEMSIZE, sz) || !rd(o + O_OUTER, outer)) continue;
        if (owner && objName(outer) != owner) continue;
        int32_t propsSize = 0; rd(outer + S_PROPSIZE, propsSize);
        if (!g_pOffset) {
            g_pOffset = probeOffsetSlot(o, outer, propsSize);
            logf("[ue4] Offset_Internal is at UProperty+0x%x (PropertiesSize %d)", g_pOffset, propsSize);
            if (!g_pOffset) {
                uint32_t w[24]; if (safeRead(o + 0x28, w, sizeof w)) {
                    logf("[ue4] raw %s (%s, outer %s %s) +0x28..: %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x",
                         name, cls.c_str(), objName(outer).c_str(), objName(objClass(outer)).c_str(),
                         w[0], w[1], w[2], w[3], w[4], w[5], w[6], w[7], w[8], w[9], w[10], w[11]);
                    logf("[ue4] raw +0x58..: %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x",
                         w[12], w[13], w[14], w[15], w[16], w[17], w[18], w[19], w[20], w[21], w[22], w[23]);
                }
                continue;
            }
        }
        if (!rd(o + g_pOffset, off)) continue;
        p.owner = outer; p.offset = off; p.size = sz; p.type = cls; p.ownerName = objName(outer);
        logf("[ue4] %s: %s at +0x%x (size %d) in %s", name, cls.c_str(), off, sz, p.ownerName.c_str());
        return p;
    }
    logf("[ue4] property \"%s\"%s%s not found among %d objects", name, owner ? " in " : "", owner ? owner : "", g_objNum);
    return p;
}
bool isA(BYTE *cls, BYTE *want)
{
    for (int depth = 0; cls && depth < 32; ++depth) {
        if (cls == want) return true;
        if (!rd(cls + S_SUPER, cls)) return false;
    }
    return false;
}
// first live (non-default) instance of `owner`; returns its object index
int findInstance(BYTE *owner, BYTE **out)
{
    std::vector<BYTE *> objs = snapshot();
    for (int i = 0; i < (int)objs.size(); ++i) {
        BYTE *o = objs[i]; if (!o) continue;
        uint32_t fl; if (!rd(o + O_FLAGS, fl) || (fl & (RF_CDO | RF_ARCHETYPE))) continue;
        if (!isA(objClass(o), owner)) continue;
        *out = o; return i;
    }
    return -1;
}

// One-shot listing of every property whose name matches a hint, so the
// right names for a new game can be read off the log instead of guessed.
void discoverProps()
{
    static const char *hints[] = {"speed", "rpm", "gear", "throttle", "brake", "steer", "slip", "susp", "wheel"};
    std::vector<BYTE *> objs = snapshot();
    int shown = 0;
    for (int i = 0; i < (int)objs.size() && shown < 200; ++i) {
        BYTE *o = objs[i]; if (!o) continue;
        int32_t n; if (!rd(o + O_NAME, n)) continue;
        std::string nm = nameOf(n); if (nm.size() < 4) continue;
        std::string low = nm; for (auto &ch : low) ch = (char)tolower(ch);
        bool hit = false; for (const char *h : hints) if (low.find(h) != std::string::npos) { hit = true; break; }
        if (!hit) continue;
        std::string cls = objName(objClass(o));
        if (cls.size() < 8 || cls.compare(cls.size() - 8, 8, "Property") != 0) continue;
        BYTE *outer; if (!rd(o + O_OUTER, outer)) continue;
        std::string on = objName(outer), oc = objName(objClass(outer));
        if (oc == "Function") continue;                 // parameters, not state
        int32_t off = -1; if (g_pOffset) rd(o + g_pOffset, off);
        logf("[ue4]   %-40s %-16s +0x%-4x in %s", nm.c_str(), cls.c_str(), off, on.c_str());
        ++shown;
    }
    logf("[ue4] discovery: %d matching properties listed", shown);
}

// Every property belonging to one class, whatever its name. Used on the class
// that owns RPM so a speed/gear field that does not match the name hints is
// still visible in the log.
void dumpClass(BYTE *owner, const char *label)
{
    if (!owner) return;
    std::vector<BYTE *> objs = snapshot();
    int n = 0;
    for (BYTE *o : objs) {
        if (!o) continue;
        BYTE *ou; if (!rd(o + O_OUTER, ou) || ou != owner) continue;
        std::string cls = objName(objClass(o));
        if (cls.size() < 8 || cls.compare(cls.size() - 8, 8, "Property") != 0) continue;
        int32_t off = -1; if (g_pOffset) rd(o + g_pOffset, off);
        logf("[ue4]   %-44s %-16s +0x%x", objName(o).c_str(), cls.c_str(), off);
        ++n;
    }
    logf("[ue4] %s has %d properties", label, n);
}

Prop g_pRpm, g_pMax, g_pSpeed, g_pGear;
std::atomic<bool> g_cameraProbeRequested{false};
BYTE *g_inst = nullptr; int g_instIdx = -1;
ULONGLONG g_nextScan = 0, g_nextDiscover = 0;
bool g_propsResolved = false;

// ---- camera identity probe (explicit developer request only) ----
// Scanning for "camera" objects saturated on MovieScene/CameraAnim objects in a race (stage 4, GO572877), so the probe
// follows the engine's own pointers instead: the one live PlayerController -> PlayerCameraManager -> ViewTarget and
// CameraCache (the POV actually rendered last frame). The camera's position in the view target's frame is the identity
// a per-camera set can key on; component lists and manager fields are logged as evidence, not interpreted.
BYTE *findClass(const char *name)
{
    for (BYTE *o : snapshot()) if (o && objName(o) == name && objName(objClass(o)) == "Class") return o;
    return nullptr;
}
std::string label(BYTE *o) { return o ? objName(o) + " : " + objName(objClass(o)) : std::string("null"); }
bool liveObj(BYTE *o) { uint32_t fl; return o && rd(o + O_FLAGS, fl) && !(fl & (RF_CDO | RF_ARCHETYPE)); }
BYTE *readObj(BYTE *base, const Prop &p)
{
    BYTE *v = nullptr;
    return (base && p.offset >= 0 && rd(base + p.offset, v) && plausiblePtr((uint64_t)v)) ? v : nullptr;
}
// FWeakObjectPtr {ObjectIndex, SerialNumber}: valid only while the item's SerialNumber (+0x10 in 4.17) still matches.
BYTE *readWeak(BYTE *base, const Prop &p)
{
    int32_t w[2], serial; BYTE *item;
    if (!base || p.offset < 0 || !safeRead(base + p.offset, w, sizeof w) || !w[1]) return nullptr;
    if (!(item = itemAt(w[0])) || !rd(item + 0x10, serial) || serial != w[1]) return nullptr;
    return objAt(w[0]);
}
struct View { float loc[3] = {}, rot[3] = {}, fov = 0; bool ok = false; };
struct ViewLayout { Prop loc, rot, fov; bool ok() const { return loc.offset >= 0 && rot.offset >= 0 && fov.offset >= 0; } };
View readView(BYTE *at, const ViewLayout &l)
{
    View v;
    v.ok = at && l.ok() && safeRead(at + l.loc.offset, v.loc, sizeof v.loc) && safeRead(at + l.rot.offset, v.rot, sizeof v.rot) &&
           rd(at + l.fov.offset, v.fov);
    return v;
}
// d expressed in the frame of a UE FRotator {pitch, yaw, roll} (FRotationMatrix axes; cm, X forward, Y right, Z up).
void toLocal(const float d[3], const float rot[3], float out[3])
{
    const float k = 3.14159265f / 180.f;
    float sp = sinf(rot[0] * k), cp = cosf(rot[0] * k), sy = sinf(rot[1] * k), cy = cosf(rot[1] * k), sr = sinf(rot[2] * k), cr = cosf(rot[2] * k);
    const float x[3] = {cp * cy, cp * sy, sp};
    const float y[3] = {sr * sp * cy - cr * sy, sr * sp * sy + cr * cy, -sr * cp};
    const float z[3] = {-(cr * sp * cy + sr * sy), cy * sr - cr * sp * sy, cr * cp};
    out[0] = d[0] * x[0] + d[1] * x[1] + d[2] * x[2];
    out[1] = d[0] * y[0] + d[1] * y[1] + d[2] * y[2];
    out[2] = d[0] * z[0] + d[1] * z[1] + d[2] * z[2];
}
// Values of the manager's own fields (classes below PlayerCameraManager): a Blueprint camera index/mode would show here.
void dumpValues(BYTE *inst, const char *stopAt)
{
    BYTE *cls = objClass(inst);
    const auto objs = snapshot();
    for (int depth = 0; cls && depth < 8 && objName(cls) != stopAt; ++depth) {
        for (BYTE *o : objs) {
            BYTE *ou; if (!o || !rd(o + O_OUTER, ou) || ou != cls) continue;
            std::string type = objName(objClass(o));
            if (type.size() < 8 || type.compare(type.size() - 8, 8, "Property") != 0) continue;
            int32_t off = -1, sz = 0; if (!g_pOffset || !rd(o + g_pOffset, off) || off < 0) continue;
            rd(o + P_ELEMSIZE, sz);
            Prop p; p.offset = off; p.size = sz;
            char val[160] = "?";
            int32_t i32; float f; uint8_t b;
            if (type == "IntProperty" && rd(inst + off, i32)) snprintf(val, sizeof val, "%d", i32);
            else if (type == "FloatProperty" && rd(inst + off, f)) snprintf(val, sizeof val, "%.3f", f);
            else if ((type == "ByteProperty" || type == "EnumProperty") && sz == 1 && rd(inst + off, b)) snprintf(val, sizeof val, "%u", b);
            else if (type == "BoolProperty" && rd(inst + off, b)) snprintf(val, sizeof val, "byte 0x%02x (bitfield mask unread)", b);
            else if (type == "ObjectProperty") snprintf(val, sizeof val, "%s", label(readObj(inst, p)).c_str());
            else if (type == "WeakObjectProperty") snprintf(val, sizeof val, "%s", label(readWeak(inst, p)).c_str());
            else continue;
            logf("[camera-probe]   %s.%s (%s +0x%x) = %s", objName(cls).c_str(), objName(o).c_str(), type.c_str(), off, val);
        }
        if (!rd(cls + S_SUPER, cls)) break;
    }
}
void probeCameras()
{
    BYTE *pcClass = findClass("PlayerController");
    std::vector<BYTE *> pcs;
    if (pcClass) for (BYTE *o : snapshot()) if (liveObj(o) && isA(objClass(o), pcClass)) pcs.push_back(o);
    if (pcs.size() != 1) {
        logf("[camera-probe] %zu live PlayerController instances; need exactly one, no identity", pcs.size());
        for (size_t i = 0; i < pcs.size() && i < 8; ++i) logf("[camera-probe]   controller %s", label(pcs[i]).c_str());
        return;
    }
    BYTE *pc = pcs[0];
    Prop pPcm = findProp("PlayerCameraManager", "PlayerController");
    BYTE *pcm = readObj(pc, pPcm);
    logf("[camera-probe] controller %s -> camera manager %s", label(pc).c_str(), label(pcm).c_str());
    if (!pcm) return;
    dumpValues(pcm, "PlayerCameraManager");
    ViewLayout vl{findProp("Location", "MinimalViewInfo"), findProp("Rotation", "MinimalViewInfo"), findProp("FOV", "MinimalViewInfo")};
    Prop pVt = findProp("ViewTarget", "PlayerCameraManager"), pCache = findProp("CameraCache", "PlayerCameraManager");
    Prop pTarget = findProp("Target", "TViewTarget"), pVtPov = findProp("POV", "TViewTarget"), pCachePov = findProp("POV", "CameraCacheEntry");
    if (!vl.ok() || pVt.offset < 0 || pCache.offset < 0 || pTarget.offset < 0 || pVtPov.offset < 0 || pCachePov.offset < 0) {
        logf("[camera-probe] view struct layout not resolved; no identity");
        return;
    }
    BYTE *vt = pcm + pVt.offset;
    BYTE *target = readObj(vt, pTarget);
    View pov = readView(pcm + pCache.offset + pCachePov.offset, vl), vtPov = readView(vt + pVtPov.offset, vl);
    logf("[camera-probe] view target %s", label(target).c_str());
    if (pov.ok) logf("[camera-probe] rendered POV loc (%.1f %.1f %.1f) rot (%.2f %.2f %.2f) fov %.2f", pov.loc[0], pov.loc[1], pov.loc[2], pov.rot[0], pov.rot[1], pov.rot[2], pov.fov);
    if (vtPov.ok) logf("[camera-probe] view-target POV loc (%.1f %.1f %.1f) fov %.2f", vtPov.loc[0], vtPov.loc[1], vtPov.loc[2], vtPov.fov);
    Prop pRoot = findProp("RootComponent", "Actor"), pRelLoc = findProp("RelativeLocation", "SceneComponent"),
         pRelRot = findProp("RelativeRotation", "SceneComponent"), pFov = findProp("FieldOfView", "CameraComponent");
    BYTE *root = readObj(target, pRoot);
    float rl[3], rr[3];
    if (pov.ok && root && pRelLoc.offset >= 0 && pRelRot.offset >= 0 && safeRead(root + pRelLoc.offset, rl, sizeof rl) && safeRead(root + pRelRot.offset, rr, sizeof rr)) {
        // An unattached root's relative transform is its world transform (vehicle roots are unattached).
        const float d[3] = {pov.loc[0] - rl[0], pov.loc[1] - rl[1], pov.loc[2] - rl[2]};
        float local[3]; toLocal(d, rr, local);
        float dy = pov.rot[1] - rr[1]; while (dy > 180) dy -= 360; while (dy < -180) dy += 360;
        logf("[camera-probe] identity: target class %s, camera in target frame (%.0f %.0f %.0f) cm, pitch %.1f yaw %.1f, fov %.1f (root %s)",
             objName(objClass(target)).c_str(), local[0], local[1], local[2], pov.rot[0] - rr[0], dy, pov.fov, label(root).c_str());
    } else {
        logf("[camera-probe] no identity: rendered POV or target root transform unreadable");
    }
    BYTE *camClass = findClass("CameraComponent");
    int n = 0;
    if (target && camClass) for (BYTE *o : snapshot()) {
        BYTE *ou; if (!liveObj(o) || !rd(o + O_OUTER, ou) || ou != target || !isA(objClass(o), camClass)) continue;
        float f = 0, l[3] = {}; if (pFov.offset >= 0) rd(o + pFov.offset, f);
        if (pRelLoc.offset >= 0) safeRead(o + pRelLoc.offset, l, sizeof l);
        logf("[camera-probe]   component %s fov %.1f relative (%.0f %.0f %.0f)", label(o).c_str(), f, l[0], l[1], l[2]);
        ++n;
    }
    logf("[camera-probe] done: %d camera components on the view target", n);
}

float readF(BYTE *o, const Prop &p) { float v; return (p.offset >= 0 && rd(o + p.offset, v)) ? v : 0.f; }
int readI(BYTE *o, const Prop &p)
{
    if (p.offset < 0) return 0;
    if (p.size == 1) { uint8_t v; return rd(o + p.offset, v) ? (int)(int8_t)v : 0; }
    if (p.size == 4 && p.type == "FloatProperty") { float v; return rd(o + p.offset, v) ? (int)v : 0; }
    int32_t v; return rd(o + p.offset, v) ? v : 0;
}

} // namespace

void ue4RequestCameraProbe() { g_cameraProbeRequested = true; }

void ue4Init()
{
    if (g_base) return;
    g_base = (BYTE *)GetModuleHandleA(nullptr);
    section(".data", g_dataOff, g_dataLen);
}

static bool discover()
{
    if (!g_dataLen) return false;
    std::string buf(g_dataLen, 0);
    if (!safeRead(g_base + g_dataOff, &buf[0], g_dataLen)) {
        // .data may end in an uncommitted tail; read what we can, page by page
        size_t got = 0;
        while (got < g_dataLen && safeRead(g_base + g_dataOff + got, &buf[got], 4096)) got += 4096;
        buf.resize(got);
    }
    const uint64_t *d = (const uint64_t *)buf.data(); size_t n = buf.size() / 8;
    if (!g_names && !findNames(d, n)) return false;
    if (!g_objs && !findObjects(d, n)) return false;
    return true;
}

bool ue4Poll()
{
    g_ue4.available = 0;
    if (!g_cfg.ue4Enabled) return false;
    ue4Init();
    ULONGLONG now = GetTickCount64();
    if (!g_ue4.ready) {
        if (now < g_nextDiscover) return false;
        g_nextDiscover = now + 5000;
        if (!discover()) return false;
        g_ue4.ready = true;
    }
    if (!g_propsResolved) {
        if (now < g_nextScan) return false;
        g_nextScan = now + 5000;
        static bool discovered = false;
        if (g_cfg.ue4Discover && !discovered) { discovered = true; discoverProps(); }
        g_pRpm = findProp(g_cfg.ue4Rpm);
        if (g_pRpm.offset < 0) return false;
        g_pMax = findProp(g_cfg.ue4MaxRpm);
        g_pSpeed = findProp(g_cfg.ue4Speed);
        g_pGear = findProp(g_cfg.ue4Gear);
        g_propsResolved = true;
        if (g_cfg.ue4Discover) dumpClass(g_pRpm.owner, g_pRpm.ownerName.c_str());
    }
    if (g_cameraProbeRequested.exchange(false)) probeCameras();
    // validate the cached instance: same pointer still sits at its index
    if (g_inst) {
        BYTE *o = objAt(g_instIdx); uint32_t fl;
        if (o != g_inst || !rd(o + O_FLAGS, fl)) { g_inst = nullptr; g_ue4.live = false; }
    }
    if (!g_inst) {
        if (now < g_nextScan) return false;
        g_nextScan = now + 2000;
        g_instIdx = findInstance(g_pRpm.owner, &g_inst);
        if (g_instIdx < 0) { g_ue4.live = false; return false; }
        logf("[ue4] live %s instance #%d at %p", g_pRpm.ownerName.c_str(), g_instIdx, (void *)g_inst);
    }
    uint32_t available = 0;
    auto sampleF = [&](const Prop& p, std::atomic<float>& output, uint32_t bit) {
        float v = 0;
        if (p.offset >= 0 && rd(g_inst + p.offset, v)) available |= bit;
        output = v;
    };
    sampleF(g_pRpm, g_ue4.rpm, 1);
    sampleF(g_pMax, g_ue4.maxRpm, 2);
    sampleF(g_pSpeed, g_ue4.speed, 4);
    int gear = 0;
    if (g_pGear.offset >= 0) {
        if (g_pGear.size == 1) { int8_t v; if (rd(g_inst + g_pGear.offset, v)) { gear = v; available |= 8; } }
        else if (g_pGear.size == 4 && g_pGear.type == "FloatProperty") {
            float v;
            if (rd(g_inst + g_pGear.offset, v) && v >= -128 && v <= 128) { gear = (int)v; available |= 8; }
        } else { int32_t v; if (rd(g_inst + g_pGear.offset, v)) { gear = v; available |= 8; } }
    }
    g_ue4.gear = gear;
    g_ue4.available = available;
    if (!(available & 1)) { g_inst = nullptr; g_ue4.live = false; return false; }
    g_ue4.live = true;
    return true;
}
