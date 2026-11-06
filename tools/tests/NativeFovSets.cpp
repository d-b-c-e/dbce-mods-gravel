#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

// Actual production effective-FOV block from src/triple.cpp, with only the rig configuration it reads stubbed.
constexpr double Pi = 3.14159265358979323846;
struct FixtureRig { double panelW = 708.4, panelH = 398.5, eye = 0, vfov = 58.715; } g_tc;
/* FOV_BLOCK */

int main() {
    int checks = 0, failures = 0;
    auto check = [&](bool ok, const char* why) { ++checks; if (!ok) { ++failures; std::printf("FAIL: %s\n", why); } };
    auto label = [](bool realistic, float gameOffset, float rigOffset) {
        static char b[96]; setLabelFor(b, sizeof b, realistic, gameOffset, rigOffset); return std::string(b);
    };
    auto expected = [](double v, bool custom) {
        char b[96]; std::snprintf(b, sizeof b, "Realistic FOV %.0f\xC2\xB0%s", v, custom ? " (Custom)" : ""); return std::string(b);
    };

    // Reviewer case: a valid long eye distance starts below 20 degrees (398.5 mm panel, 1500 mm eye: about 15.13).
    g_tc.eye = 1500;
    const double base = rigVerticalFov();
    check(base > 15.0 && base < 15.3, "1500 mm eye gives about 15.1 degrees");
    check(eyeFor(90.f, true, 0) == 1500, "no offset keeps the exact measured eye");
    check(label(true, 0, 0) == expected(base, false) && label(true, 0, 0).find("15") != std::string::npos, "label shows the measured start, not 20");
    check(eyeFor(90.f, true, -2) >= 1500 - 1e-9, "narrowing below 20 never widens the rendered view");
    check(tunedFov(base, -2) <= base, "narrowing tune never raises the FOV");
    check(eyeFor(90.f, true, 2) < 1500, "widening tune widens");
    check(label(true, 0, 2) == expected(base + 2, true), "label uses the same tuned FOV as the projection");

    // Owner rig (Wheelkit 660 mm eye, about 33.6 degrees).
    g_tc.eye = 660;
    const double owner = rigVerticalFov();
    check(owner > 33.4 && owner < 33.8, "660 mm eye gives about 33.6 degrees");
    check(eyeFor(90.f, true, -2) > 660 && eyeFor(90.f, true, 2) < 660, "numpad + narrows and - widens around the start");
    check(label(true, 0, 0) == expected(owner, false), "owner label is the seat-true start");

    // vertical_fov above 120 is kept as the start and only tuned towards the range.
    g_tc.eye = 0; g_tc.vfov = 130;
    check(tunedFov(rigVerticalFov(), 0) == 130 && tunedFov(130, 4) == 130 && tunedFov(130, -4) == 126, "start above 120 kept; widening stops, narrowing works");

    // The rule never reverses a tune, for low, normal and high starts.
    bool monotonic = true;
    for (double b : {15.13, 33.6, 90.0, 130.0})
        for (int o = -40; o < 40; o += 2) if (tunedFov(b, o + 2) < tunedFov(b, o) || (o >= 0 && tunedFov(b, o) < b) || (o <= 0 && tunedFov(b, o) > b)) monotonic = false;
    check(monotonic, "tune is monotonic and keeps the start on the right side");
    check(tunedFov(90, -2) == 88 && tunedFov(90, 200) == 120 && tunedFov(25, -40) == 20, "ordinary game FOV keeps the 20..120 range");

    // Game set: eye from the tuned game FOV; label carries only the Custom marker.
    check(std::fabs(eyeFor(88.f, false, 5) - g_tc.panelW / 2 / std::tan(88 * Pi / 360)) < 1e-9, "Game set ignores the Realistic offset");
    check(label(false, 0, 3) == "Game FOV" && label(false, -2, 0) == "Game FOV (Custom)", "Game label marks only its own tune");

    std::printf("%d checks, %d failures; actual effective-FOV block, no window/game/device calls\n", checks, failures);
    return failures ? 1 : 0;
}
