// Reader of the whole dbce-mod.json for the panel core (XC-1, docs/DBCE-MOD-JSON.md). The settings section is the
// UI-1 descriptor (dbce_panel_descriptor.h), read by the same strict checker; the other sections are read with the
// documented defaults, so the values match the installer's (installer/DbceModFile.ps1) and Wheelkit's
// (shared/mod/DbceModFile.cs). Implemented in dbce_panel_descriptor.cpp; DumpModFile writes the canonical dump.
#pragma once
#include "dbce_panel_descriptor.h"
#include <utility>

namespace dbce { namespace panel {

struct ModFromTo { std::string from, to; };
struct ModKnownBuild { std::string file, sha256, label; };
struct ModCondition {
    std::vector<std::string> actions, settingsValues;
    std::string transmission, settingsKey, settingsDefault, note;
};
struct ModGameControl { std::string id, kind, label, group; };
struct ModCameraStart { std::string camera, start, file, section, key, realistic, game; };
struct ModProbe { std::string id, type, label; };
struct ModTestState { std::string state, field, is, text, picture; };
struct ModRouteStep { std::string key, input, waitState, waitText; long timeout = 0; };
struct ModFile {
    std::string schema, id, name, version, folder, repository, logFile, logStart;
    double steamAppId = 0;
    std::vector<std::string> exe;
    // package
    std::string kind, uiHost = "game";
    bool hasLoader = false, strict = false;
    std::string loaderName, loaderVersion, loaderBundle;
    std::vector<ModFromTo> payload, optional, seedConfig;
    std::vector<std::string> conflicts;
    std::vector<ModKnownBuild> knownBuilds;
    // settings (UI-1)
    bool hasSettings = false;
    Descriptor settings;
    // controls
    bool hasControls = false, hasOptIn = false, optInAppliesAfterRestart = false;
    std::string controlsStore, controlsProfile, controlsSettings, optInSection, optInKey, optInValue, optInDefault;
    std::vector<std::string> delivered;
    std::vector<std::pair<std::string, std::string>> unsupported;
    std::vector<ModCondition> conditions;
    std::vector<ModGameControl> gameControls;
    std::vector<ModCameraStart> cameras;
    // selfTest
    bool hasSelfTest = false;
    std::string selfTestFolder;
    std::vector<std::string> selfTestSteps;
    std::vector<ModProbe> probes;
    // test (the pilot's descriptor, 07-testing-framework.md)
    bool hasTest = false, hasLaunch = false, hasTestSave = false, hasEntry = false, hasQuit = false;
    long launchSteamAppId = 0;
    std::string launchExe, testSaveFrom, testSaveTo, entryCommand, entryRoute, quitCommand, quitKey, quitInput;
    std::vector<std::string> launchArgs, protect;
    std::vector<ModTestState> states;
    std::vector<ModRouteStep> route;
    // installed
    bool hasInstalled = false, loaderInstalledByUs = false;
    std::string installedVersion, commit, installedUtc, backups;
    std::vector<ModFromTo> files;                       // from = path, to = sha256
    std::vector<std::pair<std::string, bool>> flags;
};

bool ParseModFile(const char *json, size_t length, ModFile &out, std::string &error);
bool LoadModFile(const wchar_t *path, ModFile &out, std::string &error);
std::string DumpModFile(const ModFile &m);

}} // namespace dbce::panel
