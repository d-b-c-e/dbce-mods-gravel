// Developer-only sampled signal capture. No pose writes or input injection.
// The request is separate from player settings and is claimed once at startup.
#include "common.h"
#include "session_writer.h"
#include <algorithm>
#include <memory>

namespace {
bool g_muted = false;
bool g_finished = false;
std::wstring g_result;
std::string g_id;
double g_seconds = 60;
std::unique_ptr<dbce::session::Writer> g_writer;
LARGE_INTEGER g_begin{}, g_frequency{};
double g_drivingAt = -1;

std::wstring beside(const wchar_t* leaf) {
    wchar_t path[32768]; DWORD n = GetModuleFileNameW(nullptr, path, 32768);
    if (!n || n >= 32768) return {};
    std::wstring s(path, n); auto slash = s.find_last_of(L"\\/");
    return slash == std::wstring::npos ? L"" : s.substr(0, slash + 1) + leaf;
}
std::wstring value(const std::wstring& ini, const wchar_t* key) {
    wchar_t s[128]; DWORD n = GetPrivateProfileStringW(L"session", key, L"", s, 128, ini.c_str());
    return n >= 127 ? L"" : std::wstring(s, n);
}
uint64_t number(const std::wstring& s) {
    if (s.empty() || s.size() > 18 || s.find_first_not_of(L"0123456789") != std::wstring::npos) return 0;
    return _wcstoui64(s.c_str(), nullptr, 10);
}
void finish(const char* reason) {
    bool ok = g_writer && g_writer->stop();
    logf("[recording] %s; completed=%d; outputs remain muted until exit", reason, ok);
    g_finished = true;
}
}

bool recordingMuted() { return g_muted; }

void recordingInit() {
    const auto request = beside(L"milestone_recording.request.ini");
    if (request.empty() || GetFileAttributesW(request.c_str()) == INVALID_FILE_ATTRIBUTES) return;
    // Any unconsumed diagnostic request fails closed for outputs, even if
    // malformed/expired. A normal launch has no request and remains unchanged.
    g_muted = true;
    const auto id = value(request, L"id");
    const auto expiry = number(value(request, L"expiresFileTimeUtc"));
    const auto seconds = number(value(request, L"seconds"));
    FILETIME now; GetSystemTimeAsFileTime(&now);
    ULARGE_INTEGER stamp; stamp.LowPart = now.dwLowDateTime; stamp.HighPart = now.dwHighDateTime;
    if (id.size() != 32 || id.find_first_not_of(L"0123456789abcdef") != std::wstring::npos ||
        expiry <= stamp.QuadPart || expiry - stamp.QuadPart > 10ull * 60 * 10000000 || seconds < 1 || seconds > 600) {
        g_finished = true; logf("[recording] invalid/expired request; remove request after closing; outputs muted"); return;
    }
    wchar_t local[32768]; DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", local, 32768);
    if (!n || n >= 32768) { g_finished = true; return; }
    g_result = std::wstring(local, n) + L"\\Dbce\\StagePlayback\\Gravel\\" + id;
    DWORD attrs = GetFileAttributesW(g_result.c_str());
    // Claim on the same volume first. Steam and LocalAppData need not share
    // a drive; MoveFile directly into the evidence directory would fail then.
    const auto claimed = request + L".claimed-" + id;
    if (attrs == INVALID_FILE_ATTRIBUTES || !(attrs & FILE_ATTRIBUTE_DIRECTORY) || (attrs & FILE_ATTRIBUTE_REPARSE_POINT) ||
        !MoveFileW(request.c_str(), claimed.c_str())) {
        g_finished = true; logf("[recording] request claim failed; outputs muted"); return;
    }
    if (!CopyFileW(claimed.c_str(), (g_result + L"\\claimed-request.ini").c_str(), TRUE) || !DeleteFileW(claimed.c_str())) {
        g_finished = true; logf("[recording] claim archive failed; outputs muted"); return;
    }
    g_seconds = double(seconds); g_id.assign(id.begin(), id.end());
    logf("[recording] claimed %s: %.0fs after first live UE observation, maximum 180s startup; outputs muted", g_id.c_str(), g_seconds);
}

void recordingPoll() {
    if (!g_muted || g_finished) return;
    try {
        if (!g_writer) {
            g_writer.reset(new dbce::session::Writer());
            dbce::session::Metadata m{"Gravel", "native-signals-1", "native-session-1", {}, {}};
            m.properties = {
                {"captureId", g_id}, {"kind", "sampled-signals"}, {"producer", "gravel-native-observer-v1"},
                {"sampling", "independent atomics on observation worker; not a physics tick"},
                {"forceQualification", "unqualified-request-summary; no effect lifecycle or ordered collision events"},
                {"playbackQualification", "none; no pose/stage/control ownership"}, {"outputs", "DirectInput sink guarded; UDP delivery suppressed"},
                {"availability", "observed bits and per-source age; cached values are not current physics"},
                {"inputMapping", "raw DirectInput reads normalized by proxy ini; not final game controls"},
                {"ffbGain", std::to_string(g_cfg.ffbGain)}, {"speedScale", std::to_string(g_cfg.speedScale)},
                {"fmodSpeedScale", std::to_string(g_cfg.fmodSpeedScale)},
                {"steerAxis", g_cfg.steer}, {"throttleAxis", g_cfg.throttle}, {"brakeAxis", g_cfg.brake},
                {"clutchAxis", g_cfg.clutch}, {"handbrakeAxis", g_cfg.handbrake}
            };
            m.channelUnits = {{"sample.interval", "s"}, {"ue.rpm", "rpm"}, {"ue.maxRpm", "rpm"},
                {"ue.speedRaw", "game units"}, {"ue.gearRaw", "game index"}, {"ffb.constantSummary", "normalized request proxy"},
                {"ffb.periodicSummary", "normalized request proxy"}, {"fmod.speed", "audio normalized"}};
            dbce::session::Limits limits; limits.seconds = 180 + g_seconds + 10;
            if (!g_writer->open(g_result + L"\\signals.jsonl", m, limits)) { g_finished = true; logf("[recording] exclusive file open failed"); return; }
            QueryPerformanceFrequency(&g_frequency); QueryPerformanceCounter(&g_begin);
        }
        LARGE_INTEGER now; QueryPerformanceCounter(&now);
        double elapsed = double(now.QuadPart - g_begin.QuadPart) / double(g_frequency.QuadPart);
        static double previous = -1;
        if (elapsed <= previous) return;
        bool live = g_ue4.live.load();
        if (live && g_drivingAt < 0) g_drivingAt = elapsed;
        ULONGLONG ms = GetTickCount64();
        dbce::session::Channels c{
            {"sample.interval", previous < 0 ? 0 : elapsed - previous},
            {"capture.vehicleSeen", g_drivingAt >= 0 ? 1 : 0}, {"capture.startupTimedOut", g_drivingAt < 0 && elapsed >= 180 ? 1 : 0},
            {"delivery.muted", 1}, {"ue.ready", g_ue4.ready ? 1 : 0}, {"ue.live", live ? 1 : 0},
            {"ue.available", double(g_ue4.available.load())}, {"input.reads", double(g_input.reads.load())},
            {"ffb.updates", double(g_ffb.updates.load())}, {"ffb.effects", double(g_ffb.effects.load())},
            {"ffb.tableOverflow", double(g_ffb.tableOverflow.load())}, {"ffb.mutedWrites", double(g_ffb.mutedWrites.load())},
            {"ffb.mutedStarts", double(g_ffb.mutedStarts.load())}, {"fmod.calls", double(g_fmod.calls.load())},
            {"fmod.impactSequence", double(g_fmod.impactSeq.load())}
        };
        auto age = [&](const char* name, ULONGLONG at) { if (at) c[name] = ms >= at ? (ms-at)/1000.0 : 0; };
        auto add = [&](const char* name, double v) { if (std::isfinite(v)) c[name] = v; else c[std::string(name)+".invalid"] = 1; };
        auto ia = g_input.observedAt.load(); c["input.observed"] = ia ? 1 : 0; age("input.age", ia);
        if (ia) {
            add("input.steer", g_input.steer); add("input.throttle", g_input.throttle); add("input.brake", g_input.brake);
            add("input.clutch", g_input.clutch); add("input.handbrake", g_input.handbrake);
            c["input.buttons"] = g_input.buttons.load();
            c["input.rawSteer"] = g_input.rawSteer.load(); c["input.rawThrottle"] = g_input.rawThr.load(); c["input.rawBrake"] = g_input.rawBrk.load();
        }
        auto fa = g_ffb.observedAt.load(); c["ffb.observed"] = fa ? 1 : 0; age("ffb.age", fa);
        const char* forceNames[] = {"ffb.constantSummary", "ffb.periodicSummary", "ffb.springSummary", "ffb.damperSummary"};
        std::atomic<float>* forceValues[] = {&g_ffb.constant, &g_ffb.periodic, &g_ffb.spring, &g_ffb.damper};
        for (int i=0; i<4; ++i) {
            auto at = g_ffb.summaryAt[i].load(); std::string key = forceNames[i];
            c[key+".observed"] = at ? 1 : 0;
            if (at) { add(forceNames[i], forceValues[i]->load()); age((key+".age").c_str(), at); }
        }
        const char* names[] = {"fmod.rpm", "fmod.speed", "fmod.load", "fmod.lateralSlip", "fmod.longitudinalSlip", "fmod.suspension", "fmod.braking", "fmod.impact", "fmod.impactSpeed"};
        std::atomic<float>* values[] = {&g_fmod.rpm, &g_fmod.speed, &g_fmod.load, &g_fmod.latSlip, &g_fmod.longSlip, &g_fmod.susp, &g_fmod.braking, &g_fmod.impact, &g_fmod.impactVel};
        for (int i=0; i<9; ++i) {
            auto at = g_fmod.observedAt[i].load(); std::string key = names[i];
            c[key+".observed"] = at ? 1 : 0;
            if (at) { add(names[i], values[i]->load()); age((key+".age").c_str(), at); }
        }
        auto available = g_ue4.available.load();
        if (live) {
            if (available & 1) add("ue.rpm", g_ue4.rpm);
            if (available & 2) add("ue.maxRpm", g_ue4.maxRpm);
            if (available & 4) add("ue.speedRaw", g_ue4.speed);
            if (available & 8) c["ue.gearRaw"] = g_ue4.gear.load();
        }
        previous = elapsed;
        if (!g_writer->sample(elapsed, c)) { g_finished = true; logf("[recording] stopped incomplete: %s", g_writer->reason().c_str()); return; }
        if (g_drivingAt >= 0 && elapsed - g_drivingAt >= g_seconds) finish("capture interval ended");
        else if (g_drivingAt < 0 && elapsed >= 180) finish("startup timed out; no driving qualification");
    } catch (...) {
        if (g_writer) g_writer->abandon();
        g_finished = true; logf("[recording] fault; incomplete file retained and outputs remain muted");
    }
}
