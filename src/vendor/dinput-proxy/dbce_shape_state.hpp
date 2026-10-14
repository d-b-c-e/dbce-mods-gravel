// Pedal rest shaping (controls/dbce_shape.hpp) applied to one just-read GetDeviceState buffer. Shared by the fence
// (FencedDevice::GetDeviceState) and the force layer's GetDeviceState hook, after any test injection, so an injected raw
// sample is shaped exactly like a physical one. Standard formats only (c_dfDIJoystick / c_dfDIJoystick2 read at their
// own size); a device with shapes read any other way is reported unavailable, never shaped by guesswork.
#pragma once
#ifndef DIRECTINPUT_VERSION
#define DIRECTINPUT_VERSION 0x0800
#endif
#include <windows.h>
#include <dinput.h>
#include <stdio.h>
#include <string>
#include "../controls/dbce_shape.hpp"

namespace dbce { namespace shape {

// format: 1 c_dfDIJoystick, 2 c_dfDIJoystick2, 0 any other (dbce_joyformat.hpp standardFormat). range(axis, min, max)
// reads the range the game set (DIPROP_RANGE). first(shape, miss, min, max): see ShapeTable::apply (for the log).
template <class Range, class First>
dbce::controls::ShapeResult applyToState(const dbce::controls::ShapeTable &table, const std::string &instance, int format,
                                         DWORD cb, LPVOID data, Range range, First first)
{
    if (!table.countFor(instance)) return {};
    const bool standard = (format == 1 && cb == sizeof(DIJOYSTATE)) || (format == 2 && cb == sizeof(DIJOYSTATE2));
    if (!data) return table.missAll(instance, dbce::controls::ShapeMiss::NoAxes, first);
    if (!standard) return table.missAll(instance, dbce::controls::ShapeMiss::Format, first);
    dbce::controls::StateView v;
    v.axes = (long *)&((DIJOYSTATE *)data)->lX;
    return table.apply(instance, v, range, first);
}

// The log line for ShapeTable's first-time events.
inline std::string firstLine(const dbce::controls::PedalShape &s, dbce::controls::ShapeMiss miss, long mn, long mx)
{
    char b[400];
    if (miss == dbce::controls::ShapeMiss::None)
        sprintf_s(b, "shape: %s rest %ld on %s axis %d first corrected (game range %ld..%ld)", s.action.c_str(), s.binding.rest,
                  s.instance.c_str(), s.axis, mn, mx);
    else
        sprintf_s(b, "shape: %s rest %ld on %s axis %d NOT corrected: %s; the raw value is used", s.action.c_str(), s.binding.rest,
                  s.instance.c_str(), s.axis, dbce::controls::missText(miss));
    return b;
}

}} // namespace dbce::shape
