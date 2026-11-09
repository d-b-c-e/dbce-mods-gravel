// Effect-gain emulation for wheels that ignore DIEFFECT.dwGain, pure (no device calls). Shared by the DirectInput proxies'
// force layer (dbce_ffb_layer.h); first written for Rush Rally 3 (2026-10-08).
//
// Rush Rally creates its rumble as a full-strength Square (magnitude 10000) and silences it with the effect's gain
// (dwGain = 0), later also sending a period of 0; its force slider works through effect gain too. Logitech-style
// drivers honour the gain; the MOZA R12 evidently does not: in the owner's first unmuted session (2026-10-08) the wheel
// was driven hard to one side and alternated about every 30 s, and the slider at 0 changed nothing. The proxy therefore
// sends every effect with gain 10000 and the gain already applied to its magnitudes, which is what a gain-honouring
// device plays anyway. A periodic effect with period 0 (undefined) is sent silent.
#pragma once
#ifndef DIRECTINPUT_VERSION
#define DIRECTINPUT_VERSION 0x0800
#endif
#include <windows.h>
#include <dinput.h>
#include <cstring>

namespace dbce { namespace ffb {

enum Kind { K_OTHER, K_CONSTANT, K_RAMP, K_PERIODIC, K_CONDITION };

union TypeParams {
    DICONSTANTFORCE constant;
    DIRAMPFORCE ramp;
    DIPERIODIC periodic;
    DICONDITION condition[2];
};

// The gain applied here is the game's effect gain times the mod's Strength (50% = the game's own level, up to 2x):
// combinedGain() gives 0..20000; every resulting magnitude is capped at the wheel's maximum (DI_FFNOMINALMAX).
const DWORD kMaxGain = 2 * DI_FFNOMINALMAX;
inline DWORD combinedGain(DWORD gameGain, int strengthPercent)
{
    if (gameGain > DI_FFNOMINALMAX) gameGain = DI_FFNOMINALMAX;
    if (strengthPercent < 0) strengthPercent = 0;
    if (strengthPercent > 100) strengthPercent = 100;
    return (DWORD)((unsigned long long)gameGain * (unsigned)strengthPercent / 50);
}
inline DWORD clampGain(DWORD gain) { return gain > kMaxGain ? kMaxGain : gain; }
inline LONG scaled(LONG v, DWORD gain)
{
    long long r = (long long)v * (long long)clampGain(gain) / DI_FFNOMINALMAX;
    return (LONG)(r > DI_FFNOMINALMAX ? DI_FFNOMINALMAX : r < -(long long)DI_FFNOMINALMAX ? -(long long)DI_FFNOMINALMAX : r);
}
inline DWORD scaledU(DWORD v, DWORD gain)
{
    unsigned long long r = (unsigned long long)v * clampGain(gain) / DI_FFNOMINALMAX;
    return (DWORD)(r > DI_FFNOMINALMAX ? DI_FFNOMINALMAX : r);
}

// Bytes of type-specific parameters this kind uses (0 = not handled: passed through unchanged).
inline DWORD paramBytes(Kind k, DWORD cb)
{
    switch (k) {
    case K_CONSTANT: return cb >= sizeof(DICONSTANTFORCE) ? (DWORD)sizeof(DICONSTANTFORCE) : 0;
    case K_RAMP: return cb >= sizeof(DIRAMPFORCE) ? (DWORD)sizeof(DIRAMPFORCE) : 0;
    case K_PERIODIC: return cb >= sizeof(DIPERIODIC) ? (DWORD)sizeof(DIPERIODIC) : 0;
    case K_CONDITION: return (cb == sizeof(DICONDITION) || cb == 2 * sizeof(DICONDITION)) ? cb : 0;   // one or two axes only
    default: return 0;
    }
}

// The parameters with the effect gain applied, for a device told gain 10000.
inline TypeParams applyGain(Kind k, const TypeParams &in, DWORD bytes, DWORD gain)
{
    TypeParams out = in;
    switch (k) {
    case K_CONSTANT: out.constant.lMagnitude = scaled(in.constant.lMagnitude, gain); break;
    case K_RAMP: out.ramp.lStart = scaled(in.ramp.lStart, gain); out.ramp.lEnd = scaled(in.ramp.lEnd, gain); break;
    case K_PERIODIC:
        if (in.periodic.dwPeriod == 0) { out.periodic.dwMagnitude = 0; out.periodic.lOffset = 0; break; }   // undefined wave: silent
        out.periodic.dwMagnitude = scaledU(in.periodic.dwMagnitude, gain);
        out.periodic.lOffset = scaled(in.periodic.lOffset, gain);
        break;
    case K_CONDITION:
        for (DWORD i = 0; i < bytes / sizeof(DICONDITION) && i < 2; ++i) {
            out.condition[i].lPositiveCoefficient = scaled(in.condition[i].lPositiveCoefficient, gain);
            out.condition[i].lNegativeCoefficient = scaled(in.condition[i].lNegativeCoefficient, gain);
        }
        break;
    default: break;
    }
    return out;
}

inline DIENVELOPE applyGain(const DIENVELOPE &in, DWORD gain)
{
    DIENVELOPE out = in;
    out.dwAttackLevel = scaledU(in.dwAttackLevel, gain);
    out.dwFadeLevel = scaledU(in.dwFadeLevel, gain);
    return out;
}

}} // namespace dbce::ffb
