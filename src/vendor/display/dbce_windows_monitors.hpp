// MIT. Source-vendored Windows monitor reader for dbce_racing_row.hpp, the C++ counterpart of WindowsMonitors.cs: each
// display's desktop rectangle and the primary flag, read with EnumDisplayMonitors + GetMonitorInfoW. Read-only: it
// changes no display setting, profile or window. Coordinates are those of the calling process's DPI context. Kept
// apart from the pure policy so fixtures and non-Windows hosts never need it. Include <windows.h> first, as the mod
// already does, with whatever WIN32_LEAN_AND_MEAN/NOMINMAX settings it uses.
#pragma once
#include <windows.h>
#include <vector>
#include "dbce_racing_row.hpp"

namespace dbce::display {

// The displays Windows reports now, in its enumeration order. False, with `out` empty, if Windows refuses the query.
// Call it from the mod's own code: a mod that hooks the game's GetMonitorInfoW import (to report the span) does not
// change what this reads.
inline bool read_windows_monitors(std::vector<MonitorRect>& out)
{
    struct State
    {
        std::vector<MonitorRect>* list;
        bool failed;
    } state{&out, false};
    out.clear();
    const BOOL enumerated = EnumDisplayMonitors(
        nullptr, nullptr,
        [](HMONITOR monitor, HDC, LPRECT, LPARAM data) -> BOOL {
            auto& s = *reinterpret_cast<State*>(data);
            MONITORINFO info{};
            info.cbSize = sizeof info;
            if (!GetMonitorInfoW(monitor, &info))
            {
                s.failed = true;
                return FALSE;
            }
            const RECT& r = info.rcMonitor;
            s.list->push_back({r.left, r.top, r.right - r.left, r.bottom - r.top, (info.dwFlags & MONITORINFOF_PRIMARY) != 0});
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&state));
    if (!enumerated || state.failed)
    {
        out.clear();
        return false;
    }
    return true;
}

// The racing row for the displays Windows reports now (see select_racing_row), with the caller's wide-primary aspect.
inline RacingRow select_windows_racing_row(double surround_aspect)
{
    std::vector<MonitorRect> monitors;
    if (!read_windows_monitors(monitors)) return detail::none("Windows did not report the displays (EnumDisplayMonitors or GetMonitorInfo failed)");
    return select_racing_row(monitors, surround_aspect);
}

} // namespace dbce::display
