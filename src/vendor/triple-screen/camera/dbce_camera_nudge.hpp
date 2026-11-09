// STD-046 camera position tweaks for triple-screen ports: offsets on the game's own camera (the calculated start), kept
// per camera and set and, where the game can tell, per car. Pure logic: the port owns the storage key (car, camera,
// set), the key polling and the camera record it moves.
//
// - Keys (STD-005): Numpad 8/2 forward/back, 9/3 up/down, 4/6 left/right, 7/1 tilt down/up; 5 resets with the FOV.
// - Saved as four whole numbers "ahead up right tilt" (cm, cm, cm, degrees; positive tilt looks down).
// - nudgeCamera moves an eye along the camera's own axes. The world's handedness is per engine: EGO (DiRT 3) is
//   right-handed, so right = forward x up (rig 2026-11-07: moving along up x forward put the car right of centre).
//
// First consumer: dbce-mods-dirt-3 0.3.13 (rig 2026-11-07: chase up/right/reset, per-car saves).
#pragma once
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace dbce::triple_screen {

struct ViewNudge {
    int ahead = 0, up = 0, right = 0, tilt = 0;   // cm, cm, cm, degrees (positive tilt looks down)
    bool any() const { return ahead || up || right || tilt; }
};
constexpr int kNudgeLimitCm = 300, kNudgeTiltLimit = 45;

// "ahead up right tilt"; false (v untouched) when malformed or out of range.
inline bool parseNudge(const char *b, ViewNudge &v)
{
    int a, u, r, t;
    char extra;
    if (!b || sscanf_s(b, "%d %d %d %d %c", &a, &u, &r, &t, &extra, 1) != 4) return false;
    if (abs(a) > kNudgeLimitCm || abs(u) > kNudgeLimitCm || abs(r) > kNudgeLimitCm || abs(t) > kNudgeTiltLimit) return false;
    v.ahead = a; v.up = u; v.right = r; v.tilt = t;
    return true;
}
inline void formatNudge(char *out, size_t n, const ViewNudge &v) { _snprintf_s(out, n, _TRUNCATE, "%d %d %d %d", v.ahead, v.up, v.right, v.tilt); }

enum NudgeKey { kMoveForward, kMoveBack, kMoveUp, kMoveDown, kMoveLeft, kMoveRight, kTiltDown, kTiltUp, kNudgeKeyCount };
struct NudgeSteps { int moveCm = 2, tiltDeg = 1; };   // STD-006 defaults: 0.02 m, 1 degree

// One key step; true when the value changed (it stops at the limits).
inline bool applyNudgeKey(ViewNudge &v, NudgeKey k, const NudgeSteps &s = NudgeSteps())
{
    const ViewNudge before = v;
    auto step = [](int &x, int d, int lim) { x += d; x = x < -lim ? -lim : x > lim ? lim : x; };
    switch (k) {
    case kMoveForward: step(v.ahead, s.moveCm, kNudgeLimitCm); break;
    case kMoveBack: step(v.ahead, -s.moveCm, kNudgeLimitCm); break;
    case kMoveUp: step(v.up, s.moveCm, kNudgeLimitCm); break;
    case kMoveDown: step(v.up, -s.moveCm, kNudgeLimitCm); break;
    case kMoveLeft: step(v.right, -s.moveCm, kNudgeLimitCm); break;
    case kMoveRight: step(v.right, s.moveCm, kNudgeLimitCm); break;
    case kTiltDown: step(v.tilt, s.tiltDeg, kNudgeTiltLimit); break;
    case kTiltUp: step(v.tilt, -s.tiltDeg, kNudgeTiltLimit); break;
    default: return false;
    }
    return memcmp(&before, &v, sizeof v) != 0;
}

// Moves and tilts a camera's eye and axes in place: up and forward (orthogonal, any length) and the eye position, in
// metres. rightHanded: right = forward x up (EGO); otherwise right = up x forward (a left-handed D3D-style world).
inline void nudgeCamera(float *up, float *fwd, float *pos, const ViewNudge &v, bool rightHanded)
{
    const float nu = sqrtf(up[0] * up[0] + up[1] * up[1] + up[2] * up[2]);
    const float nf = sqrtf(fwd[0] * fwd[0] + fwd[1] * fwd[1] + fwd[2] * fwd[2]);
    if (!(nu > 1e-3f) || !(nf > 1e-3f)) return;
    const float u[3] = {up[0] / nu, up[1] / nu, up[2] / nu}, f[3] = {fwd[0] / nf, fwd[1] / nf, fwd[2] / nf};
    const float s = rightHanded ? 1.0f : -1.0f;
    const float r[3] = {s * (f[1] * u[2] - f[2] * u[1]), s * (f[2] * u[0] - f[0] * u[2]), s * (f[0] * u[1] - f[1] * u[0])};
    for (int k = 0; k < 3; ++k) pos[k] += (f[k] * v.ahead + u[k] * v.up + r[k] * v.right) / 100.0f;
    if (v.tilt) {
        const float a = v.tilt * 0.017453292519943295f, c = cosf(a), sn = sinf(a);
        for (int k = 0; k < 3; ++k) {
            fwd[k] = (f[k] * c - u[k] * sn) * nf;
            up[k] = (u[k] * c + f[k] * sn) * nu;
        }
    }
}

}  // namespace dbce::triple_screen
