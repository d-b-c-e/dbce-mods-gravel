// Telemetry thread: fold InputState / FfbState / FmodState / Ue4State into
// Forza "Data Out" packets and send them over UDP at a fixed rate.
//
// WHY FORZA'S FORMAT
// Nothing here is Forza-specific; the packet is just the most widely parsed
// telemetry layout there is. SimHub, SimRacingStudio, ShakeIt, dashboards,
// motion software - all speak it. Emitting it means zero integration work
// on the receiving side. Three variants, selectable in the ini:
//   sled  232 B  the original v1 packet (physics only)
//   fm7   311 B  sled + "car dash" block (speed, gear, pedals, laps)
//   fh4   324 B  fm7 with Horizon's 12 unknown bytes inserted after the sled
//
// WHAT IS REAL AND WHAT IS DERIVED
//   measured  steer, throttle, brake, clutch, handbrake  (game's own reads)
//             constant / periodic force            (game's own FFB output)
//             rpm, max rpm, speed, gear           (UE4 HUD values, if found)
//   derived   longitudinal accel  = d(speed)/dt
//             lateral accel       = -constant force  (steering torque proxy)
//             surface rumble      = periodic force, or jerk of constant force
//             wheel rotation      = speed / 0.33 m
// Derived fields exist so effect engines have something to key on; the log
// and the listener tool always show the raw channels for calibration.
#include "common.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <cmath>
#include <cstdarg>

Config     g_cfg;
InputState g_input;
FfbState   g_ffb;
FmodState  g_fmod;
Ue4State   g_ue4;

void fmodDumpDiscovery();

// ------------------------------------------------------------------ utils
void sidecarPath(char *out, const char *leaf)
{
    GetModuleFileNameA(nullptr, out, MAX_PATH);
    char *s = strrchr(out, '\\');
    if (s) strcpy(s + 1, leaf); else strcpy(out, leaf);
}

static CRITICAL_SECTION g_logLock;
static bool g_logInit = false;
void logf(const char *fmt, ...)
{
    if (!g_cfg.log) return;
    if (!g_logInit) { InitializeCriticalSection(&g_logLock); g_logInit = true; }
    char path[MAX_PATH]; sidecarPath(path, "milestone_mod.log");
    EnterCriticalSection(&g_logLock);
    if (FILE *f = fopen(path, "a")) {
        SYSTEMTIME t; GetLocalTime(&t);
        fprintf(f, "%02d:%02d:%02d.%03d ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
        va_list ap; va_start(ap, fmt); vfprintf(f, fmt, ap); va_end(ap);
        fputc('\n', f); fclose(f);
    }
    LeaveCriticalSection(&g_logLock);
}

bool safeRead(const void *addr, void *out, size_t n)
{
    SIZE_T got = 0;
    return addr && ReadProcessMemory(GetCurrentProcess(), addr, out, n, &got) && got == n;
}

static void iniStr(const char *ini, const char *sec, const char *key, char *out, int n)
{
    char buf[256];
    if (GetPrivateProfileStringA(sec, key, "\x01", buf, sizeof buf, ini) && buf[0] != '\x01') { strncpy(out, buf, n - 1); out[n - 1] = 0; }
}
static int iniInt(const char *ini, const char *sec, const char *key, int def) { return GetPrivateProfileIntA(sec, key, def, ini); }
static float iniFloat(const char *ini, const char *sec, const char *key, float def)
{
    char b[64] = ""; iniStr(ini, sec, key, b, sizeof b); return b[0] ? (float)atof(b) : def;
}

void loadConfig()
{
    char ini[MAX_PATH]; sidecarPath(ini, "milestone_mod.ini");
    char hex[32] = ""; iniStr(ini, "proxy", "product", hex, sizeof hex);
    if (hex[0]) g_cfg.product = (uint32_t)strtoul(hex, nullptr, 16);
    g_cfg.retype = iniInt(ini, "proxy", "retype", 1) != 0;
    g_cfg.log = iniInt(ini, "proxy", "log", 0) != 0;
    g_cfg.enabled = iniInt(ini, "telemetry", "enabled", 1) != 0;
    iniStr(ini, "telemetry", "host", g_cfg.host, sizeof g_cfg.host);
    g_cfg.port = iniInt(ini, "telemetry", "port", 5300);
    g_cfg.mirrorPort = iniInt(ini, "telemetry", "mirror_port", 0);
    iniStr(ini, "telemetry", "format", g_cfg.format, sizeof g_cfg.format);
    g_cfg.rate = iniInt(ini, "telemetry", "rate", 60);
    iniStr(ini, "telemetry", "race_on", g_cfg.raceOn, sizeof g_cfg.raceOn);
    iniStr(ini, "input", "steer", g_cfg.steer, sizeof g_cfg.steer);
    iniStr(ini, "input", "throttle", g_cfg.throttle, sizeof g_cfg.throttle);
    iniStr(ini, "input", "brake", g_cfg.brake, sizeof g_cfg.brake);
    iniStr(ini, "input", "handbrake", g_cfg.handbrake, sizeof g_cfg.handbrake);
    iniStr(ini, "input", "clutch", g_cfg.clutch, sizeof g_cfg.clutch);
    g_cfg.ffbGain = iniFloat(ini, "ffb", "gain", 1.0f);
    g_cfg.fmodDiscover = iniInt(ini, "fmod", "discover", 1) != 0;
    iniStr(ini, "fmod", "rpm", g_cfg.fmodRpm, sizeof g_cfg.fmodRpm);
    iniStr(ini, "fmod", "load", g_cfg.fmodLoad, sizeof g_cfg.fmodLoad);
    iniStr(ini, "fmod", "speed", g_cfg.fmodSpeed, sizeof g_cfg.fmodSpeed);
    iniStr(ini, "fmod", "lateral_slip", g_cfg.fmodLatSlip, sizeof g_cfg.fmodLatSlip);
    iniStr(ini, "fmod", "longitudinal_slip", g_cfg.fmodLongSlip, sizeof g_cfg.fmodLongSlip);
    iniStr(ini, "fmod", "suspension", g_cfg.fmodSusp, sizeof g_cfg.fmodSusp);
    iniStr(ini, "fmod", "braking", g_cfg.fmodBraking, sizeof g_cfg.fmodBraking);
    iniStr(ini, "fmod", "impact", g_cfg.fmodImpact, sizeof g_cfg.fmodImpact);
    iniStr(ini, "fmod", "impact_velocity", g_cfg.fmodImpactVel, sizeof g_cfg.fmodImpactVel);
    g_cfg.impactGain = iniFloat(ini, "fmod", "impact_gain", 25.0f);
    g_cfg.impactDecayMs = iniInt(ini, "fmod", "impact_decay_ms", 150);
    g_cfg.fmodSpeedScale = iniFloat(ini, "fmod", "speed_scale", 55.0f);
    g_cfg.ue4Enabled = iniInt(ini, "ue4", "enabled", 1) != 0;
    g_cfg.ue4Discover = iniInt(ini, "ue4", "discover", 1) != 0;
    iniStr(ini, "ue4", "rpm", g_cfg.ue4Rpm, sizeof g_cfg.ue4Rpm);
    iniStr(ini, "ue4", "maxrpm", g_cfg.ue4MaxRpm, sizeof g_cfg.ue4MaxRpm);
    iniStr(ini, "ue4", "speed", g_cfg.ue4Speed, sizeof g_cfg.ue4Speed);
    iniStr(ini, "ue4", "gear", g_cfg.ue4Gear, sizeof g_cfg.ue4Gear);
    g_cfg.speedScale = iniFloat(ini, "ue4", "speed_scale", 1.0f / 3.6f);
    g_cfg.gearOffset = iniInt(ini, "ue4", "gear_offset", 0);
}

// ------------------------------------------------------------ forza packet
// The layout and the encoder come from dbce-wheel-mod-toolkit
// (lib/toolkit/include/forza_packet.h). The Sled and Dash structs that used
// to live here moved there unchanged - every field keeps its name - so the
// 232 / 311 / 324 rule is now enforced once, by a header with its own test.
#include "forza_packet.h"
using dbce::forza::Sled;
using dbce::forza::Dash;

static uint8_t u8(float f) { f = f < 0 ? 0 : f > 1 ? 1 : f; return (uint8_t)(f * 255.f + 0.5f); }

static DWORD WINAPI telemetryThread(LPVOID)
{
    const bool deliver = g_cfg.enabled && !recordingMuted();
    WSADATA w{};
    SOCKET s = INVALID_SOCKET;
    if (deliver && WSAStartup(MAKEWORD(2, 2), &w) == 0) s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    sockaddr_in to{}; to.sin_family = AF_INET; to.sin_port = htons((u_short)g_cfg.port);
    inet_pton(AF_INET, g_cfg.host, &to.sin_addr);
    sockaddr_in mirror = to; mirror.sin_port = htons((u_short)g_cfg.mirrorPort);
    const bool dash = _stricmp(g_cfg.format, "sled") != 0, fh4 = _stricmp(g_cfg.format, "fh4") == 0;
    const int rate = g_cfg.rate > 0 && g_cfg.rate <= 240 ? g_cfg.rate : 60;
    const float dt = 1.f / rate;
    logf("[observer] %d Hz; UDP %s", rate, deliver ? "enabled" : "disabled; observation continues");

    uint8_t pkt[324]; float prevSpeed = 0, prevConst = 0; ULONGLONG nextStatus = 0, nextDump = 0;
    uint32_t lastImpactSeq = 0; ULONGLONG impactAt = 0; float impactMag = 0.f;
    LARGE_INTEGER freq, t0; QueryPerformanceFrequency(&freq); QueryPerformanceCounter(&t0);
    uint64_t tick = 0;
    for (;; ++tick) {
        // pace to the target rate without drifting
        LARGE_INTEGER now; QueryPerformanceCounter(&now);
        double due = double(tick) / rate, elapsed = double(now.QuadPart - t0.QuadPart) / freq.QuadPart;
        if (due > elapsed) Sleep((DWORD)((due - elapsed) * 1000));
        // Disk/discovery delays must not cause a burst of backdated duplicate
        // samples. Record actual time and resume at the next deadline.
        else if (elapsed - due > 1.0 / rate) tick = uint64_t(elapsed * rate);

        bool ue = ue4Poll();
        recordingPoll();
        // Gravel exposes no speed on the HUD widget, so FMOD's normalised
        // VehicleSpeed is the source; a UE4 property wins where one exists.
        float speed = 0.f;
        if (ue && g_ue4.speed.load() != 0.f) speed = g_ue4.speed.load() * g_cfg.speedScale;
        else if (g_fmod.speed >= 0.f) speed = g_fmod.speed.load() * g_cfg.fmodSpeedScale;
        float rpm = ue ? g_ue4.rpm.load() : (g_fmod.rpm >= 0 ? g_fmod.rpm.load() : 0.f);
        float maxRpm = ue && g_ue4.maxRpm > 0 ? g_ue4.maxRpm.load() : 8000.f;
        float cf = g_ffb.constant.load(), pf = g_ffb.periodic.load();
        float accZ = (speed - prevSpeed) / dt; prevSpeed = speed;
        // A collision becomes a decaying spike on longitudinal acceleration,
        // which is what an impact physically is and what effect engines key
        // on. VehicleSpeed is smoothed for audio, so the raw derivative alone
        // blunts a wall strike into a gentle slowdown.
        {
            uint32_t seq = g_fmod.impactSeq.load();
            if (seq != lastImpactSeq) {
                lastImpactSeq = seq;
                impactAt = GetTickCount64();
                // Weighted towards collision speed: Intensity saturates at 1.0
                // for almost every hit, so weighting it flattened a kerb strike
                // and a head-on into the same nudge.
                float vel = g_fmod.impactVel.load();
                impactMag = g_fmod.impact.load() * (0.15f + vel * 1.8f) * g_cfg.impactGain;
            }
            if (impactAt) {
                ULONGLONG age = GetTickCount64() - impactAt;
                if (age >= (ULONGLONG)g_cfg.impactDecayMs) { impactAt = 0; impactMag = 0.f; }
                else accZ -= impactMag * (1.f - float(age) / float(g_cfg.impactDecayMs));
            }
        }
        float jerk = fabsf(cf - prevConst) / dt; prevConst = cf;
        float rumble = pf > 0.f ? pf : (jerk > 1.f ? 1.f : jerk);
        int gear = ue ? g_ue4.gear.load() + g_cfg.gearOffset : 0;

        Sled sl{}; Dash d{};
        sl.isRaceOn = (_stricmp(g_cfg.raceOn, "always") == 0) ? 1 : (ue ? 1 : 0);
        sl.timestampMs = GetTickCount();
        sl.engineMaxRpm = maxRpm; sl.engineIdleRpm = maxRpm * 0.12f; sl.currentEngineRpm = rpm;
        sl.accX = -cf * 9.81f; sl.accZ = accZ; sl.velZ = speed;
        // Real physics channels, straight off the engine-audio parameters.
        // FMOD stops updating the moment a race ends, so the last in-race values
        // would otherwise persist through the menus - a shaker driven from them
        // would sit on a constant output. No live vehicle means no physics.
        float lat = 0.f, lon = 0.f, susp = 0.f, torque = 0.f;
        if (ue) {
            lat = g_fmod.latSlip.load(); lon = g_fmod.longSlip.load();
            susp = g_fmod.susp.load();
            torque = g_fmod.load >= 0 ? g_fmod.load.load() : 0.f;
        } else {
            speed = 0.f;
        }
        float combined = sqrtf(lat * lat + lon * lon);
        if (susp > 0.f) rumble = susp;          // suspension beats an FFB guess
        for (int i = 0; i < 4; ++i) {
            sl.wheelRotSpeed[i] = speed / 0.33f;
            sl.surfaceRumble[i] = rumble;
            sl.onRumble[i] = susp > 0.7f ? 1 : 0;
            sl.normSusp[i] = susp;
            sl.suspTravel[i] = susp * 0.15f;     // ~150 mm of travel
            sl.slipRatio[i] = lon;
            sl.slipAngle[i] = lat;
            sl.combinedSlip[i] = combined;
        }
        sl.drivetrain = 2; sl.cylinders = 4;
        d.speed = speed; d.gear = (uint8_t)(gear < 0 ? 0 : gear > 10 ? 10 : gear);
        d.torque = torque * 400.f;               // normalised -> Nm, nominal
        d.power = torque * rpm * 0.05f;
        d.accel = u8(g_input.throttle); d.brake = u8(g_input.brake); d.clutch = u8(g_input.clutch);
        d.handbrake = u8(g_input.handbrake);
        float st = g_input.steer.load(); st = st < -1 ? -1 : st > 1 ? 1 : st;
        d.steer = (int8_t)(st * 127.f);

        // Sizes are exact and checked by the receiver: sled 232, fm7 311,
        // fh4/fh5 324. The Horizon packet is 232 + 12 pad + 79 dash + ONE
        // trailing byte; emitting 323 makes SimHub log "unprocessed data" at
        // packet rate and never connect.
        const dbce::forza::Layout layout = !dash ? dbce::forza::FORZA_SLED_232
                                          : fh4 ? dbce::forza::FORZA_HORIZON_324 : dbce::forza::FORZA_FM7_DASH_311;
        int n = dbce::forza::build(layout, sl, d, pkt, sizeof pkt, 'R');   // 'R': this mod's frames, for forza_probe
        static int loggedSize = 0;
        if (loggedSize != n) { loggedSize = n; logf("[tx] packet size %d bytes (%s)", n, g_cfg.format); }
        if (s != INVALID_SOCKET) {
        int rc = sendto(s, (const char *)pkt, n, 0, (sockaddr *)&to, sizeof to);
        static int lastRc = -2, lastErr = 0;
        if (rc != lastRc) { lastRc = rc; lastErr = rc < 0 ? WSAGetLastError() : 0;
                            logf("[tx] sendto -> %d (err %d) socket %d mirror %d", rc, lastErr, (int)s, g_cfg.mirrorPort); }
        if (g_cfg.mirrorPort) {
            int mrc = sendto(s, (const char *)pkt, n, 0, (sockaddr *)&mirror, sizeof mirror);
            static int lastM = -2;
            if (mrc != lastM) { lastM = mrc; logf("[tx] mirror sendto -> %d (err %d) port %d", mrc,
                                                  mrc < 0 ? WSAGetLastError() : 0, g_cfg.mirrorPort); }
        }
        }

        ULONGLONG ms = GetTickCount64();
        if (ms >= nextStatus) {
            nextStatus = ms + 5000;
            logf("[tx] input:%s(%u reads) ffb:%s(%u upd, %u eff) fmod:%s(%u calls) ue4:%s%s | steer %.2f thr %.2f brk %.2f hb %.2f (raw %d %d %d) | cf %.2f pf %.2f | rpm %.0f/%.0f spd %.1f gear %d | slip %.2f/%.2f susp %.2f trq %.2f",
                 g_input.live ? "live" : "-", (unsigned)g_input.reads.load(), g_ffb.live ? "live" : "-",
                 (unsigned)g_ffb.updates.load(), (unsigned)g_ffb.effects.load(), g_fmod.live ? "live" : "-",
                 (unsigned)g_fmod.calls.load(), g_ue4.ready ? "ready" : "-", ue ? "+live" : "",
                 g_input.steer.load(), g_input.throttle.load(), g_input.brake.load(), g_input.handbrake.load(),
                 g_input.rawSteer.load(), g_input.rawThr.load(), g_input.rawBrk.load(),
                 cf, pf, rpm, maxRpm, speed, gear, lat, lon, susp, torque);
        }
        if (ms >= nextDump) { nextDump = ms + 30000; fmodDumpDiscovery(); }
    }
}

void telemetryStart()
{
    static bool started = false;
    if (started) return;
    started = true;
    HANDLE thread = CreateThread(nullptr, 0, telemetryThread, nullptr, 0, nullptr);
    if (thread) CloseHandle(thread);
    else { started = false; logf("[observer] thread creation failed"); }
}
