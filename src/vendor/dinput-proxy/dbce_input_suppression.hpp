// Input-only panel policy. No device calls, panel dependency, force hooks or writers.
#pragma once
#include "dbce_joyformat.hpp"
#include <cstring>

namespace dbce { namespace input {

// Preserve the fence's joystick IDs: injection/shaping accept only exact 1/2.
enum { Unknown = 0, Joystick = 1, Joystick2 = 2, Keyboard = 3, Mouse = 4, Mouse2 = 5 };

inline int format(LPCDIDATAFORMAT f)
{
    if (const int joy = fence::standardFormat(f)) return joy;
    if (!f || f->dwSize != sizeof(DIDATAFORMAT) || f->dwObjSize != sizeof(DIOBJECTDATAFORMAT) ||
        f->dwFlags != DIDF_RELAXIS || !f->rgodf) return Unknown;
    const bool keys = f->dwDataSize == 256 && f->dwNumObjs == 256;
    const bool mouse = (f->dwDataSize == sizeof(DIMOUSESTATE) && f->dwNumObjs == 7) ||
                       (f->dwDataSize == sizeof(DIMOUSESTATE2) && f->dwNumObjs == 11);
    if (!keys && !mouse) return Unknown;
    for (DWORD i = 0; i < f->dwNumObjs; ++i) {
        const auto &o = f->rgodf[i];
        const DWORD kind = DIDFT_GETTYPE(o.dwType);
        if (o.dwFlags != 0) return Unknown;
        if (keys) {
            if (o.dwOfs != i || !o.pguid || !IsEqualGUID(*o.pguid, GUID_Key) || !(kind & DIDFT_BUTTON) || (kind & ~DIDFT_BUTTON)) return Unknown;
        } else if (i < 3) {
            const GUID *axes[] = {&GUID_XAxis, &GUID_YAxis, &GUID_ZAxis};
            if (o.dwOfs != i * sizeof(LONG) || !o.pguid || !IsEqualGUID(*o.pguid, *axes[i]) || !(kind & DIDFT_AXIS) || (kind & ~DIDFT_AXIS)) return Unknown;
        } else if (o.dwOfs != 12 + i - 3 || o.pguid || !(kind & DIDFT_BUTTON) || (kind & ~DIDFT_BUTTON)) return Unknown;
    }
    return keys ? Keyboard : f->dwDataSize == sizeof(DIMOUSESTATE) ? Mouse : Mouse2;
}

// Call only after a successful read and all input producers, before observers.
inline void suppressState(int layout, DWORD cb, void *data)
{
    if (!data) return;
    if ((layout == Keyboard && cb == 256) || (layout == Mouse && cb == sizeof(DIMOUSESTATE)) ||
        (layout == Mouse2 && cb == sizeof(DIMOUSESTATE2))) std::memset(data, 0, cb);
    else if (layout == Joystick && cb == sizeof(DIJOYSTATE)) {
        auto *s = static_cast<DIJOYSTATE *>(data);
        std::memset(s->rgbButtons, 0, sizeof s->rgbButtons);
        for (auto &p : s->rgdwPOV) p = (DWORD)-1;
    } else if (layout == Joystick2 && cb == sizeof(DIJOYSTATE2)) {
        auto *s = static_cast<DIJOYSTATE2 *>(data);
        std::memset(s->rgbButtons, 0, sizeof s->rgbButtons);
        for (auto &p : s->rgdwPOV) p = (DWORD)-1;
    }
}

// The caller already performed the original read with unchanged arguments. Zero
// reported events for PEEK too, without draining its queue or changing HRESULT.
// shortcut: buffered axes are also hidden; queued/held input may return on close.
inline void suppressData(LPDWORD count) { if (count) *count = 0; }

}} // namespace dbce::input
