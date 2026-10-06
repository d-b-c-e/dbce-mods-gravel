// Shared state and helpers for the Milestone telemetry mod.
//
// One DLL (dinput8.dll) with four cooperating modules:
//   proxy.cpp      DirectInput8 forwarding; re-types the wheel as DRIVING;
//                  taps the game's own reads of the wheel (input) and its
//                  force-feedback effects (FFB)          -> InputState, FfbState
//   fmod_tap.cpp   hooks FMOD Studio setParameterValue   -> FmodState
//   ue4.cpp        reads live vehicle values through UE4 reflection -> Ue4State
//   telemetry.cpp  60 Hz thread folding all of it into Forza "Data Out" UDP
//   triple.cpp     triple screens through the engine's emulated stereo device
//
// Everything a game can read is left untouched except dwDevType. All taps are
// observe-only.
#pragma once
#define WIN32_LEAN_AND_MEAN
#define DIRECTINPUT_VERSION 0x0800
#include <windows.h>
#include <dinput.h>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

// ------------------------------------------------------------------ config
struct Config {
    // [proxy]
    uint32_t product      = 0x0006346E;   // (PID<<16)|VID of the wheel to re-type
    bool     retype       = true;
    bool     log          = false;
    // [telemetry]
    bool     enabled      = true;
    char     host[64]     = "127.0.0.1";
    int      port         = 5300;
    int      mirrorPort   = 0;            // optional second destination (validation listener)
    char     format[8]    = "fm7";        // fm7 (311 B) | fh4 (324 B) | sled (232 B)
    int      rate         = 60;
    char     raceOn[8]    = "auto";       // auto | always
    // [input]  DIJOYSTATE field names: lX lY lZ lRx lRy lRz slider0 slider1
    char     steer[12]    = "lX";
    char     throttle[12] = "lZ";
    char     brake[12]    = "lRz";
    char     handbrake[12]= "slider0";
    char     clutch[12]   = "";
    // [ffb]
    float    ffbGain      = 1.0f;         // scale applied to normalised force
    // [fmod]
    bool     fmodDiscover = true;         // log every distinct parameter name
    // Gravel drives its engine audio from real physics, so these are the best
    // telemetry source in the game. All are normalised unless noted.
    char     fmodRpm[32]      = "RPM";
    char     fmodLoad[32]     = "torque";
    char     fmodSpeed[32]    = "VehicleSpeed";
    char     fmodLatSlip[32]  = "LateralSlip";
    char     fmodLongSlip[32] = "LongitudinalSlip";
    char     fmodSusp[32]     = "SuspensionMovement";
    char     fmodBraking[32]  = "BrakingForce";
    // Collision parameters. FMOD sets these only when a crash/scrape sound
    // fires - a few hundred calls a race against ~175k for VehicleSpeed -
    // so they are events, not continuous channels.
    char     fmodImpact[32]   = "Intensity";
    char     fmodImpactVel[32]= "Speed";
    // Measured over 368 collisions in Gravel: Intensity is effectively binary
    // (median 1.0, mean 0.88) so it says THAT you hit something, not how hard;
    // the collision Speed parameter (0..0.46, median 0.23) is what actually
    // discriminates a scrape from a crash. So velocity carries the magnitude.
    float    impactGain     = 80.0f;      // peak m/s^2 at full collision speed
    int      impactDecayMs  = 150;
    // VehicleSpeed is 0..1 of the car's top speed; multiply to get m/s.
    // Calibrate against the in-game speedo: m/s = kmh / 3.6.
    float    fmodSpeedScale = 55.0f;
    // [ue4]
    bool     ue4Enabled   = true;
    bool     ue4Discover  = true;         // list matching property names once
    char     ue4Rpm[48]   = "VehicleCurrentRPM";
    char     ue4MaxRpm[48]= "VehicleMaxRPM";
    char     ue4Speed[48] = "VehicleCurrentSpeed";
    char     ue4Gear[48]  = "VehicleCurrentGear";
    float    speedScale   = 1.0f / 3.6f;  // raw -> m/s (default assumes km/h)
    int      gearOffset   = 0;            // added to raw gear before Forza mapping
};
extern Config g_cfg;
void loadConfig();

// ------------------------------------------------------------------ logging
void logf(const char *fmt, ...);
void sidecarPath(char *out, const char *leaf);   // <exe dir>\leaf

// ------------------------------------------------------------ shared state
// Written by hooks on the game's threads, read by the telemetry thread.
// Plain atomics; each field is independently consistent, which is all a
// 60 Hz sampler needs.
struct InputState {
    std::atomic<float> steer{0};      // -1..1
    std::atomic<float> throttle{0};   // 0..1
    std::atomic<float> brake{0};
    std::atomic<float> clutch{0};
    std::atomic<float> handbrake{0};
    std::atomic<uint32_t> buttons{0}; // first 32 as bitmask (for the log only)
    std::atomic<int>   rawSteer{0}, rawThr{0}, rawBrk{0};   // last raw axis values, for calibration
    std::atomic<bool>  live{false};   // a GetDeviceState has succeeded
    std::atomic<uint32_t> reads{0};
};
struct FfbState {
    std::atomic<float> constant{0};   // -1..1, signed, latest constant-force magnitude
    std::atomic<float> periodic{0};   // 0..1, latest periodic (rumble) magnitude
    std::atomic<float> spring{0};     // 0..1
    std::atomic<float> damper{0};     // 0..1
    std::atomic<bool>  live{false};
    std::atomic<uint32_t> updates{0};
    std::atomic<uint32_t> effects{0}; // effects created so far
};
struct FmodState {
    std::atomic<float> rpm{-1};       // -1 = never seen
    std::atomic<float> load{-1};      // engine torque 0..1
    std::atomic<float> speed{-1};     // 0..1 of top speed
    std::atomic<float> latSlip{0};    // 0..~0.6
    std::atomic<float> longSlip{0};   // -0.5..1, negative = lockup
    std::atomic<float> susp{0};       // 0..1 suspension movement
    std::atomic<float> braking{0};    // 0..1
    std::atomic<float> impact{0};     // 0..1, collision intensity (event)
    std::atomic<float> impactVel{0};  // 0..1, collision speed (event)
    std::atomic<uint32_t> impactSeq{0};   // bumped on each fresh impact event
    std::atomic<bool>  live{false};
    std::atomic<uint32_t> calls{0};
};
struct Ue4State {
    std::atomic<float> rpm{0};
    std::atomic<float> maxRpm{0};
    std::atomic<float> speed{0};      // raw units from the game
    std::atomic<int>   gear{0};
    std::atomic<bool>  live{false};   // a vehicle/HUD instance is being read
    std::atomic<bool>  ready{false};  // GNames/GObjects resolved
};
extern InputState g_input;
extern FfbState   g_ffb;
extern FmodState  g_fmod;
extern Ue4State   g_ue4;

// -------------------------------------------------------------- entry points
void proxyInit();          // called once, off the loader lock
void fmodTapInstall();     // delay-load IAT hook
void ue4Init();            // start discovery (idempotent)
bool ue4Poll();            // refresh g_ue4; returns live
void telemetryStart();     // spawn the sender thread
void tripleAttach();       // DllMain: three projected views via emulated stereo (triple.cpp)
void tripleReport();       // log what tripleAttach did, once logging is configured

// Safe read of this process's memory; false on unmapped pages instead of AV.
bool safeRead(const void *addr, void *out, size_t n);
template <typename T> bool rd(const void *addr, T &out) { return safeRead(addr, &out, sizeof(T)); }
