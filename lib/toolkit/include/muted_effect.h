// Diagnostic sink policy for a proxy which observes requests before forwarding.
// Never mutate the caller's DIEFFECT or parameter buffers. The proxy must also
// suppress Start/Escape/device resume commands and disable device autocentre
// before acquisition. This helper alone is NOT a complete hardware mute.
#pragma once
#include <windows.h>
#include <dinput.h>
#include <cstddef>
#include <cstring>

namespace dbce { namespace dproxy {
struct MutedEffectWrite {
    DIEFFECT effect{};
    DWORD flags = 0;
    bool copy(LPCDIEFFECT source, DWORD requestedFlags) {
        if (!source || (source->dwSize != sizeof(DIEFFECT) && source->dwSize != sizeof(DIEFFECT_DX5))) return false;
        std::memcpy(&effect, source, source->dwSize);
        effect.dwGain = 0;
        flags = (requestedFlags | DIEP_GAIN) & ~DIEP_START;
        return true;
    }
};
}} // dbce::dproxy
