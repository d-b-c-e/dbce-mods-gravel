#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>

// Actual production declarations/dispatcher, with only the memory lock replaced by inert stand-ins.
/* CAMERA_STATE */
/* CAMERA_STEPS */
int g_camLock = 0;
CameraOffsets g_cam;
std::atomic<bool> g_realistic{false};
inline void fixtureAcquire(int*) {}
inline void fixtureRelease(int*) {}
#define AcquireSRWLockExclusive fixtureAcquire
#define ReleaseSRWLockExclusive fixtureRelease
/* CAMERA_DISPATCH */
#undef AcquireSRWLockExclusive
#undef ReleaseSRWLockExclusive

int main() {
    int checks = 0, failures = 0;
    auto check = [&](bool ok, const char* why) { ++checks; if (!ok) { ++failures; std::printf("FAIL: %s\n", why); } };
    auto action = [](int key) { for (int i = 0; i < CamCount; ++i) if (g_camKeys[i] == key) return i; return -1; };
    const int plus = action(VK_ADD), minus = action(VK_SUBTRACT), reset = action(VK_NUMPAD5);
    check(plus >= 0 && minus >= 0 && reset >= 0, "all FOV/reset keys remain bound");
    if (plus < 0 || minus < 0 || reset < 0) return 1;
    g_cam = CameraOffsets{}; applyCamera(plus, 1);
    check(g_cam.fov == -g_fovStep, "numpad + narrows field of view");
    g_cam = CameraOffsets{}; applyCamera(minus, 1);
    check(g_cam.fov == g_fovStep, "numpad - widens field of view");
    applyCamera(plus, 1); check(g_cam.fov == 0, "opposite taps cancel");
    g_cam.fov = 3; applyCamera(plus, 2);
    check(g_cam.fov == 3 - 2 * g_fovStep, "repeated plus applies narrower offsets to an existing tune");
    applyCamera(reset, 1); check(g_cam.fov == 0, "reset still returns to the calculated start");
    applyCamera(action(VK_NUMPAD8), 1); check(g_cam.x == g_moveStep && g_cam.fov == 0, "position key is unchanged");
    check(action(VK_F6) == -1, "unrelated key stays unbound");
    // Each Game / Realistic set keeps its own FOV offset; position and tilt are shared (STD-041/046).
    g_cam = CameraOffsets{}; g_realistic = true; applyCamera(minus, 2);
    check(g_cam.fovRealistic == 2 * g_fovStep && g_cam.fov == 0, "FOV keys tune only the Realistic set while it is active");
    g_realistic = false; applyCamera(plus, 1);
    check(g_cam.fov == -g_fovStep && g_cam.fovRealistic == 2 * g_fovStep, "Game set tune leaves the Realistic tune alone");
    applyCamera(action(VK_NUMPAD8), 1); g_realistic = true; applyCamera(reset, 1);
    check(g_cam.fovRealistic == 0 && g_cam.x == 0 && g_cam.fov == -g_fovStep, "reset returns the active set and position, other set keeps its tune");
    applyCamera(minus, 100); check(g_cam.fovRealistic == 40, "Realistic offset is clamped like the Game offset");
    g_realistic = false;
    std::printf("%d checks, %d failures; actual key dispatcher, no window/game/device calls\n", checks, failures);
    return failures ? 1 : 0;
}
