// StoreControlsText (dbce_store.h): a game's controls from the shared store as the shim's [Controls] profile
// (ini-controls-1), the C++ twin of shared/mod/DbceStoreControls.cs. Both give the same bytes for the same profile;
// test_store.exe and Dbce.Wheel.ModFile.Tests compare them with schema/fixtures/controls/drive-rally.store-controls.txt.
#include "dbce_store.h"
#include <windows.h>
#include <bcrypt.h>
#include <stdio.h>
#include <string.h>
#include <algorithm>

#pragma comment(lib, "bcrypt.lib")

namespace dbce { namespace panel {

namespace {

const char *const kActions[] = {
    "steer", "throttle", "brake", "clutch", "handbrake", "shiftUp", "shiftDown", "neutral", "reverse",
    "confirm", "back", "start", "select", "navUp", "navDown", "navLeft", "navRight",
    "camera", "lookBack", "reset", "horn", "modMenu", "muteFfb",
    "gear1", "gear2", "gear3", "gear4", "gear5", "gear6", "gear7", "gear8"};
const char *const kShifts[] = {"shiftUp", "shiftDown"};
const char *const kGates[] = {"neutral", "reverse", "gear1", "gear2", "gear3", "gear4", "gear5", "gear6", "gear7", "gear8"};
constexpr size_t kActionCount = sizeof kActions / sizeof kActions[0];

int ActionIndex(const std::string &action)
{
    for (size_t i = 0; i < kActionCount; ++i) if (action == kActions[i]) return (int)i;
    return -1;
}

bool Contains(const std::vector<std::string> &list, const std::string &value)
{
    return std::find(list.begin(), list.end(), value) != list.end();
}

// A GUID in any case, with or without braces, as "{lowercase}"; false for a malformed or all-zero GUID.
bool Guid(const std::string &text, std::string &out)
{
    std::string s = text;
    if (s.size() == 38 && s.front() == '{' && s.back() == '}') s = s.substr(1, 36);
    if (s.size() != 36) return false;
    bool nonZero = false;
    out = "{";
    for (size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        if (i == 8 || i == 13 || i == 18 || i == 23) { if (c != '-') return false; out += c; continue; }
        if (c >= 'A' && c <= 'F') c = (char)(c - 'A' + 'a');
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
        if (c != '0') nonZero = true;
        out += c;
    }
    out += "}";
    return nonZero;
}

bool HasControl(const std::string &s)
{
    for (unsigned char c : s) if (c < 0x20 || c == 0x7f) return true;
    return false;
}

std::string Lower(std::string s)
{
    for (char &c : s) if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
    return s;
}

// One binding in the ini-controls-1 grammar (schema 1: no calibration points); empty when it can't be written.
std::string Binding(const StoreBinding &b, std::string &key)
{
    const std::string kind = Lower(b.kind);
    if (kind != "axis" && kind != "button" && kind != "hat") return {};
    std::string device, product;
    if (!Guid(b.instanceGuid, device) || !Guid(b.productGuid, product) || HasControl(b.deviceName)) return {};
    std::string t = kind + " " + std::to_string(b.index);
    if (kind == "hat") t += " " + std::to_string(b.direction);
    t += " dev=" + device + " prod=" + product;
    if (kind == "axis") {
        if (b.direction != 1 && b.direction != -1) return {};
        t += " range=" + std::to_string(b.minimum) + ".." + std::to_string(b.maximum) + " rest=" + std::to_string(b.rest) +
             (b.direction == 1 ? " travel=+1" : " travel=-1");
        if (b.inverted) t += " inverted";
    }
    if (b.calibrated) t += " calibrated";
    if (!b.deviceName.empty()) {
        t += " name=\"";
        for (char c : b.deviceName) { if (c == '\\' || c == '"') t += '\\'; t += c; }
        t += "\"";
    }
    key = device + "|" + kind + "|" + std::to_string(b.index) + "|" + (kind == "hat" ? std::to_string(b.direction) : "-1");
    return t;
}

bool Fits(const std::string &value) { return !HasControl(value) && value.size() <= 128; }

std::string Sha256Hex(const std::string &data)
{
    unsigned char hash[32] = {};
    BCRYPT_ALG_HANDLE alg = nullptr;
    std::string hex;
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0) return hex;
    if (BCryptHash(alg, nullptr, 0, (PUCHAR)data.data(), (ULONG)data.size(), hash, sizeof hash) == 0) {
        char two[3];
        for (unsigned char c : hash) { sprintf_s(two, "%02X", c); hex += two; }
    }
    BCryptCloseAlgorithmProvider(alg, 0);
    return hex;
}

} // namespace

std::string StoreControlsText(const StoreProfile &profile, const std::vector<std::string> &delivered, std::string &why)
{
    std::string lines[kActionCount], keys[kActionCount];
    std::vector<std::string> skipped;
    for (const StoreBinding &b : profile.bindings) {
        const int i = ActionIndex(b.action);
        if (i < 0 || !Contains(delivered, b.action)) continue;
        std::string key, text = Binding(b, key);
        if (text.empty()) { skipped.push_back(b.action); continue; }
        lines[i] = text; keys[i] = key;
    }
    if (!Fits(profile.name) || !Fits(profile.transmission)) {
        why = "the profile name or transmission must fit 128 UTF-8 bytes without control characters";
        return {};
    }
    if (lines[ActionIndex("steer")].empty()) { why = "the profile " + profile.name + " has no steering this game can use"; return {}; }

    auto drop = [&](const char *const *actions, size_t count) { for (size_t k = 0; k < count; ++k) lines[ActionIndex(actions[k])].clear(); };
    const char *const *used = nullptr;
    size_t usedCount = 0;
    if (profile.transmission == "Sequential") { drop(kGates, 10); used = kShifts; usedCount = 2; }
    else if (profile.transmission == "H-pattern") {
        drop(kShifts, 2); used = kGates; usedCount = 10;
        bool gate = false;
        for (const char *g : kGates) if (strcmp(g, "neutral") != 0 && !lines[ActionIndex(g)].empty()) gate = true;
        if (!gate) drop(kGates, 10);   // neutral alone selects no gear
    } else { drop(kShifts, 2); drop(kGates, 10); }
    // One physical control selects one gear: two gear actions on one control write neither.
    bool clash = false;
    for (size_t a = 0; a < usedCount && !clash; ++a)
        for (size_t b = a + 1; b < usedCount; ++b) {
            const int ia = ActionIndex(used[a]), ib = ActionIndex(used[b]);
            if (!lines[ia].empty() && !lines[ib].empty() && keys[ia] == keys[ib]) { clash = true; break; }
        }
    if (clash) { drop(used, usedCount); skipped.push_back("gears (two share one control)"); }

    std::string identity = profile.id + "\n" + profile.name + "\n" + profile.transmission + "\n";
    std::string body;
    for (size_t i = 0; i < kActionCount; ++i)
        if (!lines[i].empty()) { identity += std::string(kActions[i]) + "=" + lines[i] + "\n"; body += std::string(kActions[i]) + " = " + lines[i] + "\r\n"; }
    why.clear();
    for (size_t i = 0; i < skipped.size(); ++i) why += (i ? ", " : "not written: ") + skipped[i];
    return "[Controls]\r\nSchema = 1\r\nProfile = " + profile.name + "\r\nRevision = " + Sha256Hex(identity) + "\r\nTransmission = " +
           profile.transmission + "\r\n" + body;
}

}} // namespace dbce::panel
