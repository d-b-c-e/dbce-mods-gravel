// The XInput output fence: the rumble half of the closed force fence (dbce_fence.hpp covers DirectInput only).
// Header-only, x64 and x86, no link dependency (it uses the XInput types, never the library).
//
// A proxy that points the game's XInputSetState import slot at its own wrapper calls SetState() with the original
// export. Open (normal play): the call is forwarded with the game's own arguments and its result returned, untouched.
// Closed (a fenced or muted test run): the game's motor speeds never reach the pad; the original is called with both
// motors at zero instead, so the motors stay at rest and the game still gets the real connection result
// (ERROR_SUCCESS or ERROR_DEVICE_NOT_CONNECTED). The game's own XINPUT_VIBRATION is never written. A null vibration is
// forwarded as is (it carries no force). A missing original answers ERROR_DEVICE_NOT_CONNECTED.
//
// Why this matters: a virtual pad's rumble can be turned into wheel force by the tool feeding that pad, so an
// all-haptics-off test cannot leave XInputSetState open. Reads (XInputGetState, XInputGetCapabilities) are not touched.
#pragma once
#include <windows.h>
#include <xinput.h>

namespace dbce {
namespace xinput_fence {

typedef DWORD(WINAPI *SetStateFn)(DWORD user, XINPUT_VIBRATION *vibration);

struct Counters {
    volatile LONG forwarded;   // open: passed through unchanged
    volatile LONG blocked;     // closed: a non-zero request replaced by zero motors
    volatile LONG zeroed;      // closed: a request that was already zero (still sent as zero)
};

inline Counters &counters() { static Counters c; return c; }

inline DWORD SetState(SetStateFn real, bool closed, DWORD user, XINPUT_VIBRATION *vibration)
{
    if (!real) return ERROR_DEVICE_NOT_CONNECTED;
    if (!closed || !vibration) {
        if (!closed) InterlockedIncrement(&counters().forwarded);
        return real(user, vibration);
    }
    InterlockedIncrement(vibration->wLeftMotorSpeed || vibration->wRightMotorSpeed ? &counters().blocked : &counters().zeroed);
    XINPUT_VIBRATION rest = {};
    return real(user, &rest);
}

}  // namespace xinput_fence
}  // namespace dbce
