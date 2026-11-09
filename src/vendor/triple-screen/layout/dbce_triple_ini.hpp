#pragma once
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "../geometry/dbce_triple_geometry.hpp"

// C++17. The [Triple] INI contract Wheelkit Apply writes for projected-triples mods (see docs/native-compositor.md).
// `get(key, buffer, size)` returns true and fills a NUL-terminated value when the key exists; mods wrap
// GetPrivateProfileStringA or their own parser. Width is the panel CHORD; CurveRadiusMm 0 means flat.
namespace dbce::triple_screen {
enum class Screens { off, surround, separate_monitors };
struct IniTriple {
    Screens screens;
    Rig rig;
    double curve_radius_mm;
    const char* refusal;   // nullptr when the geometry is usable; Screens is valid either way
};

template <class Get> IniTriple read_ini_triple(Get get) {
    IniTriple t{Screens::off, {}, 0, nullptr};
    char b[64];
    if (get("Screens", b, sizeof b)) {
        if (!std::strcmp(b, "Surround")) t.screens = Screens::surround;
        else if (!std::strcmp(b, "Separate monitors")) t.screens = Screens::separate_monitors;
    }
    struct Key { const char* name; double* to; double low, high; bool required; };
    double width = 0, height = 0, eye = 0, left = 0, right = 0, bezel = 0, lift = 0, radius = 0;
    const Key keys[] = {{"PanelWidthMm", &width, 100, 3000, true}, {"PanelHeightMm", &height, 50, 2000, true},
                        {"EyeDistanceMm", &eye, 100, 3000, true}, {"LeftYawDegrees", &left, 0, 89.999, true},
                        {"RightYawDegrees", &right, 0, 89.999, true}, {"BezelMm", &bezel, 0, 100, false},
                        {"EyeHeightMm", &lift, -1000, 1000, false}, {"CurveRadiusMm", &radius, 0, 20000, false}};
    for (const Key& k : keys) {
        if (!get(k.name, b, sizeof b)) { if (k.required) { t.refusal = "a [Triple] geometry key is missing"; return t; } continue; }
        char* end = nullptr;
        errno = 0;
        const double v = std::strtod(b, &end);
        if (end == b || *end || errno || !std::isfinite(v) || v < k.low || v > k.high) { t.refusal = "a [Triple] geometry value is out of range"; return t; }
        *k.to = v;
    }
    if (radius != 0 && (radius < 300 || radius < width / 2)) { t.refusal = "CurveRadiusMm is too small for the panel width"; return t; }
    // EyeDistanceMm is already eye to the plane through the panel edges.
    if (chord_eye_distance(eye, width, radius, eye) != Error::none) { t.refusal = "the eye distance or panel curve is invalid"; return t; }
    t.rig = {width, height, eye, left, right, lift, bezel};
    t.curve_radius_mm = radius;
    return t;
}
// Defaults for the keys a player never set (fresh install, or F6 sliders left alone): the screen size Windows reports,
// eye 660 mm, bezel 8 mm, flat, and side screens turned to face the eye. `screens` is the mod's own display detection
// (three matching monitors in a row: separate_monitors). A size of 0 means unknown: a missing size key is then refused.
struct IniDefaults {
    Screens screens = Screens::off;
    double panel_width_mm = 0, panel_height_mm = 0, eye_distance_mm = 660, bezel_mm = 8;
};

// The side angle (degrees) at which each flat side panel's centre faces the eye ("Facing you"), in build_rig's
// convention: eye `eye_mm` from the centre panel, the side panel starting `bezel_mm` past the centre panel's edge.
// NaN when there is no such angle.
inline double facing_you_degrees(double width_mm, double eye_mm, double bezel_mm) {
    if (!(width_mm > 0) || !(eye_mm > 0) || !(bezel_mm >= 0)) return std::nan("");
    const double half = width_mm / 2, along = bezel_mm + half, reach = std::sqrt(eye_mm * eye_mm + half * half);
    if (along > reach) return std::nan("");
    const double a = (std::atan2(half, eye_mm) + std::asin(along / reach)) * 180 / detail::pi;
    return a > 0 && a < 89.999 ? a : std::nan("");
}

// read_ini_triple with `d` standing in for every key the INI does not have. Saved keys always win, and a saved value
// that is out of range is still refused. The side angle default is facing_you_degrees of the width, eye and bezel used.
template <class Get> IniTriple read_ini_triple(Get get, const IniDefaults& d) {
    char b[64];
    auto number = [&](const char* key, double fallback) {
        if (!get(key, b, sizeof b)) return fallback;
        char* end = nullptr;
        const double v = std::strtod(b, &end);
        return end != b && !*end && std::isfinite(v) ? v : fallback;   // a bad saved value is refused below
    };
    const double yaw = facing_you_degrees(number("PanelWidthMm", d.panel_width_mm), number("EyeDistanceMm", d.eye_distance_mm),
                                          number("BezelMm", d.bezel_mm));
    auto filled = [&](const char* key, char* out, size_t size) -> bool {
        if (get(key, out, size)) return true;
        double v = 0;
        if (!std::strcmp(key, "Screens")) {
            const char* text = d.screens == Screens::surround ? "Surround" : d.screens == Screens::separate_monitors ? "Separate monitors" : nullptr;
            if (!text || std::strlen(text) >= size) return false;
            std::memcpy(out, text, std::strlen(text) + 1);
            return true;
        }
        if (!std::strcmp(key, "PanelWidthMm")) v = d.panel_width_mm;
        else if (!std::strcmp(key, "PanelHeightMm")) v = d.panel_height_mm;
        else if (!std::strcmp(key, "EyeDistanceMm")) v = d.eye_distance_mm;
        else if (!std::strcmp(key, "BezelMm")) v = d.bezel_mm;
        else if (!std::strcmp(key, "LeftYawDegrees") || !std::strcmp(key, "RightYawDegrees")) v = yaw;
        if (!(v > 0) || !std::isfinite(v)) return false;
        return std::snprintf(out, size, "%.9g", v) > 0;
    };
    return read_ini_triple(filled);
}
} // namespace dbce::triple_screen
