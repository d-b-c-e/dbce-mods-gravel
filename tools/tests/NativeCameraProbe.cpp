#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <map>
#include <memory>
#include <string>
#include <vector>

// The actual camera-probe block of src/ue4.cpp, compiled against synthetic reflection memory: fake UObjects, classes,
// properties and an FUObjectItem array with the 4.17 layout the probe reads. Only the helpers the block calls are stubbed.
typedef unsigned char BYTE;
const int O_FLAGS = 0x08, O_INDEX = 0x0C, O_CLASS = 0x10, O_OUTER = 0x20, S_SUPER = 0x30, S_PROPSIZE = 0x40, P_ELEMSIZE = 0x34;
const uint32_t RF_CDO = 0x10, RF_ARCHETYPE = 0x20;
int g_pOffset = 0x50;

struct Item { BYTE *object; int32_t flags, cluster, serial, pad; };
static_assert(sizeof(Item) == 24, "FUObjectItem is 24 bytes");
struct World {
    std::vector<std::unique_ptr<BYTE[]>> mem;
    std::vector<Item> items;
    std::map<const BYTE *, std::string> names;
    int32_t nextSerial = 1;
    World() { items.reserve(8192); }
} *W = nullptr;
const size_t BLOCK = 0x800;

bool safeRead(const void *addr, void *out, size_t n)
{
    auto a = (const BYTE *)addr;
    for (auto &m : W->mem) if (a >= m.get() && a + n <= m.get() + BLOCK) { memcpy(out, a, n); return true; }
    auto ib = (const BYTE *)W->items.data();
    if (a >= ib && a + n <= ib + W->items.size() * sizeof(Item)) { memcpy(out, a, n); return true; }
    return false;
}
template <typename T> bool rd(const void *addr, T &out) { return safeRead(addr, &out, sizeof(T)); }
template <typename T> void put(BYTE *at, T v) { memcpy(at, &v, sizeof v); }
std::vector<std::string> g_log;
void logf(const char *fmt, ...)
{
    char b[1024]; va_list a; va_start(a, fmt); vsnprintf(b, sizeof b, fmt, a); va_end(a); g_log.push_back(b);
}
std::string objName(BYTE *o) { auto it = W->names.find(o); return it == W->names.end() ? "" : it->second; }
BYTE *objClass(BYTE *o) { BYTE *c; return (o && rd(o + O_CLASS, c)) ? c : nullptr; }
BYTE *itemAt(int i) { return (i >= 0 && (size_t)i < W->items.size()) ? (BYTE *)&W->items[i] : nullptr; }
std::vector<BYTE *> snapshot() { std::vector<BYTE *> v; for (auto &i : W->items) v.push_back(i.object); return v; }
bool isA(BYTE *cls, BYTE *want)
{
    for (int depth = 0; cls && depth < 32; ++depth) { if (cls == want) return true; if (!rd(cls + S_SUPER, cls)) return false; }
    return false;
}

/* PROBE_BLOCK */

BYTE *obj(const std::string &name, BYTE *cls, BYTE *outer = nullptr, uint32_t flags = 0)
{
    W->mem.emplace_back(new BYTE[BLOCK]());
    BYTE *o = W->mem.back().get();
    const int32_t idx = (int32_t)W->items.size();
    W->items.push_back({o, 0, 0, W->nextSerial++, 0});
    put(o + O_FLAGS, flags); put(o + O_INDEX, idx); put(o + O_CLASS, cls ? cls : o); put(o + O_OUTER, outer);
    W->names[o] = name;
    return o;
}
BYTE *g_meta;
BYTE *klass(const std::string &name, BYTE *super, int32_t size)
{
    BYTE *c = obj(name, g_meta); put(c + S_SUPER, super); put(c + S_PROPSIZE, size); return c;
}
std::map<std::string, BYTE *> T;   // property type classes
BYTE *prop(const std::string &name, const std::string &type, BYTE *owner, int32_t off, int32_t size)
{
    BYTE *p = obj(name, T[type], owner); put(p + P_ELEMSIZE, size); put(p + 0x50, off); return p;
}
int32_t idxOf(BYTE *o) { int32_t i; memcpy(&i, o + O_INDEX, 4); return i; }

struct Scene {
    BYTE *cActor, *cScene, *cCam, *cPc, *cPcm, *cGravel, *cCar, *cTvt, *cCce, *cMvi;
    BYTE *pc, *pcm, *car, *root, *weakProp;
};
// A well-formed race: one controller (plus its CDO), a Blueprint camera manager with an index and a weak view target,
// a car with a root and two cameras, and finite POVs. Options let each test break one thing.
struct Opt { bool noPc = false, twoPc = false, dupFov = false, pcmAsInt = false, locOutside = false, smallPov = false,
             managerIsCar = false, managerBogus = false, nullTarget = false, splitMvi = false, foreignTarget = false,
             noMvi = false; int extraFields = 0, cams = 2; };
Scene build(World &w, const Opt &o = {})
{
    W = &w; T.clear();
    g_meta = obj("Class", nullptr); put(g_meta + O_CLASS, g_meta);
    for (const char *t : {"ObjectProperty", "StructProperty", "FloatProperty", "IntProperty", "WeakObjectProperty", "ByteProperty", "BoolProperty"})
        T[t] = obj(t, g_meta);
    BYTE *structMeta = obj("ScriptStruct", g_meta);
    auto strct = [&](const char *n, int32_t size) { BYTE *s = obj(n, structMeta); put(s + S_PROPSIZE, size); return s; };
    Scene s{};
    BYTE *cObject = klass("Object", nullptr, 0x28);
    s.cActor = klass("Actor", cObject, 0x300);
    s.cScene = klass("SceneComponent", cObject, 0x300);
    s.cCam = klass("CameraComponent", s.cScene, 0x400);
    s.cPc = klass("PlayerController", s.cActor, 0x500);
    BYTE *cCheckpoint = klass("CheckpointPlayerController_C", s.cPc, 0x520);
    s.cPcm = klass("PlayerCameraManager", s.cActor, 0x700);
    BYTE *cIgnition = klass("IgnitionCameraManager", s.cPcm, 0x710);
    s.cGravel = klass("GravelCameraManager_C", cIgnition, 0x780);
    s.cCar = klass("Car", s.cActor, 0x400);
    s.cTvt = strct("TViewTarget", 0x60); s.cCce = strct("CameraCacheEntry", 0x60);
    s.cMvi = strct(o.noMvi ? "MinimalViewInfoX" : "MinimalViewInfo", 0x40);
    // A second, same-named struct holding one of the members, and a same-named owner of the wrong kind.
    BYTE *mviTwin = o.splitMvi ? strct("MinimalViewInfo", 0x40) : nullptr;
    BYTE *foreign = o.foreignTarget ? klass("TViewTarget", cObject, 0x60) : nullptr;

    prop("PlayerCameraManager", o.pcmAsInt ? "IntProperty" : "ObjectProperty", s.cPc, 0x428, o.pcmAsInt ? 4 : 8);
    prop("ViewTarget", "StructProperty", s.cPcm, 0x100, 0x60);
    prop("CameraCache", "StructProperty", s.cPcm, 0x200, 0x60);
    prop("Target", "ObjectProperty", foreign ? foreign : s.cTvt, 0, 8);
    prop("POV", "StructProperty", s.cTvt, 0x10, o.smallPov ? 0x10 : 0x40);
    prop("POV", "StructProperty", s.cCce, 0x10, 0x40);
    prop("Location", "StructProperty", s.cMvi, o.locOutside ? 0x3C : 0, 12);
    prop("Rotation", "StructProperty", s.cMvi, 0xC, 12);
    prop("FOV", "FloatProperty", mviTwin ? mviTwin : s.cMvi, 0x18, 4);
    if (o.dupFov) prop("FOV", "FloatProperty", s.cMvi, 0x1C, 4);
    prop("RootComponent", "ObjectProperty", s.cActor, 0x60, 8);
    prop("RelativeLocation", "StructProperty", s.cScene, 0x80, 12);
    prop("RelativeRotation", "StructProperty", s.cScene, 0x8C, 12);
    prop("FieldOfView", "FloatProperty", s.cCam, 0x300, 4);
    prop("CameraIndex", "IntProperty", s.cGravel, 0x700, 4);
    s.weakProp = prop("CurrentViewTargetActor", "WeakObjectProperty", s.cGravel, 0x708, 8);
    prop("ViewTargetOffset", "StructProperty", cIgnition, 0x704, 4);       // struct fields are not dumped
    for (int i = 0; i < o.extraFields; ++i) prop("Extra" + std::to_string(i), "IntProperty", s.cGravel, 0x710, 4);

    obj("Default__PlayerController", s.cPc, nullptr, RF_CDO);
    if (!o.noPc) s.pc = obj("CheckpointPlayerController_C_0", cCheckpoint);
    if (o.twoPc) obj("CheckpointPlayerController_C_1", cCheckpoint);
    s.pcm = obj("GravelCameraManager_C_0", s.cGravel);
    s.car = obj("Car_0", s.cCar);
    s.root = obj("CarMesh", s.cScene, s.car);
    for (int i = 0; i < o.cams; ++i) {
        BYTE *c = obj("Camera" + std::to_string(i), s.cCam, s.car);
        put(c + 0x300, 70.f + i); put(c + 0x80, 10.f * i); put(c + 0x84, 0.f); put(c + 0x88, 50.f);
    }
    if (s.pc) put(s.pc + 0x428, o.managerBogus ? (BYTE *)(uintptr_t)0x12345678 : o.managerIsCar ? s.car : s.pcm);
    put(s.pcm + 0x700, int32_t(2));
    put(s.pcm + 0x708, idxOf(s.car)); put(s.pcm + 0x70C, W->items[idxOf(s.car)].serial);
    put(s.pcm + 0x100, o.nullTarget ? (BYTE *)nullptr : s.car);
    const float pov[7] = {100, 200, 300, -5, 90, 0, 75};
    memcpy(s.pcm + 0x100 + 0x10, pov, sizeof pov); memcpy(s.pcm + 0x200 + 0x10, pov, sizeof pov);
    put(s.car + 0x60, s.root);
    const float rl[6] = {90, 200, 250, 0, 90, 0}; memcpy(s.root + 0x80, rl, sizeof rl);
    return s;
}
bool has(const std::string &needle) { for (auto &l : g_log) if (l.find(needle) != std::string::npos) return true; return false; }
int count(const std::string &needle) { int n = 0; for (auto &l : g_log) if (l.find(needle) != std::string::npos) ++n; return n; }
// Runs one probe and checks the single terminal line; returns that line.
std::string run() { g_log.clear(); probeCameras(); return g_log.empty() ? "" : g_log.back(); }

int main()
{
    int checks = 0, failures = 0;
    auto check = [&](bool ok, const char *why) { ++checks; if (!ok) { ++failures; std::printf("FAIL: %s\n", why); for (auto &l : g_log) std::printf("  | %s\n", l.c_str()); } };
    auto terminal = [&](const std::string &status) {
        const std::string last = run();
        check(count("v2 done status=") == 1 && last.find("v2 done status=" + status + " ") != std::string::npos, ("one terminal line, status " + status).c_str());
    };

    { World w; build(w); terminal("ok");
      check(count("controller candidate") == 1 && has("controller candidate CheckpointPlayerController_C_0 : CheckpointPlayerController_C"), "CDO excluded, one candidate");
      check(has("-> PlayerCameraManager GravelCameraManager_C_0 : GravelCameraManager_C"), "manager followed through the pointer");
      check(has("GravelCameraManager_C.CameraIndex (IntProperty +0x700) = 2"), "Blueprint int field value");
      check(has("GravelCameraManager_C.CurrentViewTargetActor (WeakObjectProperty +0x708) = Car_0 : Car"), "weak pointer resolved with serial");
      check(!has("ViewTargetOffset"), "struct fields not dumped");
      check(has("ViewTarget.Target Car_0 : Car"), "view target");
      check(has("CameraCache.POV (sampled cache) loc (100 200 300) rot (-5 90 0) fov 75"), "raw cache POV");
      check(has("relative loc (90 200 250) rot (0 90 0) (attachment unchecked)"), "raw root transform labelled");
      check(has("component Camera1 : CameraComponent fov 71 relative loc (10 0 50)"), "component raw values");
      check(has("components=2/16") && has("fields=2/64"), "counts in terminal line");
      check(!has("identity"), "no derived identity claimed");
      check(has("candidate, not a proven local player") && has("sampled cache, not the rendered frame"), "limits printed"); }

    { World w; build(w); g_pOffset = 0; terminal("no-offset-slot"); g_pOffset = 0x50; }
    { World w; Opt o; o.noPc = true; build(w, o); terminal("no-controller"); }
    { World w; Opt o; o.twoPc = true; build(w, o); terminal("ambiguous-controller"); check(count("controller candidate") == 2, "both candidates listed"); }
    { World w; Opt o; o.dupFov = true; build(w, o); terminal("layout-refused"); check(has("MinimalViewInfo.FOV: ambiguous"), "ambiguous member refused"); }
    { World w; Opt o; o.pcmAsInt = true; build(w, o); terminal("layout-refused"); check(has("PlayerController.PlayerCameraManager: unexpected type"), "wrong type refused"); }
    { World w; Opt o; o.locOutside = true; build(w, o); terminal("layout-refused"); check(has("MinimalViewInfo.Location: outside owner"), "member past owner size refused"); }
    { World w; Opt o; o.smallPov = true; build(w, o); terminal("layout-refused"); check(has("nested member does not fit"), "nested bounds refused"); }
    { World w; Opt o; o.splitMvi = true; build(w, o); terminal("owner-unresolved");
      check(has("owner ScriptStruct MinimalViewInfo: ambiguous"), "members split across same-named structs refused"); }
    { World w; Opt o; o.foreignTarget = true; build(w, o); terminal("layout-refused");
      check(has("TViewTarget.Target: missing"), "member of a same-named owner of another kind is not bound"); }
    { World w; Opt o; o.noMvi = true; build(w, o); terminal("owner-unresolved");
      check(has("owner ScriptStruct MinimalViewInfo: missing"), "missing owner refused"); }
    { World w; Opt o; o.managerIsCar = true; build(w, o); terminal("manager-unavailable"); check(has("unexpected class Car"), "manager class checked"); }
    { World w; Opt o; o.managerBogus = true; build(w, o); terminal("manager-unavailable"); check(has("not a live object"), "unreadable pointer is unavailable, not null"); }
    { World w; Opt o; o.nullTarget = true; build(w, o); terminal("target-unavailable"); check(has("ViewTarget.Target none"), "null target reported as none"); }

    { World w; Scene s = build(w);
      const float nan = std::numeric_limits<float>::quiet_NaN(), inf = std::numeric_limits<float>::infinity();
      put(s.pcm + 0x200 + 0x10, inf); put(s.pcm + 0x200 + 0x10 + 0x18, nan); put(s.pcm + 0x100 + 0x10 + 0x18, 500.f);
      put(s.pcm + 0x100 + 0x10 + 0x10, 3.0e38f);
      terminal("ok");
      check(has("CameraCache.POV (sampled cache) loc (nonfinite 200 300)") && has("fov nonfinite"), "nonfinite values reported, no hang");
      check(has("fov 500 (implausible)"), "implausible FOV flagged"); }

    { World w; Scene s = build(w); W->items[idxOf(s.car)].serial += 7; terminal("ok");
      check(has("CurrentViewTargetActor (WeakObjectProperty +0x708) = stale (serial mismatch)"), "reused weak slot is stale"); }
    { World w; Scene s = build(w); put(s.pcm + 0x70C, int32_t(0)); terminal("ok");
      check(has("CurrentViewTargetActor (WeakObjectProperty +0x708) = none"), "null weak pointer"); }
    { World w; Scene s = build(w); put(s.pcm + 0x708, int32_t(999999)); terminal("ok");
      check(has("= unavailable (index out of range)"), "weak index out of range"); }

    { World w; Opt o; o.extraFields = 100; o.cams = 20; build(w, o); terminal("ok");
      check(has("fields=64/64 truncated") && count("GravelCameraManager_C.") == 64, "field cap");
      check(has("components=16/16 truncated") && count("component Camera") == 16, "component cap"); }

    std::printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
