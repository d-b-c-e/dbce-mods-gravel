// Shared force layer for DirectInput proxies (dinput8.dll mods): what the game asks of the wheel, and the player's
// control over it, in one place for every proxy game.
//
//  - Force log (bounded): device properties, cooperative level, acquisition, every effect's type and parameters, starts,
//    stops and the wheel's resting axes, muted or not.
//  - FFB Off/On (STD-004, UX-05): Off stops the effects the game has running and starts none; On starts them again.
//    The F6 panel's Stop FFB and F8 turn it Off. Saved in [FFB] Enabled of the mod's ini.
//  - Strength and effect-gain emulation (optional per game): the wheel is told gain 10000 with the game's own effect gain,
//    times Strength/50, applied to each effect's force (dbce_ffb_gain.hpp). For games that silence or scale effects with
//    DIEFFECT.dwGain, which the MOZA R12 ignores (Rush Rally 3, 2026-10-08). Saved in [FFB] Strength.
//  - The F6 panel's input rule: while the panel is open the game gets no keyboard, mouse or wheel buttons (axes pass).
//  - The panel's FFB page body (DrawPanelPage).
// The test switch (Options::mute) keeps every effect from starting whatever the setting says. While FFB is off or muted,
// the wheel's auto-centring spring is never switched on either (it is a force).
#pragma once
#include <windows.h>

namespace dbce { namespace controls { class ShapeTable; } }   // controls/dbce_shape.hpp, for SetShapes

namespace dbce { namespace ffb {

struct Options {
    const char *iniPath = nullptr;           // [FFB] Enabled (default onByDefault), Strength (default 50)
    bool onByDefault = true;                 // [FFB] Enabled when the key is absent (false: Off until an attended feel test)
    void (*log)(const char *line) = nullptr;
    bool mute = false;                       // test switch: no effect ever starts
    bool gainEmulation = false;              // apply effect gain and Strength in the proxy (see above)
    const char *wheelNote = nullptr;         // optional line under the FFB page's effect list (Advanced)
};

// Once, before DirectInput is used (not from DllMain).
void Init(const Options &options);

// After a successful DirectInput8Create (IDirectInput8A or W: the same table layout). Devices it creates are hooked.
void HookDirectInput(void *directInput8);
// For a proxy that wraps devices itself: hook one created device.
void HookDevice(void *device);

bool On();
// Enable and Strength apply only after a successful save (or immediately when iniPath is null).
// Off always blocks/stops first, even if saving fails; On() then stays false and the panel reports unsaved Off.
void SetOn(bool on);
int Strength();
void SetStrength(int percent);
bool Muted();
// The game window is closing (WM_CLOSE, WM_DESTROY, session end): stop every effect and block new ones for the rest of
// the process, without saving Off, so the wheel never holds torque after the game. Safe before Init and repeated.
void StopForExit();

// The F6 panel's FFB page (dbce_panel widgets): FFB Off/On, Strength (with gain emulation), status; Advanced lists the
// game's effects.
void DrawPanelPage(bool advanced);
// The same facts for a panel drawn from a settings descriptor (UI-7): how many effects the game created and how many
// run, and the Advanced list as one line, "name: state" per effect joined by "; " ("" when none). Null outputs are skipped.
void EffectSummary(int *created, int *running, char *list, size_t listSize);

// Test injection (STD-033 section 6, toolkit docs/controls-contract.md). Development only: a mod calls
// AllowInjection(true) only when its inject.on switch existed at game start. Inject() makes the game's next
// GetDeviceState reads of one object on one device instance (matched by the instance GUID the game created it with,
// nothing else) return a raw sample for `ms` (50-15000), before the game's own binding resolution. kind 1 = axis
// (index 0-7: X Y Z Rx Ry Rz Slider0 Slider1; value in bindMin..bindMax, rescaled to the range the game set),
// 2 = button (0-127; value 0 or nonzero), 3 = hat (0-3; value an angle or -1). Buffered GetDeviceData reads are not
// covered. Returns false with a reason when off, unknown or full.
void AllowInjection(bool on);

// Pedal rest shaping (STD-033, controls/dbce_shape.hpp): from now on, GetDeviceState reads of each shaped axis (matched
// by the instance GUID the device reports) return the sample the game's end-anchored pedal filter reads as the
// contract's value, after any test injection. Once per process, before the game creates its devices; the table must be
// complete and outlive the process's reads (never freed). Returns false for an empty table or a second call.
bool SetShapes(const dbce::controls::ShapeTable *table);
bool Inject(const GUID &instance, int kind, int index, long value, long bindMin, long bindMax, DWORD ms, char *why, size_t whyLen);
bool ParseGuid(const char *text, GUID *out);   // "{xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx}"

}} // namespace dbce::ffb
