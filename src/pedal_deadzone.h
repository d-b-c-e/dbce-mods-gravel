// Pedal resting dead zone (ISS-0039). A pedal that rests a little above the bottom of its range (the owner's MOZA
// brake rested at 850-920 of 0..65535 in November) reads as lightly pressed, and Gravel then holds the car on the
// start line in a burnout. The proxy rewrites the game's read of each pedal axis so that the bottom `fraction` of the
// range counts as fully released and the rest of the travel is stretched back over the whole range: no step at the
// edge of the dead zone, and a fully pressed pedal still reads full. The low end is the released end (MOZA pedals, and
// WheelConfig.ini `&1.0&0.0`). Pure functions with no Windows calls; tools/tests/NativePedalDeadZone.cpp checks them.
#pragma once
#include <cmath>

// The share of the range that counts as released, from the ini's percent (0 turns it off; at most 20%).
inline double pedalDeadZoneFraction(double percent)
{
    if (!(percent > 0)) return 0;
    return (percent > 20 ? 20 : percent) / 100.0;
}

// One axis sample in lo..hi -> the sample the game reads. Readings at or below lo + fraction of the range become lo.
inline long pedalRestDeadZone(long v, long lo, long hi, double fraction)
{
    if (hi <= lo || !(fraction > 0) || v >= hi) return v;
    const double span = double(hi) - double(lo), dz = span * fraction, x = double(v) - double(lo);
    if (x <= dz) return lo;
    const double r = std::round(double(lo) + (x - dz) * span / (span - dz));
    return r >= double(hi) ? hi : (long)r;
}
