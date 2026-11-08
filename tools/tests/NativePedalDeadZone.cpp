// Pedal resting dead zone (src/pedal_deadzone.h, ISS-0039). Run by Test-PedalDeadZone.ps1.
#include "../../src/pedal_deadzone.h"
#include <cassert>
#include <cstdio>
#include <initializer_list>

int main()
{
    // The ini's percent: 2 is the default, 0 and junk turn it off, large values stop at 20%.
    assert(pedalDeadZoneFraction(2) == 0.02);
    assert(pedalDeadZoneFraction(0) == 0 && pedalDeadZoneFraction(-1) == 0 && pedalDeadZoneFraction(std::nan("")) == 0);
    assert(pedalDeadZoneFraction(50) == 0.20);

    const double dz = pedalDeadZoneFraction(2);
    const long lo = 0, hi = 65535;
    // The owner's brake at rest, before and after the 2026-11-07 recalibration, reads as released.
    for (long rest : {0L, 200L, 235L, 850L, 920L, 1310L}) assert(pedalRestDeadZone(rest, lo, hi, dz) == 0);
    // Just past the dead zone the pedal starts from zero again (no step), and full travel still reads full.
    assert(pedalRestDeadZone(1312, lo, hi, dz) == 1);
    assert(pedalRestDeadZone(hi, lo, hi, dz) == hi);
    assert(pedalRestDeadZone(hi - 1, lo, hi, dz) == hi - 1);
    // Halfway through the remaining travel is half the range; the mapping only ever rises.
    { const long mid = pedalRestDeadZone(lo + 1311 + (hi - 1311) / 2, lo, hi, dz); assert(mid >= 32767 && mid <= 32768); }
    for (long v = 0, last = -1; v <= hi; v += 7) { const long r = pedalRestDeadZone(v, lo, hi, dz); assert(r >= last && r <= hi); last = r; }
    // A single spike like the 32767 read the owner saw is not hidden by the dead zone.
    assert(pedalRestDeadZone(32767, lo, hi, dz) > 32000);
    // A range the game narrowed or offset (SetProperty RANGE) uses its own low end.
    assert(pedalRestDeadZone(-32768 + 600, -32768, 32767, dz) == -32768);
    assert(pedalRestDeadZone(32767, -32768, 32767, dz) == 32767);
    assert(pedalRestDeadZone(5, 0, 1000, dz) == 0 && pedalRestDeadZone(510, 0, 1000, dz) == 500);
    // Off, an unknown range or a reading outside it: passed through unchanged (below the range counts as released).
    assert(pedalRestDeadZone(900, lo, hi, 0) == 900);
    assert(pedalRestDeadZone(900, 100, 100, dz) == 900);
    assert(pedalRestDeadZone(70000, lo, hi, dz) == 70000);
    assert(pedalRestDeadZone(-5, lo, hi, dz) == 0);
    std::puts("PASS: pedal dead zone");
    return 0;
}
