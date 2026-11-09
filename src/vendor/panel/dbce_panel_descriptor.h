// The F6 settings descriptor (UI-1; docs/architecture/2026-11/00-overview.md section 3, 04-cross-cutting.md F.5,
// 05-ux-simplification.md section 1): one JSON settings block per mod, from which the shared panel draws its fixed
// standard pages. A game only declares values inside the standard controls; there are no per-game widgets.
//
//   { "settings": {                                   (or the same object without the "settings" wrapper)
//       "product": "GRID (2019) Wheel Mod", "version": "0.2.24", "modPage": "https://...", "keys": "F6 settings · ...",
//       "config": "dbce-grid2019.ini",              (optional: the mod's settings ini)
//       "pages": { "ffb": [ { "id": "ffb.enabled", "kind": "toggle", "label": "Force feedback", "default": true,
//                             "help": "F8 turns it off at any time.", "level": "simple", "apply": "live" }, ... ],
//                  "controls": [...], "display": [...], "cameras": [...], "telemetry": [...], "help": [...] } } }
//
// Row fields: id (unique; letters, digits, '.', '_', '-'), kind (toggle, choice, percent, slider, number, binding,
// action, status), label, help (at most 12 words), level (simple, advanced = under "More options", developer = only
// on the Developer tab behind the hidden developer switch), apply (live or next-start), choices (choice), min, max,
// default, unit (slider, number), decimals (number), text (status and binding: the first text shown).
// Unknown pages, kinds, levels and fields are rejected, so a typo never silently drops a row. Each page has at most
// five simple controls and two simple status lines (05-ux-simplification.md 1.0).
//
// The types are header-only so dbce_panel.cpp can draw a descriptor without linking the parser; a mod that reads a
// descriptor also compiles dbce_panel_descriptor.cpp.
#pragma once
#include <stddef.h>
#include <string>
#include <vector>

namespace dbce { namespace panel {

enum class RowKind { Toggle, Choice, Percent, Slider, Number, Binding, Action, Status };
enum class RowLevel { Simple, Advanced, Developer };
// The standard pages in tab order. Developer is not a descriptor page: it lists every developer-level row.
enum class StdPage { Controls, Ffb, Display, Cameras, Telemetry, Help, Developer, Count };

struct Row {
    std::string id, label, help, unit, text;
    StdPage page = StdPage::Help;
    RowKind kind = RowKind::Status;
    RowLevel level = RowLevel::Simple;
    bool nextStart = false;                 // "apply": "next-start"
    std::vector<std::string> choices;       // choice
    double minValue = 0, maxValue = 0;      // percent (0-100 unless given), slider, number
    double defaultValue = 0;                // toggle 0/1, choice index, percent/slider/number value
    int decimals = 0;                       // number
    // dbce-mod.json additions (docs/DBCE-MOD-JSON.md): what the value means across games, and where the mod keeps it.
    std::string role;
    bool hasStore = false;
    std::string storeFile, storeSection, storeKey, storeOn, storeOff;   // storeFile defaults to Descriptor::config
    double storeScale = 1;                  // stored value = shown value x scale
};

struct Descriptor {
    std::string product, version, modPage, keys;
    std::string config;                     // the mod's ini, relative to the descriptor file
    std::string format = "ini";             // ini or xml (dbce-mod.json)
    std::vector<Row> rows;                  // in draw order within each page
};

// Strict parse; on failure returns false with a one-line reason ("row 'x': unknown kind 'y'") and leaves out empty.
bool ParseDescriptor(const char *json, size_t length, Descriptor &out, std::string &error);
bool LoadDescriptor(const wchar_t *path, Descriptor &out, std::string &error);

inline const char *StdPageId(StdPage p)
{
    static const char *const k[] = {"controls", "ffb", "display", "cameras", "telemetry", "help", "developer"};
    return (int)p >= 0 && p < StdPage::Count ? k[(int)p] : "";
}
inline const char *StdPageName(StdPage p)
{
    static const char *const k[] = {"Controls", "Force feedback", "Display", "Cameras", "Telemetry", "Help", "Developer"};
    return (int)p >= 0 && p < StdPage::Count ? k[(int)p] : "";
}
inline bool RowIsControl(RowKind k) { return k != RowKind::Status; }

}} // namespace dbce::panel
