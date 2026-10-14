// Is a game's DirectInput data format one of the two standard joystick formats (c_dfDIJoystick, c_dfDIJoystick2), object
// for object? Test injection writes DIJOYSTATE fields by their standard meaning, so it is delivered only into those two
// formats; a custom format of the same size (different offsets, other objects) gets nothing (Astra's review of 908e252).
// The two formats are rebuilt here from their documented layout, because a dinput8.dll proxy cannot link dinput8.lib;
// test_fence.cpp checks they equal the library's own c_dfDIJoystick and c_dfDIJoystick2.
#pragma once
#ifndef DIRECTINPUT_VERSION
#define DIRECTINPUT_VERSION 0x0800
#endif
#include <windows.h>
#include <dinput.h>
#include <stddef.h>
#include <vector>

namespace dbce { namespace fence {

// DIDFT_OPTIONAL (0x80000000): set on every object of the standard formats; current SDK headers no longer define it.
constexpr DWORD kOptional = 0x80000000;

struct StandardFormat {
    std::vector<DIOBJECTDATAFORMAT> objects;
    DIDATAFORMAT format{};
};

inline StandardFormat buildStandardFormat(bool two)
{
    StandardFormat s;
    const DWORD axis = kOptional | DIDFT_AXIS | DIDFT_ANYINSTANCE;
    static const GUID *const axes[6] = {&GUID_XAxis, &GUID_YAxis, &GUID_ZAxis, &GUID_RxAxis, &GUID_RyAxis, &GUID_RzAxis};
    // As the library has them (dumped 2026-10-09): every axis carries its aspect flag, and the velocity, acceleration and
    // force sliders of c_dfDIJoystick2 point at the position sliders' offsets (24, 28), not at rglVSlider and the others.
    auto group = [&](DWORD base, DWORD flags) {
        for (DWORD k = 0; k < 6; ++k) s.objects.push_back({axes[k], base + 4 * k, axis, flags});
        for (DWORD k = 0; k < 2; ++k) s.objects.push_back({&GUID_Slider, (DWORD)FIELD_OFFSET(DIJOYSTATE, rglSlider) + 4 * k, axis, flags});
    };
    group(FIELD_OFFSET(DIJOYSTATE, lX), DIDOI_ASPECTPOSITION);
    for (DWORD k = 0; k < 4; ++k) s.objects.push_back({&GUID_POV, (DWORD)FIELD_OFFSET(DIJOYSTATE, rgdwPOV) + 4 * k, kOptional | DIDFT_POV | DIDFT_ANYINSTANCE, 0});
    for (DWORD k = 0; k < (two ? 128u : 32u); ++k) s.objects.push_back({nullptr, (DWORD)FIELD_OFFSET(DIJOYSTATE, rgbButtons) + k, kOptional | DIDFT_BUTTON | DIDFT_ANYINSTANCE, 0});
    if (two) {
        group(FIELD_OFFSET(DIJOYSTATE2, lVX), DIDOI_ASPECTVELOCITY);
        group(FIELD_OFFSET(DIJOYSTATE2, lAX), DIDOI_ASPECTACCEL);
        group(FIELD_OFFSET(DIJOYSTATE2, lFX), DIDOI_ASPECTFORCE);
    }
    s.format = {sizeof(DIDATAFORMAT), sizeof(DIOBJECTDATAFORMAT), DIDF_ABSAXIS, two ? (DWORD)sizeof(DIJOYSTATE2) : (DWORD)sizeof(DIJOYSTATE),
                (DWORD)s.objects.size(), nullptr};   // the objects are compared from the vector (no pointer kept across copies)
    return s;
}

inline bool sameFormat(LPCDIDATAFORMAT a, const StandardFormat &s)
{
    const DIDATAFORMAT &b = s.format;
    if (!a || a->dwSize != b.dwSize || a->dwObjSize != b.dwObjSize || a->dwFlags != b.dwFlags || a->dwDataSize != b.dwDataSize ||
        a->dwNumObjs != b.dwNumObjs || a->dwNumObjs != s.objects.size() || !a->rgodf) return false;
    for (DWORD i = 0; i < b.dwNumObjs; ++i) {
        const DIOBJECTDATAFORMAT &x = a->rgodf[i], &y = s.objects[i];
        if (x.dwOfs != y.dwOfs || x.dwType != y.dwType || x.dwFlags != y.dwFlags) return false;
        if ((x.pguid == nullptr) != (y.pguid == nullptr)) return false;
        if (x.pguid && !IsEqualGUID(*x.pguid, *y.pguid)) return false;
    }
    return true;
}

// 1 = c_dfDIJoystick (DIJOYSTATE), 2 = c_dfDIJoystick2 (DIJOYSTATE2), 0 = any other format.
inline int standardFormat(LPCDIDATAFORMAT f)
{
    static const StandardFormat one = buildStandardFormat(false), two = buildStandardFormat(true);
    if (sameFormat(f, one)) return 1;
    if (sameFormat(f, two)) return 2;
    return 0;
}

}} // namespace dbce::fence
