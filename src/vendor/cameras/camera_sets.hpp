// Shared STD-041/042/046 camera sets (toolkit STANDARDS.md): the Game/Realistic choice per view, per-set vertical FOV
// offsets, F7 toggle and numpad zoom writes, and the on-screen name. Pure logic, no game memory. Ported from GRID 2019's
// src/camera_views.hpp (2026-11-07); each game keeps its own view identification (its probe maps a frame to a View).
//
// - INI keys in the game's [Triple] section: <View>Realistic (true = Realistic) and <View>GameOffsetDegrees /
//   <View>RealisticOffsetDegrees. Offsets are added to the lens the game writes that frame, so they never build up.
// - Starting choices (STD-046): cockpit, dash and bumper Realistic; bonnet and chase Game. An explicit older
//   MatchGameFov (true = Game) sets every mounted view.
#pragma once
#include <math.h>
#include <errno.h>
#include <ctype.h>
#include <limits>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace dbce_camsets {

enum View { kNone = -1, kChase = 0, kBonnet, kBumper, kDash, kCockpit, kCount };
static const char *const kViewKeys[kCount] = {"ChaseRealistic", "BonnetRealistic", "BumperRealistic", "DashRealistic", "CockpitRealistic"};
static const char *const kViewNames[kCount] = {"Chase Camera", "Bonnet Camera", "Bumper Camera", "Dash Camera", "Cockpit Camera"};
static const char *const kViewStems[kCount] = {"Chase", "Bonnet", "Bumper", "Dash", "Cockpit"};
static const bool kStartRealistic[kCount] = {false, false, true, true, true};   // STD-046
const double kMinFov = 1, kMaxFov = 179, kFovStep = 2; // Only the final lens has a range; saved offsets stay finite/unbounded.

// A view counts once it has held for `frames` frames (camera cuts and the mirror's own lag do not flicker).
struct Watch {
    View shown = kNone, pending = kNone;
    int held = 0;
    bool step(View now, int frames = 5)   // true when `shown` changed this frame
    {
        if (now == shown) { pending = now; held = 0; return false; }
        if (now != pending) { pending = now; held = 1; } else ++held;
        if (held < frames) return false;
        shown = now; held = 0;
        return true;
    }
};

inline int parseBool(const char *b)   // 1 true, 0 false, -1 unrecognised
{
    if (!_stricmp(b, "1") || !_stricmp(b, "true") || !_stricmp(b, "on") || !_stricmp(b, "yes")) return 1;
    if (!_stricmp(b, "0") || !_stricmp(b, "false") || !_stricmp(b, "off") || !_stricmp(b, "no")) return 0;
    return -1;
}

struct Settings {
    bool realistic[kCount] = {false, false, true, true, true};
    double offset[kCount][2] = {};   // [view][0 Game, 1 Realistic], degrees added to the vertical FOV
};

inline void offsetKey(char *out, size_t n, View v, bool realistic)
{
    _snprintf_s(out, n, _TRUNCATE, "%s%sOffsetDegrees", kViewStems[v], realistic ? "Realistic" : "Game");
}

// get(key, buffer, size) returns true when the key exists in [Triple].
template <class Get> Settings readSettings(Get get)
{
    Settings s;
    char b[64] = "";
    int matchGame = -1;
    if (get("MatchGameFov", b, sizeof b)) matchGame = parseBool(b);
    for (int v = 0; v < kCount; ++v) {
        s.realistic[v] = v == kChase ? false : matchGame >= 0 ? matchGame == 0 : kStartRealistic[v];
        b[0] = 0;
        if (get(kViewKeys[v], b, sizeof b)) { const int x = parseBool(b); if (x >= 0) s.realistic[v] = x == 1; }
        for (int r = 0; r < 2; ++r) {
            char k[48];
            offsetKey(k, sizeof k, (View)v, r == 1);
            b[0] = 0;
            if (get(k, b, sizeof b)) {
                char *end = nullptr;
                errno = 0;
                const double d = strtod(b, &end);
                const bool underflow = errno == ERANGE && d == 0;
                const bool parsed = end != b;
                while (isspace((unsigned char)*end)) ++end;
                // Retain inline comments, refuse malformed/truncated numeric input.
                if (parsed && isfinite(d) && !underflow && (!*end || *end == ';' || *end == '#') &&
                    (strlen(b) < sizeof b - 1 || *end == ';' || *end == '#')) s.offset[v][r] = d;
            }
        }
    }
    return s;
}

inline bool validLens(const float t[2])
{
    return isfinite(t[0]) && isfinite(t[1]) && t[0] > 0 && t[1] > 0;
}
// The game's native/seat lens plus the raw saved offset. Apply one final fence and
// derive both tangents from that same angle; ordinary zero offsets stay bit-exact.
inline void target(const Settings &s, View v, const float game[2], const float measured[2], float out[2])
{
    out[0] = game[0]; out[1] = game[1];
    if (v <= kNone || v >= kCount) return;
    const bool r = s.realistic[v];
    const float *base = r ? measured : game;
    out[0] = base[0]; out[1] = base[1];
    const double off = s.offset[v][r ? 1 : 0];
    if (!validLens(base) || !isfinite(off)) return;
    const double degrees = 2 * atan((double)base[1]) * 57.29577951308232;
    if (off == 0 && degrees >= kMinFov && degrees <= kMaxFov) return;
    const double requested = degrees + off;
    const double effective = requested < kMinFov ? kMinFov : requested > kMaxFov ? kMaxFov : requested;
    const double t = tan(effective / 57.29577951308232 / 2);
    const double h = ((double)base[0] / base[1]) * t;
    if (isfinite(h) && h > 0 && h <= (std::numeric_limits<float>::max)()) { out[0] = (float)h; out[1] = (float)t; }
}

// Changes, each returning the one key and value to write (STD-044: only its own key).
struct Write { char key[48]; char value[32]; };
inline Write toggle(Settings &s, View v)
{
    Write w{};
    s.realistic[v] = !s.realistic[v];
    strcpy_s(w.key, kViewKeys[v]);
    strcpy_s(w.value, s.realistic[v] ? "true" : "false");
    return w;
}
// Numpad + zooms in (narrower), - zooms out (STD-048); 5 resets the current set to the calculation.
enum Zoom { kZoomIn, kZoomOut, kZoomReset };
inline Write zoom(Settings &s, View v, Zoom z)
{
    Write w{};
    if (v <= kNone || v >= kCount) return w;
    const int r = s.realistic[v] ? 1 : 0;
    const double old = s.offset[v][r];
    double next = z == kZoomReset ? 0 : old + (z == kZoomIn ? -kFovStep : kFovStep);
    if (z != kZoomReset && next == old)
        next = nextafter(old, z == kZoomIn ? -std::numeric_limits<double>::infinity() : std::numeric_limits<double>::infinity());
    // At the finite representation edge, refuse rather than save infinity.
    if (!isfinite(next)) return w;
    offsetKey(w.key, sizeof w.key, v, r == 1);
    if (_snprintf_s(w.value, sizeof w.value, _TRUNCATE, "%.17g", next) < 0) return Write{};
    s.offset[v][r] = next;
    return w;
}

// "Cockpit Camera - Realistic FOV (Custom)" (STD-042/046); empty for kNone.
inline void name(char *out, size_t n, const Settings &s, View v)
{
    if (v <= kNone || v >= kCount) { out[0] = 0; return; }
    const bool r = s.realistic[v];
    _snprintf_s(out, n, _TRUNCATE, "%s - %s FOV%s", kViewNames[v], r ? "Realistic" : "Game", s.offset[v][r ? 1 : 0] != 0 ? " (Custom)" : "");
}
// The on-screen label with the key reminder.
inline void label(char *out, size_t n, const Settings &s, View v, const char *game, const char *extra = nullptr)
{
    char nm[96];
    name(nm, sizeof nm, s, v);
    if (!nm[0]) { _snprintf_s(out, n, _TRUNCATE, "This camera keeps %s's own FOV (not identified yet)", game); return; }
    _snprintf_s(out, n, _TRUNCATE, "%s%s%s  \xC2\xB7  Numpad + / - zoom, 5 reset  \xC2\xB7  F7 %s FOV", nm, extra ? "  \xC2\xB7  " : "",
                extra ? extra : "", s.realistic[v] ? "Game" : "Realistic");
}

}  // namespace dbce_camsets
