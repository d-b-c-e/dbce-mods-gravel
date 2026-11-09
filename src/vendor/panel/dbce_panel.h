// Shared F6 settings panel for native mods (STD-004, STD-017, STD-022, STD-030, STD-053).
//
// The panel owns the frame (title, View: Simple/Advanced, Close, Stop FFB, flat page tabs in the standard order) and
// a set of standard widgets; the mod draws each page's body with those widgets and keeps its settings in its own
// canonical config (the ini Wheelkit also reads). Drawing uses Dear ImGui (native/third_party/imgui); a renderer
// overlay (dbce_overlay_d3d11.h) calls Frame() once per presented frame. Input arrives through dbce_panel_input.h.
#pragma once
#include <windows.h>
#include <string>
#include "dbce_panel_descriptor.h"

namespace dbce { namespace panel {

// The five core pages in their fixed order (UX-01). A page the build does not offer is hidden (STD-017); Help stays.
enum Page { PAGE_CONTROLS, PAGE_FFB, PAGE_CAMERAS, PAGE_TELEMETRY, PAGE_HELP, PAGE_COUNT };

enum class Tone { Normal, Good, Warning, Error };

struct Config {
    const char *title = "Wheel settings";
    const char *modName = "";                   // shown in Help, e.g. "Rush Rally mod 0.3.0"
    const char *iniPath = nullptr;              // the mod's canonical config: [Panel] View, Scale and Page are kept there
    bool pages[PAGE_COUNT] = {false, false, false, false, true};
    void (*drawPage)(Page page, bool advanced) = nullptr;   // the mod draws each body with the widgets below
    // Force feedback the mod can stop (UX-05): the header's Stop FFB command and F8 call stopFfb, which turns the mod's
    // saved FFB setting Off; only the FFB page's Off/On turns it back on. ffbOn reports the current setting.
    bool (*ffbOn)() = nullptr;                  // null: no Stop FFB in this mod
    void (*stopFfb)() = nullptr;
    // F7 steps the session-only correction comparison (triple-screen toolkit camera-framing-and-comparison-policy.md):
    // the mod changes mode and shows its label. Called on the window thread. Null: F7 goes to the game.
    void (*compareKey)() = nullptr;
    // Camera keys (STD-042/048, e.g. numpad + / - / 5): while the panel is closed, every key other than F6/F7/F8 is
    // offered here first (down, up and repeats, with the virtual-key code); return true to consume it so the game does
    // not see it. Called on the window thread. Never called while the panel is open. Null: keys go to the game.
    bool (*cameraKey)(int vk, bool down, bool repeat) = nullptr;
    void (*log)(const char *line) = nullptr;
    // Unattended checks only (no OS input): a file whose text is "open <Page> [simple|advanced]" or "close" is applied
    // when it changes (polled twice a second by the overlay); a file last written before the process started is ignored.
    // Null: off.
    const char *devFile = nullptr;
    // The self-test runner (native/selftest/dbce_selftest.hpp, docs/SELFTEST.md), such as [] { runner.Tick(); }:
    // PollDev calls it on every presented frame of the game's swap chain, before the devFile check. Null: no self-test.
    void (*selfTestTick)() = nullptr;
    // STD-023: whether the three views are on, for the frame-rate readout and its log line. Null: not reported.
    bool (*triplesOn)() = nullptr;
    // The display in the mod's own words for that readout (for example "Surround span, one wide view"); when set, and
    // not empty, it replaces the triples on/off suffix, so a planar wide view is never called triples. Called on the
    // present thread. Null: triplesOn as before.
    const char *(*displayText)() = nullptr;
    // Help repository links, drawn as small text hyperlinks. Descriptors must live
    // as long as the panel; opening a link requires an explicit click. Null/empty
    // leaves that link out. Issue reporting stays in the mod's own Help action.
    const char *modRepositoryUrl = nullptr;
    const char *wheelkitRepositoryUrl = nullptr;
    // UI-1: draw the standard pages (Controls, Force feedback, Display, Cameras, Telemetry, Help, and Developer behind
    // the developer switch) from a settings descriptor (dbce_panel_descriptor.h) instead of pages/drawPage. The
    // descriptor must outlive the panel. The mod pushes values with SetToggle/SetChoice/SetNumber/SetText and reads the
    // player's changes and actions with PollEvent, both on its own thread. Null: the hand-drawn pages above.
    const Descriptor *descriptor = nullptr;
};

// Called once by the mod before the overlay starts (not from DllMain).
void Configure(const Config &config);

// True while the panel is open: the mod suppresses driving input and camera shortcuts (UX-03).
bool IsOpen();
void SetOpen(bool open);
void Toggle();
// Internal, passive status only. Text is copied (up to 255 UTF-8 bytes); null/empty clears.
// Safe from a host worker thread. No recorder, hotkeys, persistence or output is enabled.
// Hosts use SessionStatusPresentation wording and own terminal-state expiry. Saved must mean sealed.
enum class StatusTone { Recording, Busy, Success, Problem, Playback };
void SetStatusLine(const char *utf8Text, StatusTone tone);
void ClearStatusLine();
bool StatusLineActive();
// STD-024: the on-screen frame-rate counter ([Panel] ShowFrameRate) is on; the overlay draws closed frames for it too.
bool FrameCounterOn();
// F8 (dbce_panel_input): Stop FFB when the mod offers it.
bool StopFfbAvailable();
void StopFfbFromKey();
bool CompareAvailable();   // Config::compareKey set (F7)
void CompareFromKey();
bool CameraKeyFromMessage(int vk, bool down, bool repeat);   // Config::cameraKey; false when unset

// ---- descriptor pages (Config::descriptor) --------------------------------------------------------------------
// Values for descriptor rows, safe from any thread; false when no row has that id or the kind does not match.
// A value the mod sets is not reported back by PollEvent.
bool SetToggle(const char *id, bool value);
bool SetChoice(const char *id, int index);
bool SetNumber(const char *id, double value);                               // percent, slider, number (clamped)
bool SetText(const char *id, const char *utf8Text, Tone tone = Tone::Normal); // status line, binding's current input
bool SetRowVisible(const char *id, bool visible);                           // hide a row that does not apply now
// A player change or command, oldest first. Toggle/Choice/Number changes carry the new value; Action is a pressed
// action row; Capture asks the mod to capture a binding (the row shows its help as the prompt until SetText or Esc);
// Clear is the binding's x.
struct PanelEvent {
    enum class Type { Changed, Action, Capture, Clear } type = Type::Changed;
    std::string id;
    bool toggle = false;
    int choice = 0;
    double number = 0;
};
bool PollEvent(PanelEvent &out);
// Esc while a binding row waits for input: stop waiting (the old binding stays) and keep the panel open.
bool CancelBindingCapture();
// Toasts and on-screen labels (camera name, the F7 view, notices): at most two lines, at the bottom of the centre
// screen for `ms` (3 s by default), panel open or closed. Safe from any thread; a new toast replaces the old one.
void ShowToast(const char *line1, const char *line2 = nullptr, unsigned ms = 3000);
bool ToastActive();
// The hidden developer switch (05-ux-simplification.md 1.7): %LOCALAPPDATA%\dbce\developer.on, read at Configure.
// Developer rows keep their values when it is off; only the Developer tab is hidden.
bool DeveloperOn();
void SetDeveloper(bool on);                 // tests and self-tests
// The row's rectangle in window pixels in the last drawn frame (x0, y0, x1, y1); false when it was not drawn.
bool RowRect(const char *id, float rect[4]);
// The descriptor page the panel shows, and whether More options is open; for the dev channel and render checks.
StdPage DescriptorPage();
bool DescriptorMoreOptions();               // More options is open on the page shown
void ShowDescriptorPage(StdPage page, bool moreOptions);
bool DescriptorPageShown(StdPage page);     // the tab is there (empty pages are hidden; Help always)
// UI-2 navigation of the descriptor pages, the same for the keyboard and the wheel: while a descriptor panel is open,
// the arrows, Enter and Space arrive here from the input hook, and a mod forwards its wheel D-pad and buttons.
// Up/Down move the focus between the tab row and the page's controls (More options included); Left/Right change the
// focused control (Off/On, the previous or next choice, a slider step) or, on the tab row, the page; Select flips a
// toggle, steps a choice, presses an action, starts a binding capture or opens More options. Back is Esc (it cancels a
// binding capture first, then closes); Open and Close open and close the panel. Safe from any thread: Open, Close and
// Back apply at once, the others on the next drawn frame. False: no descriptor, or the panel is closed (Open aside).
enum class Nav { Up, Down, Left, Right, Select, Back, Open, Close };
bool Navigate(Nav command);
// The input hook, on the window thread: true when vk is one of the navigation keys above and was used.
bool NavigateFromKey(int vk, bool down, bool repeat);
// The focused control's id ("" for the tab row; "panel.more" and the like for the panel's own rows), as last drawn.
void NavFocus(char *out, size_t size);

// What an overlay draws on a closed frame: the panel, the status line, the frame-rate counter or a toast.
bool DrawWanted();

// Overlay side, once per presented frame while the panel is open (an ImGui context must be current):
//   if (Prepare(w, h)) { BuildFonts(); <invalidate the renderer's font texture> }
//   <renderer NewFrame>; DrainInput(w, h); ImGui::NewFrame(); Draw(w, h); ImGui::Render(); <render draw data>
// Prepare returns true when the UI scale changed and the font atlas must be rebuilt (also on the first frame).
bool Prepare(int width, int height);
// The dev channel (Config::devFile) and the self-test tick (Config::selfTestTick); cheap enough to call every frame.
void PollDev();
// The dev channel's "open" for a self-test host's OpenPage: opens the panel on a page by id or name ("" keeps the page
// shown), with view simple or advanced (hand-drawn pages) or more (More options, descriptor pages). False, and the
// panel left as it was, when this build does not show that page. Not saved, like the dev channel.
bool OpenForCheck(const char *page, const char *view);
// Every presented frame of the game's swap chain, open or closed (STD-023): ten-second frame-rate windows, shown on the
// Cameras page (Help when there is none) and logged every 30 s.
void NoteFrame();
void BuildFonts();
void Draw(int width, int height);
float Scale();

// ---- standard widgets (UX-02/03) ------------------------------------------------------------------------------
// Label column on the left, value/control on the right, help underneath in secondary text.
bool OffOn(const char *label, bool *value, const char *help = nullptr);
bool Choice(const char *label, const char *const *options, int count, int *value, const char *help = nullptr);
bool Percent(const char *label, int *value, int minValue, int maxValue, int defaultValue, const char *help = nullptr);
// A slider with its own unit, e.g. "%d\u00b0" for degrees (Percent is this with "%d%%").
bool Slider(const char *label, int *value, int minValue, int maxValue, int defaultValue, const char *format, const char *help = nullptr);
void Status(const char *label, const char *value, Tone tone = Tone::Normal);
void Heading(const char *text);
void Text(const char *text);                    // short wrapped help in secondary text
bool Command(const char *label);                // compact command, right-aligned on its own row
void BeginGroup(const char *id);                // a visibly bounded group (UX-03-G)
void EndGroup();
// A short "Saved" or error note beside the header controls (UX-08).
void NoteSaved(bool ok, const char *detail = nullptr);

// Panel preferences (view, scale, page) kept in [Panel] of the mod's ini.
bool Advanced();

}} // namespace dbce::panel
