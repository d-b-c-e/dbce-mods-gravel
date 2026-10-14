// Controls contract (STD-033, docs/controls-contract.md), schemas 1 and 2: the logical binding of a rig profile's action
// to one DirectInput object, its one-line text form, and normalization. Pure C++17 (no Windows headers), header-only, so
// every native mod vendors the same file and the offline tests run anywhere. Wheelkit's C# runs the same shared vectors
// (controls-vectors.txt). Schema 2 adds one axis attribute, `cal=` (measured endpoints); a reader opts into it
// (parse(..., 2), parseSection(..., 2)), and by default everything reads exactly as schema 1.
#pragma once
#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

namespace dbce { namespace controls {

enum class Kind { None, Axis, Button, Hat };
enum class Shape { Unknown, Centred, Pedal, Digital };

struct Binding {
    Kind kind = Kind::None;
    int index = -1;
    int angle = -1;                 // hat only, 0..35999
    std::string dev, prod;          // "{xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx}", lowercase
    long min = 0, max = 0, rest = 0;
    int travel = 0;                 // +1 / -1 (axes)
    bool hasRange = false, hasRest = false, inverted = false, calibrated = false;   // calibrated: carried metadata only
    std::vector<long> cal;          // schema 2 `cal=`, raw in `range`: steer {lo, centre, hi}, pedal {released, full}; empty: none
    std::string name;
};

inline const char *const kActions[] = {
    "steer", "throttle", "brake", "clutch", "handbrake",
    "shiftUp", "shiftDown", "neutral", "reverse",
    "confirm", "back", "start", "select", "navUp", "navDown", "navLeft", "navRight",
    "camera", "lookBack", "reset", "horn", "modMenu", "muteFfb",
    "gear1", "gear2", "gear3", "gear4", "gear5", "gear6", "gear7", "gear8",
};

inline Shape shapeOf(const std::string &action)
{
    if (action == "steer") return Shape::Centred;
    if (action == "throttle" || action == "brake" || action == "clutch" || action == "handbrake") return Shape::Pedal;
    for (const char *a : kActions) if (action == a) return Shape::Digital;
    return Shape::Unknown;
}

inline const char *kindName(Kind k) { return k == Kind::Axis ? "axis" : k == Kind::Button ? "button" : k == Kind::Hat ? "hat" : "none"; }

namespace detail {
inline bool isHex(char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }
inline char lower(char c) { return (c >= 'A' && c <= 'Z') ? char(c - 'A' + 'a') : c; }
// "{8-4-4-4-12}" -> lowercase copy, or empty when malformed.
inline std::string guid(const std::string &s)
{
    static const int groups[] = {8, 4, 4, 4, 12};
    if (s.size() != 38 || s.front() != '{' || s.back() != '}') return {};
    std::string out = "{";
    size_t p = 1;
    for (int g = 0; g < 5; ++g) {
        for (int i = 0; i < groups[g]; ++i, ++p) { if (!isHex(s[p])) return {}; out += lower(s[p]); }
        if (g < 4) { if (s[p] != '-') return {}; out += '-'; ++p; }
    }
    return out + "}";
}
// A decimal integer that fits DirectInput's 32-bit LONG (-2147483648..2147483647) on every platform. Anything else,
// including values strtol would silently clamp where long is 32 bits (Windows), is refused.
inline bool integer(const std::string &s, long &v)
{
    if (s.empty() || s.size() > 11) return false;
    size_t i = s[0] == '-' ? 1 : 0;
    if (i == s.size()) return false;
    for (size_t k = i; k < s.size(); ++k) if (s[k] < '0' || s[k] > '9') return false;
    errno = 0;
    char *end = nullptr;
    const long long x = std::strtoll(s.c_str(), &end, 10);
    if (errno == ERANGE || end != s.c_str() + s.size() || x < INT32_MIN || x > INT32_MAX) return false;
    v = (long)x;
    return true;
}
} // namespace detail

// Text form -> binding. On failure returns false and a stable reason code (the vectors compare it):
// bad-kind bad-index bad-angle bad-attribute bad-guid bad-range bad-rest bad-travel bad-cal duplicate missing-dev
// missing-prod missing-range missing-rest missing-travel axis-only bad-name. `schema` 1 ignores `cal=` like any unknown
// attribute; 2 reads it (two or three integers joined by "..": the shape is checked by fits()).
inline bool parse(const std::string &text, Binding &out, std::string &reason, int schema = 1)
{
    out = Binding{};
    // A binding is one line: no control character other than tab anywhere (C0, DEL, or a UTF-8 encoded C1), so a device
    // name cannot introduce another INI key. The managed parser's rule (char.IsControl), with its reason code.
    for (size_t k = 0; k < text.size(); ++k) {
        const unsigned char c = (unsigned char)text[k], next = k + 1 < text.size() ? (unsigned char)text[k + 1] : 0;
        if ((c < 0x20 && c != '\t') || c == 0x7F || (c == 0xC2 && next >= 0x80 && next <= 0x9F)) { reason = "bad-name"; return false; }
    }
    std::vector<std::string> tok;
    for (size_t i = 0; i < text.size();) {
        while (i < text.size() && (text[i] == ' ' || text[i] == '\t')) ++i;
        if (i >= text.size()) break;
        size_t j = i;
        if (text.compare(i, 6, "name=\"") == 0) {   // quoted: spaces allowed, \" and \\ escapes
            j = i + 6;
            std::string v;
            bool closed = false;
            while (j < text.size()) {
                char c = text[j];
                if (c == '\\' && j + 1 < text.size() && (text[j + 1] == '"' || text[j + 1] == '\\')) { v += text[j + 1]; j += 2; continue; }
                if (c == '"') { closed = true; ++j; break; }
                v += c; ++j;
            }
            if (!closed) { reason = "bad-name"; return false; }
            tok.push_back("name=\"" + v);       // marker: value follows the quote
            i = j;
            continue;
        }
        while (j < text.size() && text[j] != ' ' && text[j] != '\t') ++j;
        tok.push_back(text.substr(i, j - i));
        i = j;
    }
    if (tok.empty()) { reason = "bad-kind"; return false; }
    if (tok[0] == "axis") out.kind = Kind::Axis;
    else if (tok[0] == "button") out.kind = Kind::Button;
    else if (tok[0] == "hat") out.kind = Kind::Hat;
    else { reason = "bad-kind"; return false; }
    long v = 0;
    if (tok.size() < 2 || !detail::integer(tok[1], v) || v < 0) { reason = "bad-index"; return false; }
    const long maxIndex = out.kind == Kind::Axis ? 7 : out.kind == Kind::Hat ? 3 : 127;
    if (v > maxIndex) { reason = "bad-index"; return false; }
    out.index = (int)v;
    size_t k = 2;
    if (out.kind == Kind::Hat) {
        if (tok.size() < 3 || !detail::integer(tok[2], v) || v < 0 || v > 35999) { reason = "bad-angle"; return false; }
        out.angle = (int)v;
        k = 3;
    }
    bool seenDev = false, seenProd = false, seenRange = false, seenRest = false, seenTravel = false, seenInv = false, seenCal = false, seenName = false;
    bool seenCalValues = false;
    auto dup = [&](bool &seen) { if (seen) return true; seen = true; return false; };
    for (; k < tok.size(); ++k) {
        const std::string &t = tok[k];
        if (t.compare(0, 6, "name=\"") == 0) { if (dup(seenName)) { reason = "duplicate"; return false; } out.name = t.substr(6); continue; }
        if (t == "inverted") { if (dup(seenInv)) { reason = "duplicate"; return false; } out.inverted = true; continue; }
        if (t == "calibrated") { if (dup(seenCal)) { reason = "duplicate"; return false; } out.calibrated = true; continue; }
        size_t eq = t.find('=');
        if (eq == std::string::npos || eq == 0) { reason = "bad-attribute"; return false; }
        std::string key = t.substr(0, eq), val = t.substr(eq + 1);
        if (key == "dev" || key == "prod") {
            if (dup(key == "dev" ? seenDev : seenProd)) { reason = "duplicate"; return false; }
            std::string g = detail::guid(val);
            if (g.empty()) { reason = "bad-guid"; return false; }
            (key == "dev" ? out.dev : out.prod) = g;
        } else if (key == "range") {
            if (dup(seenRange)) { reason = "duplicate"; return false; }
            size_t dots = val.find("..");
            long a = 0, b = 0;
            if (dots == std::string::npos || !detail::integer(val.substr(0, dots), a) || !detail::integer(val.substr(dots + 2), b) || b <= a) { reason = "bad-range"; return false; }
            out.min = a; out.max = b; out.hasRange = true;
        } else if (key == "rest") {
            if (dup(seenRest)) { reason = "duplicate"; return false; }
            if (!detail::integer(val, v)) { reason = "bad-rest"; return false; }
            out.rest = v; out.hasRest = true;
        } else if (key == "travel") {
            if (dup(seenTravel)) { reason = "duplicate"; return false; }
            if (val == "+1") out.travel = 1; else if (val == "-1") out.travel = -1; else { reason = "bad-travel"; return false; }
        } else if (key == "cal" && schema >= 2) {
            if (dup(seenCalValues)) { reason = "duplicate"; return false; }
            std::vector<long> points;
            for (size_t from = 0;;) {
                const size_t dots = val.find("..", from);
                if (!detail::integer(val.substr(from, dots == std::string::npos ? std::string::npos : dots - from), v)) { reason = "bad-cal"; return false; }
                points.push_back(v);
                if (dots == std::string::npos) break;
                from = dots + 2;
            }
            if (points.size() != 2 && points.size() != 3) { reason = "bad-cal"; return false; }
            out.cal = points;
        }
        // any other key=value (and `cal=` in schema 1): ignored (forward compatibility)
    }
    if (!seenDev) { reason = "missing-dev"; return false; }
    if (!seenProd) { reason = "missing-prod"; return false; }
    if (out.kind == Kind::Axis) {
        if (!seenRange) { reason = "missing-range"; return false; }
        if (!seenRest) { reason = "missing-rest"; return false; }
        if (!seenTravel) { reason = "missing-travel"; return false; }
        if (out.rest < out.min || out.rest > out.max) { reason = "bad-rest"; return false; }
    } else if (seenRange || seenRest || seenTravel || seenInv || seenCalValues) { reason = "axis-only"; return false; }
    reason.clear();
    return true;
}

// Binding -> canonical text (fixed attribute order, lowercase GUIDs).
inline std::string format(const Binding &b)
{
    std::string s = kindName(b.kind);
    s += " " + std::to_string(b.index);
    if (b.kind == Kind::Hat) s += " " + std::to_string(b.angle);
    s += " dev=" + b.dev + " prod=" + b.prod;
    if (b.kind == Kind::Axis) {
        s += " range=" + std::to_string(b.min) + ".." + std::to_string(b.max) + " rest=" + std::to_string(b.rest);
        s += b.travel > 0 ? " travel=+1" : " travel=-1";
        if (!b.cal.empty()) {
            s += " cal=";
            for (size_t i = 0; i < b.cal.size(); ++i) s += (i ? ".." : "") + std::to_string(b.cal[i]);
        }
        if (b.inverted) s += " inverted";
    }
    if (b.calibrated) s += " calibrated";
    if (!b.name.empty()) {
        s += " name=\"";
        for (char c : b.name) { if (c == '"' || c == '\\') s += '\\'; s += c; }
        s += "\"";
    }
    return s;
}

// Schema 2: is `cal` the action's shape and measured, as a fraction of the declared span S = max - min (unit-independent:
// at 0..65535 these are 512 and 8192 counts)? Steer: min <= lo < centre < hi <= max, each side >= S/128, the sweep >= S/8.
// Pedal: both points in range, |full - released| >= S/8. Compared in double, exact for any 32-bit range.
inline bool calibrationFits(Shape shape, const Binding &b)
{
    const double lo = double(b.min), hi = double(b.max), span = hi - lo;
    for (long p : b.cal) if (double(p) < lo || double(p) > hi) return false;
    if (shape == Shape::Centred) {
        if (b.cal.size() != 3) return false;
        const double l = double(b.cal[0]), c = double(b.cal[1]), h = double(b.cal[2]);
        return l < c && c < h && (c - l) * 128.0 >= span && (h - c) * 128.0 >= span && (h - l) * 8.0 >= span;
    }
    if (b.cal.size() != 2) return false;
    return std::fabs(double(b.cal[1]) - double(b.cal[0])) * 8.0 >= span;
}

// Does the binding's kind fit the shape? "" when it does, else a reason code: unknown-action, wrong-kind, rest-at-full,
// bad-calibration. The shape form serves destinations outside the 31 (a game's declared extras); the action form below
// is exactly this with the action's own shape.
inline std::string fits(Shape shape, const Binding &b)
{
    switch (shape) {
    case Shape::Unknown: return "unknown-action";
    case Shape::Centred:
        if (b.kind != Kind::Axis) return "wrong-kind";
        return b.cal.empty() || calibrationFits(Shape::Centred, b) ? "" : "bad-calibration";
    case Shape::Pedal:
        if (b.kind == Kind::Button) return "";
        if (b.kind != Kind::Axis) return "wrong-kind";
        // A valid `cal` alone decides (its released and full replace rest and the travel end, which it never reads).
        if (!b.cal.empty()) return calibrationFits(Shape::Pedal, b) ? "" : "bad-calibration";
        return (b.travel > 0 ? b.max : b.min) == b.rest ? "rest-at-full" : "";
    case Shape::Digital: return (b.kind == Kind::Button || b.kind == Kind::Hat) ? "" : "wrong-kind";
    }
    return "unknown-action";
}
inline std::string fits(const std::string &action, const Binding &b) { return fits(shapeOf(action), b); }

inline double clampd(double v, double lo, double hi) { return v < lo ? lo : v > hi ? hi : v; }

// A raw DirectInput button byte -> the shared boolean sample (down when bit 0x80 is set). Readers that already export
// booleans pass 1 for down straight to normalize().
inline bool diButtonDown(long byte) { return (byte & 0x80) != 0; }

// Raw sample (axes: in the binding's range; buttons: the shared boolean, 1 down; hats: the POV value) -> normalized:
// steer -1..1 (right positive), pedal 0..1, digital 0 or 1. Assumes fits(action, b) == "". A schema-2 `cal` that does not
// fit the action (calibrationFits) is refused explicitly: the value is neutral (0: steer centred, pedal released), never
// read through out-of-range points nor through the schema-1 fields (mufa 62480 F2, sana 62527). The managed twin throws
// unless Fits is "".
inline double normalize(Shape shape, const Binding &b, long raw)
{
    if (b.kind == Kind::Button) return raw != 0 ? 1.0 : 0.0;
    if (b.kind == Kind::Hat) {
        if (raw < 0 || (raw & 0xFFFF) == 0xFFFF) return 0.0;
        long d = std::labs((raw % 36000) - b.angle);
        if (d > 18000) d = 36000 - d;
        return d <= 4500 ? 1.0 : 0.0;
    }
    // Every operand widened to double before adding or subtracting: legal ranges reach -2147483648..2147483647, whose
    // sum, span and rest-to-end distance overflow a 32-bit long.
    const double lo = double(b.min), hi = double(b.max);
    const bool cal = !b.cal.empty();
    if (cal && !calibrationFits(shape, b)) return 0.0;   // refused: neutral
    if (shape == Shape::Centred) {
        double n;
        if (cal) {   // schema 2: two-sided around the measured centre, saturating beyond the measured locks
            const double l = double(b.cal[0]), c = double(b.cal[1]), h = double(b.cal[2]), v = double(raw);
            n = clampd(v >= c ? (v - c) / (h - c) : (v - c) / (c - l), -1.0, 1.0);
        } else {
            const double centre = (lo + hi) / 2.0, half = (hi - lo) / 2.0;
            n = clampd((double(raw) - centre) / half, -1.0, 1.0);
        }
        return b.inverted ? -n : n;
    }
    // Schema 2 `cal`: released and full are measured (their order gives the direction); otherwise rest and the travel end.
    const double full = cal ? double(b.cal[1]) : b.travel > 0 ? hi : lo, rest = cal ? double(b.cal[0]) : double(b.rest);
    const double n = clampd((double(raw) - rest) / (full - rest), 0.0, 1.0);
    return b.inverted ? 1.0 - n : n;   // the player's explicit inversion, once, after the captured travel or calibration
}
inline double normalize(const std::string &action, const Binding &b, long raw) { return normalize(shapeOf(action), b, raw); }

// Normalized -> raw sample (the exact inverse; rounded half away from zero, clamped to the range; buttons the shared
// boolean, which a DirectInput writer delivers as 0x80). Test injection only.
inline long denormalize(const std::string &action, const Binding &b, double n)
{
    if (b.kind == Kind::Button) return n >= 0.5 ? 1 : 0;
    if (b.kind == Kind::Hat) return n >= 0.5 ? b.angle : -1;
    double v;
    const double lo = double(b.min), hi = double(b.max);   // widened first, as in normalize()
    const bool cal = !b.cal.empty();
    if (cal && !calibrationFits(shapeOf(action), b)) return b.rest;   // refused, as in normalize(): the binding at rest
    if (shapeOf(action) == Shape::Centred) {
        n = clampd(b.inverted ? -n : n, -1.0, 1.0);
        if (cal) {   // inverse within the measured locks (a raw beyond them normalized to +-1 maps to the lock)
            const double l = double(b.cal[0]), c = double(b.cal[1]), h = double(b.cal[2]);
            v = n >= 0 ? c + n * (h - c) : c + n * (c - l);
        } else v = (lo + hi) / 2.0 + n * ((hi - lo) / 2.0);
    } else {
        const double full = cal ? double(b.cal[1]) : b.travel > 0 ? hi : lo, rest = cal ? double(b.cal[0]) : double(b.rest);
        n = clampd(n, 0.0, 1.0);
        v = rest + (b.inverted ? 1.0 - n : n) * (full - rest);
    }
    if (v <= lo) return b.min;   // clamped in double, so the rounding below never leaves the range
    if (v >= hi) return b.max;
    const long long r = std::llround(v);
    return r < b.min ? b.min : r > b.max ? b.max : (long)r;
}

// Rescale a raw sample between ranges (the binding's capture range and the range a game set on the device).
inline long rescale(long v, long fromMin, long fromMax, long toMin, long toMax)
{
    // Every operand widened before subtracting (a LONG_MIN..LONG_MAX range must not overflow); rounded half away from
    // zero, then clamped to the target range before the cast.
    if (fromMax <= fromMin || toMax < toMin) return toMin;
    const double t = (double(v) - double(fromMin)) / (double(fromMax) - double(fromMin));
    const double r = std::round(double(toMin) + t * (double(toMax) - double(toMin)));
    if (r <= double(toMin)) return toMin;
    if (r >= double(toMax)) return toMax;
    return (long)r;
}

// The product GUID's Data1 (PID << 16 | VID), as DirectInput games key their device tables. 0 when malformed.
inline uint32_t productId(const Binding &b)
{
    if (b.prod.size() != 38) return 0;
    return (uint32_t)std::strtoul(b.prod.substr(1, 8).c_str(), nullptr, 16);
}

// ---- the [Controls] INI section (storage adapters ini-controls-1, and ini-controls-2 when the reader opts in) --------
struct Entry { std::string action; Binding binding; };
struct Section {
    int schema = 0;
    std::string profile, revision, transmission;
    std::vector<Entry> bound;                                    // valid and fitting
    std::vector<std::string> unbound;                            // explicitly empty
    std::vector<std::pair<std::string, std::string>> invalid;    // action, reason
    std::string error;                                           // schema problem: nothing applies
};

inline std::string trim(const std::string &s)
{
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r' || s[a] == '\n')) ++a;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r' || s[b - 1] == '\n')) --b;
    return s.substr(a, b - a);
}

// Lines "key = value" (as GetPrivateProfileSection returns them, or a file's section body). Comments ';' / '#' skipped.
// `reads`: the highest schema this reader applies (1, the default, exactly as before schema 2 existed; or 2). A section
// whose Schema is above it applies nothing, with the reason in `error`.
inline Section parseSection(const std::vector<std::string> &lines, int reads = 1)
{
    Section s;
    bool haveSchema = false;
    auto entry = [&](const std::string &raw, std::string &key, std::string &val) {
        std::string line = trim(raw);
        if (line.empty() || line[0] == ';' || line[0] == '#') return false;
        size_t eq = line.find('=');
        if (eq == std::string::npos) return false;
        key = trim(line.substr(0, eq)); val = trim(line.substr(eq + 1));
        return true;
    };
    for (const std::string &raw : lines) {   // the Schema first (the last one wins), so every binding reads in it
        std::string key, val;
        if (entry(raw, key, val) && key == "Schema") { long v = 0; haveSchema = detail::integer(val, v); s.schema = haveSchema ? (int)v : -1; }
    }
    const int bindingSchema = haveSchema && s.schema == 2 && reads >= 2 ? 2 : 1;
    for (const std::string &raw : lines) {
        std::string key, val;
        if (!entry(raw, key, val)) continue;
        if (key == "Schema") continue;
        if (key == "Profile") { s.profile = val; continue; }
        if (key == "Revision") { s.revision = val; continue; }
        if (key == "Transmission") { s.transmission = val; continue; }
        if (shapeOf(key) == Shape::Unknown) { s.invalid.push_back({key, "unknown-action"}); continue; }
        if (val.empty()) { s.unbound.push_back(key); continue; }
        Binding b;
        std::string why;
        if (!parse(val, b, why, bindingSchema)) { s.invalid.push_back({key, why}); continue; }
        why = fits(key, b);
        if (!why.empty()) { s.invalid.push_back({key, why}); continue; }
        bool dupe = false;
        for (auto &e : s.bound) if (e.action == key) dupe = true;
        if (dupe) { s.invalid.push_back({key, "duplicate"}); continue; }
        s.bound.push_back({key, b});
    }
    if (!haveSchema) s.error = "no Schema";
    else if (s.schema < 1 || s.schema > (reads >= 2 ? 2 : 1))
        s.error = "Schema " + std::to_string(s.schema) + " is not supported (this mod reads " + (reads >= 2 ? "1 or 2" : "1") + ")";
    if (!s.error.empty()) s.bound.clear();
    return s;
}

inline const Binding *find(const Section &s, const std::string &action)
{
    for (auto &e : s.bound) if (e.action == action) return &e.binding;
    return nullptr;
}

}} // namespace dbce::controls
