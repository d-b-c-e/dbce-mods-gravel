// Reader of the shared store (docs/STORE.md) for the panel core and native mods: profiles.json (profiles, the shared
// cockpit, which profile each game uses), written by Wheelkit only, and runtime\<game-id>.json, one game's runtime
// settings, written by that game's mod only. Files only gain fields; this reader ignores every field it does not know.
// It agrees with shared/mod/DbceStore.cs and installer/DbceStore.ps1: DumpStore and DumpRuntimeSettings write the
// canonical dump their tests compare with schema/fixtures/store/*.dump.txt. Implemented in dbce_panel_descriptor.cpp,
// beside the dbce-mod.json reader, with the same JSON parser.
#pragma once
#include <string>
#include <utility>
#include <vector>

namespace dbce { namespace panel {

// Screen layout and measurements (Wheelkit's ScreenSetup); a missing field has Wheelkit's default.
struct StoreScreens {
    int count = 3, widthPixels = 2560, heightPixels = 1440;
    bool separateMonitors = true;
    double diagonalInches = 32, aspectWidth = 16, aspectHeight = 9, eyeDistanceMm = 660, sideAngleDegrees = 70, bezelGapMm = 8, curveRadiusMm = 0;
    bool hasMeasuredWidth = false;     // false: derive the width from the diagonal
    double measuredWidthMm = 0;
};

struct StoreBinding {
    std::string action, instanceGuid, productGuid, deviceName, kind;
    int index = 0, direction = 0, rest = 0, minimum = 0, maximum = 65535;
    bool calibrated = false, inverted = false;
    bool hasCal[3] = {false, false, false};   // calLow, calCentre, calHigh (STD-037)
    int cal[3] = {0, 0, 0};
};

// One profile: a whole setup (devices, bindings, transmission, FFB strength), optionally its own screens.
struct StoreProfile {
    std::string id, name, transmission = "Automatic", wheel, shifter;
    int ffbStrength = 50;
    bool ffbOn = true;
    bool hasScreens = false;           // false: the profile uses the store's cockpit
    StoreScreens screens;
    std::vector<StoreBinding> bindings;
};

struct Store {
    int schema = 0;
    std::string activeId;
    bool hasCockpit = false;
    StoreScreens cockpit;
    std::vector<StoreProfile> profiles;
    std::vector<std::pair<std::string, std::string>> games;   // game id -> profile Id, in file order
};

// The profile a game uses: its assignment when that names a profile, else ActiveId's, else the first; with the
// profile's own screens, else the cockpit, else none (screens is null: the mod keeps its own screen settings).
struct StoreChoice {
    const StoreProfile *profile = nullptr;
    const StoreScreens *screens = nullptr;
    const char *screensFrom = "none";  // "profile", "cockpit" or "none"
};

struct RuntimeValue {
    enum Type { Number, Bool, String } type = Number;
    std::string key, text;
    double number = 0;
    bool flag = false;
};

// runtime\<game-id>.json: the values one game's F6 panel changes. Only that game's mod writes it.
struct RuntimeSettings {
    int schema = 0;
    std::string game, profile;
    std::vector<RuntimeValue> values;
    const RuntimeValue *get(const char *key) const
    {
        for (const RuntimeValue &v : values) if (v.key == key) return &v;
        return nullptr;
    }
};

bool ParseStore(const char *json, size_t length, Store &out, std::string &error);
bool LoadStore(const wchar_t *path, Store &out, std::string &error);
StoreChoice StoreFor(const Store &store, const std::string &gameId);
std::string DumpStore(const Store &store);

bool ParseRuntimeSettings(const char *json, size_t length, RuntimeSettings &out, std::string &error);
bool LoadRuntimeSettings(const wchar_t *path, RuntimeSettings &out, std::string &error);
std::string DumpRuntimeSettings(const RuntimeSettings &settings);

// The shim's [Controls] profile (ini-controls-1) for a game's store profile, limited to the actions the build delivers;
// the C++ twin of shared/mod/DbceStoreControls.cs, byte for byte. Empty, with the reason in why, when the profile has no
// steering the build can use or its name does not fit the shim. Implemented in dbce_store_controls.cpp (links bcrypt).
std::string StoreControlsText(const StoreProfile &profile, const std::vector<std::string> &delivered, std::string &why);

// %LOCALAPPDATA%\Wheelkit\store, and the two files in it; empty when LOCALAPPDATA is unset.
std::wstring StoreFolder();
std::wstring StoreProfilesPath();
std::wstring StoreRuntimePath(const wchar_t *gameId);

}} // namespace dbce::panel
