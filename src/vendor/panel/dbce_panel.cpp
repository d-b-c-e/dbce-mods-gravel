// Shared F6 settings panel: frame, navigation and standard widgets (see dbce_panel.h).
#include "dbce_panel.h"
#include "../settings/dbce_ini_single_key_win.hpp"
#include "dbce_panel_input.h"
#include "dbce_frame_stats.hpp"
#include "../third_party/imgui/imgui.h"
#include "../third_party/imgui/imgui_internal.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <algorithm>
#include <cmath>
#include <deque>
#include <string>
#include <vector>

namespace dbce { namespace panel {

static Config g_cfg;
static volatile LONG g_open;
static volatile LONG g_statusActive;
static SRWLOCK g_statusLock = SRWLOCK_INIT;
static char g_statusText[256];
static StatusTone g_statusTone = StatusTone::Recording;
static Page g_page = PAGE_HELP;
static bool g_pageChanged = true;
static bool g_advanced;
static int g_userScale = 100;          // percent, Help > UI scale
static float g_scale = 1.0f;            // effective, from the window height and the user scale
static float g_fontScale = 0.0f;        // scale the font atlas was built for
static ImFont *g_body, *g_small, *g_heading;
static char g_note[160];
static bool g_noteOk = true;
static ULONGLONG g_noteAt;
static FrameWindow g_frames;            // STD-023, present thread only
static LARGE_INTEGER g_frameFreq, g_frameLast;
static ULONGLONG g_frameLoggedAt;
static double g_smoothMs;              // STD-024 on-screen counter: smoothed frame time, present thread
static bool g_showFps;                  // STD-024: [Panel] ShowFrameRate, off by default

// UI-1 descriptor pages (Config::descriptor). Row values and the event queue are guarded by g_rowLock: the mod sets
// values and polls events on its own thread, the render thread draws under the same lock.
struct RowState {
    const Row *row = nullptr;
    bool visible = true, toggle = false, capturing = false, changedNextStart = false;
    int choice = 0;
    double number = 0;
    std::string text;
    Tone tone = Tone::Normal;
    std::vector<const char *> choicePtrs;
    float rect[4] = {};
    unsigned drawnFrame = 0;
};
struct CoreRect { const char *id; float rect[4]; unsigned drawnFrame; };
static std::vector<RowState> g_rows;
static std::deque<PanelEvent> g_events;
static SRWLOCK g_rowLock = SRWLOCK_INIT;
static CoreRect g_coreRects[12];
static StdPage g_dpage = StdPage::Help;
static bool g_more;                     // More options open on this page; closed whenever F6 opens or the page changes
static bool g_developer;
static unsigned g_drawFrame = 1;
static SRWLOCK g_toastLock = SRWLOCK_INIT;
static char g_toast[2][128];
static volatile LONG64 g_toastUntil;
// Configure may replace the descriptor while an overlay draws (dbce_ui configures after its hooks are in): Draw holds
// this shared, Configure exclusive.
static SRWLOCK g_cfgLock = SRWLOCK_INIT;
// UI-2 navigation. Commands are queued under g_rowLock and applied by the render thread at the start of a drawn frame,
// against the focus list of the frame before: 0 is the tab row, n the nth control drawn on the page.
enum CoreItem { CORE_NONE, CORE_MORE, CORE_SHOW_FPS, CORE_TEXT_SIZE };
struct NavItem { RowState *row; CoreItem core; };
static std::deque<Nav> g_navQueue;
static std::vector<NavItem> g_navItems, g_navBuild;   // last frame's list, and the one being drawn (render thread)
static int g_focus;                                  // render thread
static char g_focusId[64];                           // under g_rowLock
static volatile LONG g_textInput;                    // ImGui is editing text: the arrows and Enter are ImGui's
static void pushEvent(PanelEvent::Type type, const RowState &r);

static const char *const kPageNames[PAGE_COUNT] = {"Controls", "FFB", "Cameras", "Telemetry", "Help"};

// ---- colours (UX-03: opaque dark neutral panel, one accent for selection) ------------------------------------
static const ImVec4 kBg(0.118f, 0.129f, 0.149f, 1.0f);        // #1E2126
static const ImVec4 kGroup(0.149f, 0.165f, 0.188f, 1.0f);     // #262A30
static const ImVec4 kBorder(0.271f, 0.294f, 0.333f, 1.0f);    // #454B55
static const ImVec4 kText(0.910f, 0.918f, 0.929f, 1.0f);      // #E8EAED
static const ImVec4 kSecondary(0.659f, 0.682f, 0.718f, 1.0f); // #A8AEB7
static const ImVec4 kButton(0.212f, 0.231f, 0.263f, 1.0f);    // #363B43
static const ImVec4 kButtonHover(0.255f, 0.278f, 0.318f, 1.0f);
static const ImVec4 kAccent(0.239f, 0.545f, 0.992f, 1.0f);    // #3D8BFD
static const ImVec4 kAccentHover(0.353f, 0.620f, 1.0f, 1.0f);
static const ImVec4 kGood(0.298f, 0.686f, 0.478f, 1.0f);
static const ImVec4 kWarning(0.878f, 0.627f, 0.188f, 1.0f);
static const ImVec4 kError(0.878f, 0.314f, 0.314f, 1.0f);

static void logf(const char *fmt, ...)
{
    if (!g_cfg.log) return;
    char b[256];
    va_list ap;
    va_start(ap, fmt);
    _vsnprintf_s(b, sizeof b, _TRUNCATE, fmt, ap);
    va_end(ap);
    g_cfg.log(b);
}

// ---- preferences in [Panel] of the mod's ini ------------------------------------------------------------------
// A descriptor page by its id ("ffb"), its tab name ("Force feedback") or the hand-drawn panel's name ("FFB").
static bool descriptorPageFromName(const char *name, StdPage *out)
{
    for (int p = 0; p < (int)StdPage::Count; ++p)
        if (!_stricmp(name, StdPageId((StdPage)p)) || !_stricmp(name, StdPageName((StdPage)p))) { *out = (StdPage)p; return true; }
    if (!_stricmp(name, "FFB")) { *out = StdPage::Ffb; return true; }
    return false;
}
static const char *descriptorPageSaveName(StdPage p) { return p == StdPage::Ffb ? "FFB" : StdPageName(p); }
static void setupDescriptor();
static void loadPrefs()
{
    if (!g_cfg.iniPath) return;
    char v[32] = "";
    GetPrivateProfileStringA("Panel", "View", "Simple", v, sizeof v, g_cfg.iniPath);
    g_advanced = _stricmp(v, "Advanced") == 0;
    g_userScale = (int)GetPrivateProfileIntA("Panel", "Scale", 100, g_cfg.iniPath);
    g_userScale = (std::min)(200, (std::max)(75, g_userScale));
    {   // "1" as written here; "On" (GRID 0.2.3-0.2.5 saved settings) and "true" (Wheelkit bool rows) read the same
        char fps[16] = "";
        GetPrivateProfileStringA("Panel", "ShowFrameRate", "0", fps, sizeof fps, g_cfg.iniPath);
        g_showFps = !_stricmp(fps, "On") || !_stricmp(fps, "true") || atoi(fps) != 0;
    }
    GetPrivateProfileStringA("Panel", "Page", "", v, sizeof v, g_cfg.iniPath);
    for (int p = 0; p < PAGE_COUNT; ++p)   // by name: a saved page keeps its meaning if pages are added (UX-01)
        if (!_stricmp(v, kPageNames[p]) && g_cfg.pages[p]) g_page = (Page)p;
    if (g_cfg.descriptor) {
        StdPage p;
        if (descriptorPageFromName(v, &p) && DescriptorPageShown(p)) g_dpage = p;
    }
}
static void savePref(const char *key, const char *value)
{
    if (!g_cfg.iniPath) return;
    if (!dbce::settings::saveKey(g_cfg.iniPath, "Panel", key, value)) {
        NoteSaved(false, "Settings could not be saved");
        logf("panel: could not save [Panel] %s (%lu)", key, GetLastError());
    }
}

void Configure(const Config &config)
{
    AcquireSRWLockExclusive(&g_cfgLock);
    g_cfg = config;
    g_cfg.pages[PAGE_HELP] = true;   // Help always stays (STD-017)
    g_page = PAGE_HELP;
    for (int p = 0; p < PAGE_COUNT; ++p) if (g_cfg.pages[p]) { g_page = (Page)p; break; }   // first offered page
    setupDescriptor();
    loadPrefs();
    ReleaseSRWLockExclusive(&g_cfgLock);
}

bool IsOpen() { return g_open != 0; }
static volatile LONG g_navReset;          // set when the panel opens: the focus goes back to the tab row
void SetOpen(bool open)
{
    const LONG was = InterlockedExchange(&g_open, open ? 1 : 0);
    if (open) { g_pageChanged = true; g_more = false; }
    if (open && !was) InterlockedExchange(&g_navReset, 1);
}
void Toggle() { SetOpen(!IsOpen()); }
bool StatusLineActive() { return InterlockedCompareExchange(&g_statusActive, 0, 0) != 0; }
bool FrameCounterOn() { return g_showFps; }
bool CompareAvailable() { return g_cfg.compareKey != nullptr; }
void CompareFromKey() { if (g_cfg.compareKey) g_cfg.compareKey(); }
bool CameraKeyFromMessage(int vk, bool down, bool repeat) { return g_cfg.cameraKey && g_cfg.cameraKey(vk, down, repeat); }
void SetStatusLine(const char *text, StatusTone tone)
{
    size_t length = 0;
    if (text) {
        while (length < sizeof(g_statusText) - 1 && text[length]) ++length;
        // A capped UTF-8 string must not end in part of a code point.
        if (length == sizeof(g_statusText) - 1)
            while (length && (static_cast<unsigned char>(text[length]) & 0xc0) == 0x80) --length;
    }
    AcquireSRWLockExclusive(&g_statusLock);
    if (length) memcpy(g_statusText, text, length);
    g_statusText[length] = 0;
    g_statusTone = tone;
    InterlockedExchange(&g_statusActive, length ? 1 : 0);
    ReleaseSRWLockExclusive(&g_statusLock);
}
void ClearStatusLine() { SetStatusLine(nullptr, StatusTone::Recording); }
bool StopFfbAvailable() { return g_cfg.ffbOn && g_cfg.stopFfb; }
void StopFfbFromKey()
{
    if (!StopFfbAvailable() || !g_cfg.ffbOn()) return;
    NoteSaved(true, "FFB stopped");
    g_cfg.stopFfb();   // callback persistence feedback, especially failure, takes precedence
    logf("panel: F8 stopped force feedback");
}
bool Advanced() { return g_advanced; }
float Scale() { return g_scale; }

void NoteSaved(bool ok, const char *detail)
{
    g_noteOk = ok;
    strcpy_s(g_note, ok ? (detail ? detail : "Saved") : (detail ? detail : "Not saved"));
    g_noteAt = GetTickCount64();
}

// ---- scale and fonts -----------------------------------------------------------------------------------------
// Logical units: 16 body, 14 secondary, 20 headings; 8-unit grid, 16 padding, 32 minimum row height (UX-03), all scaled
// together. The base follows the window height (1080 = 1.0, never below 1.0 so 720p stays readable) times UI scale.
bool Prepare(int width, int height)
{
    (void)width;
    float s = (std::max)(1.0f, (float)height / 1080.0f) * (float)g_userScale / 100.0f;
    s = (std::min)(4.0f, (std::max)(0.75f, s));
    g_scale = s;
    return std::fabs(g_fontScale - s) > 0.01f;
}

void BuildFonts()
{
    ImGuiIO &io = ImGui::GetIO();
    io.Fonts->Clear();
    const float s = g_scale;
    ImFontConfig fc;
    fc.OversampleH = 2;
    char win[MAX_PATH], body[MAX_PATH], bold[MAX_PATH];
    GetWindowsDirectoryA(win, MAX_PATH);
    sprintf_s(body, "%s\\Fonts\\segoeui.ttf", win);
    sprintf_s(bold, "%s\\Fonts\\seguisb.ttf", win);
    bool haveBody = GetFileAttributesA(body) != INVALID_FILE_ATTRIBUTES;
    bool haveBold = GetFileAttributesA(bold) != INVALID_FILE_ATTRIBUTES;
    if (haveBody) {
        g_body = io.Fonts->AddFontFromFileTTF(body, 16.0f * s, &fc);
        g_small = io.Fonts->AddFontFromFileTTF(body, 14.0f * s, &fc);
        g_heading = io.Fonts->AddFontFromFileTTF(haveBold ? bold : body, 20.0f * s, &fc);
    }
    if (!g_body) {   // no Segoe UI: the built-in font, scaled
        ImFontConfig d;
        d.SizePixels = 16.0f * s;
        g_body = io.Fonts->AddFontDefault(&d);
        d.SizePixels = 14.0f * s; g_small = io.Fonts->AddFontDefault(&d);
        d.SizePixels = 20.0f * s; g_heading = io.Fonts->AddFontDefault(&d);
    }
    io.FontDefault = g_body;
    g_fontScale = s;

    ImGuiStyle &st = ImGui::GetStyle();
    st = ImGuiStyle();
    st.WindowPadding = ImVec2(16 * s, 16 * s);
    st.FramePadding = ImVec2(12 * s, 8 * s);          // 16 px text + 2 x 8 = 32 px rows
    st.ItemSpacing = ImVec2(8 * s, 8 * s);
    st.ItemInnerSpacing = ImVec2(8 * s, 8 * s);
    st.CellPadding = ImVec2(8 * s, 4 * s);
    st.ScrollbarSize = 16 * s;
    st.GrabMinSize = 16 * s;
    st.WindowRounding = 6 * s;
    st.ChildRounding = 4 * s;
    st.FrameRounding = 4 * s;
    st.GrabRounding = 4 * s;
    st.WindowBorderSize = 1;
    st.ChildBorderSize = 1;
    st.FrameBorderSize = 0;
    ImVec4 *c = st.Colors;
    c[ImGuiCol_Text] = kText;
    c[ImGuiCol_TextDisabled] = kSecondary;
    c[ImGuiCol_WindowBg] = kBg;
    c[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_PopupBg] = kGroup;
    c[ImGuiCol_Border] = kBorder;
    c[ImGuiCol_FrameBg] = kButton;
    c[ImGuiCol_FrameBgHovered] = kButtonHover;
    c[ImGuiCol_FrameBgActive] = kButtonHover;
    c[ImGuiCol_Button] = kButton;
    c[ImGuiCol_ButtonHovered] = kButtonHover;
    c[ImGuiCol_ButtonActive] = kAccent;
    c[ImGuiCol_SliderGrab] = kAccent;
    c[ImGuiCol_SliderGrabActive] = kAccentHover;
    c[ImGuiCol_CheckMark] = kAccent;
    c[ImGuiCol_Header] = kButton;
    c[ImGuiCol_HeaderHovered] = kButtonHover;
    c[ImGuiCol_HeaderActive] = kAccent;
    c[ImGuiCol_Separator] = kBorder;
    c[ImGuiCol_ScrollbarBg] = kBg;
    c[ImGuiCol_ScrollbarGrab] = kButton;
    c[ImGuiCol_ScrollbarGrabHovered] = kButtonHover;
    c[ImGuiCol_ScrollbarGrabActive] = kAccent;
    c[ImGuiCol_NavHighlight] = kAccentHover;           // keyboard focus, distinct from the selected tab underline
}

// ---- layout helpers ------------------------------------------------------------------------------------------
static float labelWidth() { return (std::max)(200.0f * g_scale, ImGui::GetContentRegionAvail().x * 0.38f); }

static void secondaryWrapped(const char *text)
{
    ImGui::PushFont(g_small);
    ImGui::PushStyleColor(ImGuiCol_Text, kSecondary);
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted(text);
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
    ImGui::PopFont();
}

static float buttonWidth(const char *label)
{
    return ImGui::CalcTextSize(label, nullptr, true).x + 2 * ImGui::GetStyle().FramePadding.x;
}

// A segmented control: short mutually exclusive values, the selected one filled with the accent (UX-02/03-C).
static bool segments(const char *id, const char *const *options, int count, int *value)
{
    bool changed = false;
    ImGui::PushID(id);
    for (int i = 0; i < count; ++i) {
        if (i) ImGui::SameLine(0, 2 * g_scale);
        const bool on = *value == i;
        ImGui::PushStyleColor(ImGuiCol_Button, on ? kAccent : kButton);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, on ? kAccentHover : kButtonHover);
        ImGui::PushID(i);
        if (ImGui::Button(options[i]) && !on) { *value = i; changed = true; }
        ImGui::PopID();
        ImGui::PopStyleColor(2);
    }
    ImGui::PopID();
    return changed;
}

static void rowLabel(const char *label)
{
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::SameLine(labelWidth());
}

// ---- widgets ------------------------------------------------------------------------------------------------
bool OffOn(const char *label, bool *value, const char *help)
{
    static const char *const kOffOn[2] = {"Off", "On"};
    int v = *value ? 1 : 0;
    rowLabel(label);
    bool changed = segments(label, kOffOn, 2, &v);
    if (changed) *value = v == 1;
    if (help) secondaryWrapped(help);
    return changed;
}

bool Choice(const char *label, const char *const *options, int count, int *value, const char *help)
{
    rowLabel(label);
    bool changed;
    float need = 0;
    for (int i = 0; i < count; ++i) need += buttonWidth(options[i]) + 2 * g_scale;
    if (count <= 4 && need <= ImGui::GetContentRegionAvail().x) changed = segments(label, options, count, value);
    else {
        ImGui::SetNextItemWidth((std::min)(ImGui::GetContentRegionAvail().x, 360 * g_scale));
        changed = ImGui::Combo((std::string("##") + label).c_str(), value, options, count);
    }
    if (help) secondaryWrapped(help);
    return changed;
}

bool Percent(const char *label, int *value, int minValue, int maxValue, int defaultValue, const char *help)
{
    return Slider(label, value, minValue, maxValue, defaultValue, "%d%%", help);
}

bool Slider(const char *label, int *value, int minValue, int maxValue, int defaultValue, const char *format, const char *help)
{
    rowLabel(label);
    const char *reset = "Default";
    float avail = ImGui::GetContentRegionAvail().x - buttonWidth(reset) - ImGui::GetStyle().ItemSpacing.x;
    ImGui::SetNextItemWidth((std::max)(120 * g_scale, avail));
    bool changed = ImGui::SliderInt((std::string("##") + label).c_str(), value, minValue, maxValue, format, ImGuiSliderFlags_AlwaysClamp);
    ImGui::SameLine();
    ImGui::PushID(label);
    ImGui::BeginDisabled(*value == defaultValue);
    if (ImGui::Button(reset)) { *value = defaultValue; changed = true; }
    ImGui::EndDisabled();
    ImGui::PopID();
    if (help) secondaryWrapped(help);
    return changed;
}

void Status(const char *label, const char *value, Tone tone)
{
    rowLabel(label);
    const ImVec4 col = tone == Tone::Good ? kGood : tone == Tone::Warning ? kWarning : tone == Tone::Error ? kError : kText;
    ImGui::PushStyleColor(ImGuiCol_Text, col);
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted(value);
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
}

void Heading(const char *text)
{
    ImGui::Dummy(ImVec2(0, 4 * g_scale));
    ImGui::PushFont(g_heading);
    ImGui::TextUnformatted(text);
    ImGui::PopFont();
}

void Text(const char *text) { secondaryWrapped(text); }

bool Command(const char *label)
{
    float w = buttonWidth(label);
    float x = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - w;
    if (x > ImGui::GetCursorPosX()) ImGui::SetCursorPosX(x);
    return ImGui::Button(label);
}

void BeginGroup(const char *id)
{
    ImGui::PushStyleColor(ImGuiCol_ChildBg, kGroup);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16 * g_scale, 12 * g_scale));
    ImGui::BeginChild(id, ImVec2(0, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
}
void EndGroup()
{
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
    ImGui::Dummy(ImVec2(0, 8 * g_scale));   // 16 between groups (8 here + item spacing) versus 8 within (UX-03-G)
}

// ---- frame rate (STD-023) -----------------------------------------------------------------------------------
static void frameLine(char *out, size_t size, const FrameSummary &s)
{
    const char *display = g_cfg.displayText ? g_cfg.displayText() : nullptr;
    const int triples = g_cfg.triplesOn ? (g_cfg.triplesOn() ? 1 : 0) : -1;
    FormatFrameLine(out, size, s, display, triples);
}
void NoteFrame()
{
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    if (!g_frameFreq.QuadPart) QueryPerformanceFrequency(&g_frameFreq);
    if (g_frameLast.QuadPart) {
        const double ms = (double)(now.QuadPart - g_frameLast.QuadPart) * 1000.0 / (double)g_frameFreq.QuadPart;
        if (ms > 0 && ms <= FrameWindow::kGapMs) g_smoothMs = g_smoothMs > 0 ? g_smoothMs * 0.95 + ms * 0.05 : ms;
        if (g_frames.Add(ms)) {
            const ULONGLONG t = GetTickCount64();
            if (!g_frameLoggedAt || t - g_frameLoggedAt >= 30000) {
                char line[160];
                frameLine(line, sizeof line, g_frames.Last());
                logf("frame rate (10 s): %s", line);
                g_frameLoggedAt = t;
            }
        }
    }
    g_frameLast = now;
}
static void drawFrameRate()
{
    BeginGroup("frame-rate");
    const FrameSummary &s = g_frames.Last();
    char line[160] = "Measuring (10 s)";
    if (s.valid) frameLine(line, sizeof line, s);
    Status("Frame rate", line);
    if (OffOn("Show frame rate on screen", &g_showFps, "A small counter in the top-right corner, also while this panel is closed."))
        savePref("ShowFrameRate", g_showFps ? "1" : "0");
    if (Advanced()) Text("The last ten seconds, measured at the game's present; the same line goes to the log every 30 s.");
    EndGroup();
}

// ---- Help page (always present) -----------------------------------------------------------------------------
static void drawHelp(bool advanced)
{
    BeginGroup("help-version");
    Status("Version", g_cfg.modName && *g_cfg.modName ? g_cfg.modName : "unknown");
    Status("Keys", g_cfg.ffbOn ? "F6 opens and closes this panel. F8 stops force feedback. Esc closes." : "F6 opens and closes this panel. Esc closes.");
    EndGroup();
    BeginGroup("help-scale");
    int scale = g_userScale;
    if (Percent("UI scale", &scale, 75, 200, 100, "Text and controls grow or shrink together; the panel follows the window height.")) {
        g_userScale = scale;
        char v[16]; sprintf_s(v, "%d", scale);
        savePref("Scale", v);
    }
    EndGroup();
    // Host-owned issue/report actions precede the unobtrusive repository links.
    if (g_cfg.drawPage) g_cfg.drawPage(PAGE_HELP, advanced);
    const bool modLink = g_cfg.modRepositoryUrl && *g_cfg.modRepositoryUrl;
    const bool wheelkitLink = g_cfg.wheelkitRepositoryUrl && *g_cfg.wheelkitRepositoryUrl;
    if (modLink || wheelkitLink) {
        BeginGroup("help-repositories");
        ImGui::PushFont(g_small);
        if (modLink) ImGui::TextLinkOpenURL("Mod on GitHub", g_cfg.modRepositoryUrl);
        if (wheelkitLink) ImGui::TextLinkOpenURL("Wheelkit on GitHub", g_cfg.wheelkitRepositoryUrl);
        ImGui::PopFont();
        EndGroup();
    }
    if (!g_cfg.pages[PAGE_CAMERAS]) drawFrameRate();
}

// ---- descriptor pages (UI-1; Config::descriptor) -------------------------------------------------------------
// The fixed page set drawn from data: per page, its status lines and at most five simple controls, then one
// "More options" row that opens the advanced rows. Developer rows appear only on the Developer tab, behind the hidden
// developer switch. The panel owns a few standard rows itself: the frame-rate status and Show frame rate (Display),
// the version and keys lines, Text size and the mod page link (Help).
static const char kDot[] = "\xC2\xB7";        // U+00B7, in the panel font's Latin-1 range
static const char kTimes[] = "\xC3\x97";      // U+00D7

static bool developerFileOn()
{
    wchar_t path[MAX_PATH];
    DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", path, MAX_PATH);
    if (!n || n >= MAX_PATH - 32) return false;
    wcscat_s(path, L"\\dbce\\developer.on");
    return GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES;
}

static void setupDescriptor()
{
    AcquireSRWLockExclusive(&g_rowLock);
    g_rows.clear();
    g_events.clear();
    g_navQueue.clear();
    g_navItems.clear();
    g_navBuild.clear();
    g_focus = 0;
    g_focusId[0] = 0;
    for (CoreRect &c : g_coreRects) c = CoreRect();
    if (g_cfg.descriptor) {
        g_rows.resize(g_cfg.descriptor->rows.size());
        for (size_t i = 0; i < g_rows.size(); ++i) {
            RowState &r = g_rows[i];
            r.row = &g_cfg.descriptor->rows[i];
            r.toggle = r.row->defaultValue != 0;
            r.choice = (int)r.row->defaultValue;
            r.number = r.row->defaultValue;
            r.text = r.row->text;
            for (const std::string &c : r.row->choices) r.choicePtrs.push_back(c.c_str());
        }
    }
    ReleaseSRWLockExclusive(&g_rowLock);
    g_developer = g_cfg.descriptor && developerFileOn();
    g_more = false;
    g_dpage = StdPage::Help;
    if (g_cfg.descriptor)
        for (int p = 0; p < (int)StdPage::Count; ++p) if (DescriptorPageShown((StdPage)p)) { g_dpage = (StdPage)p; break; }
}

static RowState *findRow(const char *id)
{
    if (!id) return nullptr;
    for (RowState &r : g_rows) if (r.row->id == id) return &r;
    return nullptr;
}

template <class F> static bool withRow(const char *id, F f)
{
    AcquireSRWLockExclusive(&g_rowLock);
    RowState *r = findRow(id);
    const bool ok = r && f(*r);
    ReleaseSRWLockExclusive(&g_rowLock);
    return ok;
}

bool SetToggle(const char *id, bool value)
{
    return withRow(id, [&](RowState &r) { if (r.row->kind != RowKind::Toggle) return false; r.toggle = value; return true; });
}
bool SetChoice(const char *id, int index)
{
    return withRow(id, [&](RowState &r) {
        if (r.row->kind != RowKind::Choice || index < 0 || index >= (int)r.row->choices.size()) return false;
        r.choice = index;
        return true;
    });
}
bool SetNumber(const char *id, double value)
{
    return withRow(id, [&](RowState &r) {
        const RowKind k = r.row->kind;
        if (k != RowKind::Percent && k != RowKind::Slider && k != RowKind::Number) return false;
        r.number = (std::min)(r.row->maxValue, (std::max)(r.row->minValue, value));
        return true;
    });
}
bool SetText(const char *id, const char *text, Tone tone)
{
    return withRow(id, [&](RowState &r) {
        if (r.row->kind != RowKind::Status && r.row->kind != RowKind::Binding) return false;
        r.text = text ? text : "";
        r.tone = tone;
        r.capturing = false;   // a binding's new input ends its capture
        return true;
    });
}
bool SetRowVisible(const char *id, bool visible)
{
    return withRow(id, [&](RowState &r) { r.visible = visible; return true; });
}
bool PollEvent(PanelEvent &out)
{
    AcquireSRWLockExclusive(&g_rowLock);
    const bool any = !g_events.empty();
    if (any) { out = g_events.front(); g_events.pop_front(); }
    ReleaseSRWLockExclusive(&g_rowLock);
    return any;
}
bool CancelBindingCapture()
{
    bool any = false;
    AcquireSRWLockExclusive(&g_rowLock);
    for (RowState &r : g_rows) if (r.capturing) { r.capturing = false; any = true; }
    ReleaseSRWLockExclusive(&g_rowLock);
    return any;
}

// ---- UI-2 navigation (dbce_panel.h Nav) ---------------------------------------------------------------------------
static bool g_navShown;                  // a navigation command was used since the panel opened: draw the focus box
static bool g_navScroll;                 // Up/Down moved the focus: keep the focused row in view
bool Navigate(Nav c)
{
    switch (c) {
    case Nav::Open: SetOpen(true); return true;
    case Nav::Close: SetOpen(false); return true;
    case Nav::Back:
        if (!IsOpen()) return false;
        if (!CancelBindingCapture()) SetOpen(false);
        return true;
    default: break;
    }
    if (!g_cfg.descriptor || !IsOpen()) return false;
    AcquireSRWLockExclusive(&g_rowLock);
    if (g_navQueue.size() < 64) g_navQueue.push_back(c);
    ReleaseSRWLockExclusive(&g_rowLock);
    return true;
}
bool NavigateFromKey(int vk, bool down, bool repeat)
{
    if (!g_cfg.descriptor || !IsOpen() || InterlockedCompareExchange(&g_textInput, 0, 0)) return false;
    Nav c;
    switch (vk) {
    case VK_UP: c = Nav::Up; break;
    case VK_DOWN: c = Nav::Down; break;
    case VK_LEFT: c = Nav::Left; break;
    case VK_RIGHT: c = Nav::Right; break;
    case VK_RETURN: case VK_SPACE: if (repeat) return true; c = Nav::Select; break;
    default: return false;
    }
    if (down) Navigate(c);
    return true;
}
void NavFocus(char *out, size_t size)
{
    if (!out || !size) return;
    AcquireSRWLockShared(&g_rowLock);
    strncpy_s(out, size, g_focusId, _TRUNCATE);
    ReleaseSRWLockShared(&g_rowLock);
}

static const char *const kTextSizes[3] = {"Normal", "Large", "Larger"};
static const int kTextScales[3] = {100, 125, 150};
static int textSizeIndex() { return g_userScale >= 150 ? 2 : g_userScale >= 125 ? 1 : 0; }
static void setTextSize(int size)
{
    g_userScale = kTextScales[size];
    char v[16]; sprintf_s(v, "%d", g_userScale);
    savePref("Scale", v);
}
static void setShowFps(bool on) { if (on != g_showFps) { g_showFps = on; savePref("ShowFrameRate", on ? "1" : "0"); } }

// One command against the focus list of the last drawn frame (render thread, g_rowLock not held). One command per
// frame, so each one sees the page as the one before left it.
static void applyNav(Nav c)
{
    g_navShown = true;
    const int n = (int)g_navItems.size();
    if (g_focus > n) g_focus = n;
    if (c == Nav::Up || c == Nav::Down) {
        g_focus = (std::max)(0, (std::min)(n, g_focus + (c == Nav::Down ? 1 : -1)));
        g_navScroll = true;
        return;
    }
    if (g_focus == 0) {   // the tab row: Left/Right change the page, Select goes to its first control
        if (c == Nav::Select) { if (n) { g_focus = 1; g_navScroll = true; } return; }
        const int dir = c == Nav::Right ? 1 : -1;
        for (int p = (int)g_dpage + dir; p >= 0 && p < (int)StdPage::Count; p += dir) {
            if (!DescriptorPageShown((StdPage)p)) continue;
            g_dpage = (StdPage)p; g_pageChanged = true; g_more = false;
            g_navItems.clear();
            savePref("Page", descriptorPageSaveName(g_dpage));
            return;
        }
        return;
    }
    const NavItem it = g_navItems[g_focus - 1];
    switch (it.core) {
    case CORE_MORE: g_more = c == Nav::Select ? !g_more : c == Nav::Right; return;
    case CORE_SHOW_FPS: setShowFps(c == Nav::Select ? !g_showFps : c == Nav::Right); return;
    case CORE_TEXT_SIZE: {
        const int was = textSizeIndex();
        const int size = c == Nav::Select ? (was + 1) % 3 : (std::max)(0, (std::min)(2, was + (c == Nav::Right ? 1 : -1)));
        if (size != was) setTextSize(size);
        return;
    }
    default: break;
    }
    AcquireSRWLockExclusive(&g_rowLock);
    RowState &r = *it.row;
    const Row &d = *r.row;
    bool changed = false;
    const int dir = c == Nav::Right ? 1 : c == Nav::Left ? -1 : 0;
    switch (d.kind) {
    case RowKind::Toggle: {
        const bool v = c == Nav::Select ? !r.toggle : c == Nav::Right;
        changed = v != r.toggle; r.toggle = v;
        break;
    }
    case RowKind::Choice: {
        const int count = (int)d.choices.size();
        if (count < 1) break;
        const int v = c == Nav::Select ? (r.choice + 1) % count : (std::max)(0, (std::min)(count - 1, r.choice + dir));
        changed = v != r.choice; r.choice = v;
        break;
    }
    case RowKind::Percent: case RowKind::Slider: case RowKind::Number: {
        if (!dir) break;
        const double step = d.kind == RowKind::Percent ? 5.0 : d.kind == RowKind::Slider ? 1.0 : 1.0 / std::pow(10.0, d.decimals);
        double v = (std::min)(d.maxValue, (std::max)(d.minValue, r.number + dir * step));
        if (d.kind != RowKind::Number) v = (double)std::lround(v);
        changed = v != r.number; r.number = v;
        break;
    }
    case RowKind::Binding:
        if (c == Nav::Select && !r.capturing) { r.capturing = true; pushEvent(PanelEvent::Type::Capture, r); }
        break;
    case RowKind::Action:
        if (c == Nav::Select) pushEvent(PanelEvent::Type::Action, r);
        break;
    case RowKind::Status: break;
    }
    if (changed) {
        pushEvent(PanelEvent::Type::Changed, r);
        if (d.nextStart) r.changedNextStart = true;
    }
    ReleaseSRWLockExclusive(&g_rowLock);
}

// The focus box: an accent outline around a control, drawn past the body's clip on the sides only.
static void focusBox(const float r[4])
{
    const float s = g_scale, p = 4 * s;
    ImDrawList *dl = ImGui::GetWindowDrawList();
    const ImVec2 clip0 = dl->GetClipRectMin(), clip1 = dl->GetClipRectMax();
    dl->PushClipRect(ImVec2(r[0] - p - 2 * s, clip0.y), ImVec2(r[2] + p + 2 * s, clip1.y), false);
    dl->AddRect(ImVec2(r[0] - p, r[1] - p), ImVec2(r[2] + p, r[3] + p), ImGui::GetColorU32(kAccent), 4 * s, 0, 2 * s);
    dl->PopClipRect();
}

// Each focusable control, in draw order (under g_rowLock): it joins this frame's focus list, and the focused one is
// outlined and kept in view.
static void navItem(RowState *row, CoreItem core, const char *id, const float rect[4])
{
    g_navBuild.push_back({row, core});
    if ((int)g_navBuild.size() != g_focus) return;
    strncpy_s(g_focusId, id, _TRUNCATE);
    if (!g_navShown) return;
    focusBox(rect);
    if (g_navScroll) {
        ImGui::ScrollToRectEx(ImGui::GetCurrentWindow(), ImRect(rect[0], rect[1], rect[2], rect[3]), ImGuiScrollFlags_KeepVisibleEdgeY);
        g_navScroll = false;
    }
}

bool DeveloperOn() { return g_developer; }
void SetDeveloper(bool on) { g_developer = on; }
bool RowRect(const char *id, float rect[4])
{
    if (!id) return false;
    bool ok = false;
    AcquireSRWLockShared(&g_rowLock);
    const unsigned last = g_drawFrame - 1;
    for (const RowState &r : g_rows)
        if (r.row->id == id && r.drawnFrame == last) { memcpy(rect, r.rect, sizeof r.rect); ok = true; }
    for (const CoreRect &c : g_coreRects)
        if (c.id && !strcmp(c.id, id) && c.drawnFrame == last) { memcpy(rect, c.rect, sizeof c.rect); ok = true; }
    ReleaseSRWLockShared(&g_rowLock);
    return ok;
}
StdPage DescriptorPage() { return g_dpage; }
bool DescriptorMoreOptions() { return g_more; }
void ShowDescriptorPage(StdPage page, bool moreOptions)
{
    if (!DescriptorPageShown(page)) return;
    if (g_dpage != page) g_pageChanged = true;
    g_dpage = page;
    g_more = moreOptions;
}
bool DescriptorPageShown(StdPage page)
{
    if (!g_cfg.descriptor) return false;
    if (page == StdPage::Help || page == StdPage::Display) return true;   // Help always; Display has the frame rate
    bool any = false;
    AcquireSRWLockShared(&g_rowLock);
    for (const RowState &r : g_rows) {
        if (!r.visible) continue;
        if (page == StdPage::Developer ? r.row->level == RowLevel::Developer && g_developer
                                       : r.row->page == page && r.row->level != RowLevel::Developer) { any = true; break; }
    }
    ReleaseSRWLockShared(&g_rowLock);
    return any;
}

void ShowToast(const char *line1, const char *line2, unsigned ms)
{
    AcquireSRWLockExclusive(&g_toastLock);
    strncpy_s(g_toast[0], line1 ? line1 : "", _TRUNCATE);
    strncpy_s(g_toast[1], line2 ? line2 : "", _TRUNCATE);
    InterlockedExchange64(&g_toastUntil, g_toast[0][0] || g_toast[1][0] ? (LONG64)(GetTickCount64() + ms) : 0);
    ReleaseSRWLockExclusive(&g_toastLock);
}
bool ToastActive() { return (ULONGLONG)InterlockedCompareExchange64(&g_toastUntil, 0, 0) > GetTickCount64(); }
bool DrawWanted() { return IsOpen() || StatusLineActive() || FrameCounterOn() || ToastActive(); }

static void pushEvent(PanelEvent::Type type, const RowState &r)
{
    PanelEvent e;
    e.type = type;
    e.id = r.row->id;
    e.toggle = r.toggle;
    e.choice = r.choice;
    e.number = r.number;
    if (g_events.size() >= 256) g_events.pop_front();
    g_events.push_back(e);
}

static void noteCoreRect(const char *id, const ImVec2 &a, CoreItem nav = CORE_NONE)
{
    const float x1 = a.x + (std::max)(0.0f, ImGui::GetContentRegionAvail().x);
    const float y1 = ImGui::GetCursorScreenPos().y - ImGui::GetStyle().ItemSpacing.y;
    const float rect[4] = {a.x, a.y, x1, y1};
    for (CoreRect &c : g_coreRects)
        if (!c.id || !strcmp(c.id, id)) { c.id = id; memcpy(c.rect, rect, sizeof rect); c.drawnFrame = g_drawFrame; break; }
    if (nav != CORE_NONE) navItem(nullptr, nav, id, rect);
}

// A number field with its unit, clamped to the row's range.
static bool numberField(const char *label, double *value, double minValue, double maxValue, int decimals, const char *unit, const char *help)
{
    rowLabel(label);
    char fmt[16];
    sprintf_s(fmt, "%%.%df", decimals);
    const double step = decimals ? 1.0 / std::pow(10.0, decimals) : 1.0;
    ImGui::SetNextItemWidth((std::min)(ImGui::GetContentRegionAvail().x, 200 * g_scale));
    double v = *value;
    bool changed = ImGui::InputDouble("##number", &v, step, step * 10, fmt);
    if (unit && *unit) { ImGui::SameLine(); ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted(unit); }
    if (changed) {
        v = (std::min)(maxValue, (std::max)(minValue, v));
        changed = v != *value;
        *value = v;
    }
    if (help && *help) secondaryWrapped(help);
    return changed;
}

// A binding: the current input (or "Not set") as the capture button, and x to clear. While capturing, the row's help is
// the prompt; Esc cancels. Returns 1 to capture, 2 to clear.
static int bindingRow(const char *label, const std::string &text, bool capturing, const char *help)
{
    rowLabel(label);
    int result = 0;
    const float clearW = buttonWidth(kTimes);
    const float w = (std::max)(80 * g_scale, ImGui::GetContentRegionAvail().x - clearW - ImGui::GetStyle().ItemSpacing.x);
    ImGui::PushStyleColor(ImGuiCol_Button, capturing ? kAccent : kButton);
    ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.0f, 0.5f));
    if (ImGui::Button(capturing ? "Waiting for input..." : text.empty() ? "Not set" : text.c_str(), ImVec2(w, 0)) && !capturing) result = 1;
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
    ImGui::SameLine();
    ImGui::BeginDisabled(text.empty() || capturing);
    if (ImGui::Button(kTimes)) result = 2;
    ImGui::EndDisabled();
    if (capturing && help && *help) {
        ImGui::PushStyleColor(ImGuiCol_Text, kWarning);
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextUnformatted(help);
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
    }
    return result;
}

static void statusRow(const std::string &label, const std::string &text, Tone tone)
{
    if (!label.empty()) { Status(label.c_str(), text.c_str(), tone); return; }
    const ImVec4 col = tone == Tone::Good ? kGood : tone == Tone::Warning ? kWarning : tone == Tone::Error ? kError : kText;
    ImGui::AlignTextToFramePadding();
    ImGui::PushStyleColor(ImGuiCol_Text, col);
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted(text.empty() ? " " : text.c_str());
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
}

// One descriptor row with the standard control for its kind (under g_rowLock).
static void drawRow(RowState &r)
{
    const Row &d = *r.row;
    const char *label = d.label.c_str();
    const char *help = d.help.empty() ? nullptr : d.help.c_str();
    ImGui::PushID(d.id.c_str());
    const ImVec2 a = ImGui::GetCursorScreenPos();
    bool changed = false;
    switch (d.kind) {
    case RowKind::Toggle: changed = OffOn(label, &r.toggle, help); break;
    case RowKind::Choice: changed = Choice(label, r.choicePtrs.data(), (int)r.choicePtrs.size(), &r.choice, help); break;
    case RowKind::Percent: case RowKind::Slider: {
        int v = (int)std::lround(r.number);
        std::string fmt = "%d";
        if (d.kind == RowKind::Percent) fmt += "%%";
        else for (char c : d.unit) { fmt += c; if (c == '%') fmt += '%'; }
        changed = Slider(label, &v, (int)d.minValue, (int)d.maxValue, (int)std::lround(d.defaultValue), fmt.c_str(), help);
        if (changed) r.number = v;
        break;
    }
    case RowKind::Number: changed = numberField(label, &r.number, d.minValue, d.maxValue, d.decimals, d.unit.c_str(), help); break;
    case RowKind::Binding: {
        const int b = bindingRow(label, r.text, r.capturing, help);
        if (b == 1) { r.capturing = true; pushEvent(PanelEvent::Type::Capture, r); }
        if (b == 2) pushEvent(PanelEvent::Type::Clear, r);
        break;
    }
    case RowKind::Action:
        ImGui::BeginGroup();
        if (ImGui::Button(label)) pushEvent(PanelEvent::Type::Action, r);
        if (help) secondaryWrapped(help);
        ImGui::EndGroup();
        break;
    case RowKind::Status: statusRow(d.label, r.text, r.tone); break;
    }
    if (changed) {
        pushEvent(PanelEvent::Type::Changed, r);
        if (d.nextStart) r.changedNextStart = true;
    }
    if (d.nextStart && r.changedNextStart) secondaryWrapped("Takes effect next time you start the game.");
    r.rect[0] = a.x; r.rect[1] = a.y;
    r.rect[2] = a.x + (std::max)(0.0f, ImGui::GetContentRegionAvail().x);
    r.rect[3] = ImGui::GetCursorScreenPos().y - ImGui::GetStyle().ItemSpacing.y;
    r.drawnFrame = g_drawFrame;
    if (RowIsControl(d.kind)) navItem(&r, CORE_NONE, d.id.c_str(), r.rect);
    ImGui::PopID();
}

static void drawRows(StdPage page, bool statuses, RowLevel level)
{
    for (RowState &r : g_rows)
        if (r.visible && r.row->page == page && r.row->level == level && (r.row->kind == RowKind::Status) == statuses) drawRow(r);
}

static bool pageHasRows(StdPage page, RowLevel level)
{
    for (const RowState &r : g_rows) if (r.visible && r.row->page == page && r.row->level == level) return true;
    return false;
}

static void displayStatus()
{
    const FrameSummary &fs = g_frames.Last();
    const char *display = g_cfg.displayText ? g_cfg.displayText() : nullptr;
    if (!display || !*display) display = g_cfg.triplesOn ? (g_cfg.triplesOn() ? "Three screens" : "One screen") : "One screen";
    char line[192];
    if (fs.valid) sprintf_s(line, "%s %s %.0f fps", display, kDot, fs.avgFps);
    else sprintf_s(line, "%s %s measuring the frame rate", display, kDot);
    const ImVec2 a = ImGui::GetCursorScreenPos();
    statusRow(std::string(), line, Tone::Normal);
    noteCoreRect("panel.display-status", a);
}

static void helpStatus()
{
    const Descriptor &d = *g_cfg.descriptor;
    std::string version = d.product;
    if (!d.version.empty()) version += " " + d.version;
    ImVec2 a = ImGui::GetCursorScreenPos();
    statusRow(std::string(), version, Tone::Normal);
    noteCoreRect("panel.version", a);
    std::string keys = d.keys;
    if (keys.empty()) {
        keys = "F6 settings";
        if (g_cfg.ffbOn && g_cfg.stopFfb) keys += std::string(" ") + kDot + " F8 force feedback off";
        if (g_cfg.compareKey) keys += std::string(" ") + kDot + " F7 camera view";
    }
    a = ImGui::GetCursorScreenPos();
    statusRow(std::string(), keys, Tone::Normal);
    noteCoreRect("panel.keys", a);
}

static void drawDescriptorBody(StdPage page)
{
    if (page == StdPage::Developer) {
        for (int p = 0; p < (int)StdPage::Developer; ++p) {
            if (!pageHasRows((StdPage)p, RowLevel::Developer)) continue;
            Heading(StdPageName((StdPage)p));
            drawRows((StdPage)p, true, RowLevel::Developer);
            drawRows((StdPage)p, false, RowLevel::Developer);
        }
        return;
    }
    if (page == StdPage::Display) displayStatus();
    if (page == StdPage::Help) helpStatus();
    drawRows(page, true, RowLevel::Simple);
    drawRows(page, false, RowLevel::Simple);
    if (page == StdPage::Help && !g_cfg.descriptor->modPage.empty()) {
        const ImVec2 a = ImGui::GetCursorScreenPos();
        ImGui::AlignTextToFramePadding();
        ImGui::TextLinkOpenURL("Mod page", g_cfg.descriptor->modPage.c_str());
        noteCoreRect("panel.mod-page", a);
    }
    const bool coreMore = page == StdPage::Display || page == StdPage::Help;
    if (!coreMore && !pageHasRows(page, RowLevel::Advanced)) return;
    ImGui::Dummy(ImVec2(0, 4 * g_scale));
    const ImVec2 a = ImGui::GetCursorScreenPos();
    ImGui::SetNextItemOpen(g_more, ImGuiCond_Always);
    g_more = ImGui::TreeNodeEx("More options", ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_FramePadding | ImGuiTreeNodeFlags_SpanAvailWidth);
    noteCoreRect("panel.more", a, CORE_MORE);
    if (!g_more) return;
    if (page == StdPage::Display) {
        const ImVec2 b = ImGui::GetCursorScreenPos();
        if (OffOn("Show frame rate", &g_showFps, "A small counter in the top-right corner.")) savePref("ShowFrameRate", g_showFps ? "1" : "0");
        noteCoreRect("panel.show-frame-rate", b, CORE_SHOW_FPS);
    }
    if (page == StdPage::Help) {
        int size = textSizeIndex();
        const ImVec2 b = ImGui::GetCursorScreenPos();
        if (Choice("Text size", kTextSizes, 3, &size)) setTextSize(size);
        noteCoreRect("panel.text-size", b, CORE_TEXT_SIZE);
    }
    drawRows(page, true, RowLevel::Advanced);
    drawRows(page, false, RowLevel::Advanced);
}

static void drawDescriptorPanel(int width, int height)
{
    const float s = g_scale;
    float x0 = 0, x1 = (float)width;
    if (width >= height * 2.5f) { x0 = width / 3.0f; x1 = 2.0f * width / 3.0f; }
    float w = (std::min)(x1 - x0 - 48 * s, 960 * s), h = (std::min)((float)height - 96 * s, 820 * s);
    w = (std::max)(w, (std::min)(x1 - x0, 480 * s));
    h = (std::max)(h, (std::min)((float)height, 360 * s));
    ImGui::SetNextWindowPos(ImVec2(x0 + (x1 - x0 - w) / 2, (height - h) / 2), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(w, h), ImGuiCond_Always);
    ImGui::Begin("##dbce-panel", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    const ImGuiStyle &st = ImGui::GetStyle();

    // UI-2 navigation: a fresh focus on the tab row when the panel opens, then one queued command per frame.
    if (InterlockedExchange(&g_navReset, 0)) { g_focus = 0; g_navShown = false; g_navItems.clear(); }
    bool haveNav = false;
    Nav nav = Nav::Up;
    AcquireSRWLockExclusive(&g_rowLock);
    if (!g_navQueue.empty()) { nav = g_navQueue.front(); g_navQueue.pop_front(); haveNav = true; }
    ReleaseSRWLockExclusive(&g_rowLock);
    if (haveNav) applyNav(nav);

    // Header: the product name, and x to close (05-ux-simplification.md 1.0).
    ImGui::PushFont(g_heading);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(g_cfg.descriptor->product.c_str());
    ImGui::PopFont();
    ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - buttonWidth(kTimes));
    if (ImGui::Button(kTimes)) SetOpen(false);

    // Tabs in the fixed order; a page without rows is hidden, Help always shows.
    if (!DescriptorPageShown(g_dpage)) { g_dpage = StdPage::Help; g_pageChanged = true; }
    ImGui::Dummy(ImVec2(0, 4 * s));
    bool firstTab = true;
    for (int p = 0; p < (int)StdPage::Count; ++p) {
        if (!DescriptorPageShown((StdPage)p)) continue;
        if (!firstTab) ImGui::SameLine(0, 24 * s);
        firstTab = false;
        const char *name = StdPageName((StdPage)p);
        const bool sel = g_dpage == (StdPage)p;
        ImVec2 sz = ImGui::CalcTextSize(name);
        ImVec2 pos = ImGui::GetCursorScreenPos();
        ImGui::PushID(p);
        if (ImGui::InvisibleButton("tab", ImVec2(sz.x, sz.y + 10 * s)) && !sel) {
            g_dpage = (StdPage)p; g_pageChanged = true; g_more = false;
            savePref("Page", descriptorPageSaveName(g_dpage));
        }
        const bool hov = ImGui::IsItemHovered();
        ImGui::PopID();
        ImDrawList *dl = ImGui::GetWindowDrawList();
        dl->AddText(pos, ImGui::GetColorU32(sel || hov ? kText : kSecondary), name);
        if (sel) dl->AddRectFilled(ImVec2(pos.x, pos.y + sz.y + 6 * s), ImVec2(pos.x + sz.x, pos.y + sz.y + 9 * s), ImGui::GetColorU32(kAccent));
        if (sel && g_focus == 0 && g_navShown) {
            const float box[4] = {pos.x, pos.y, pos.x + sz.x, pos.y + sz.y + 10 * s};
            focusBox(box);
        }
    }
    ImGui::Separator();

    // Body above a one-line footer; it scrolls only when More options is longer than the window.
    const float footer = ImGui::GetFrameHeight() + st.ItemSpacing.y;
    ImGui::BeginChild("##body", ImVec2(0, -footer), ImGuiChildFlags_None, ImGuiWindowFlags_None);
    if (g_pageChanged) { ImGui::SetScrollY(0); g_pageChanged = false; }
    AcquireSRWLockExclusive(&g_rowLock);
    g_navBuild.clear();
    if (g_focus == 0) g_focusId[0] = 0;
    drawDescriptorBody(g_dpage);
    g_navItems.swap(g_navBuild);
    if (g_focus > (int)g_navItems.size()) { g_focus = (int)g_navItems.size(); g_focusId[0] = 0; }
    ReleaseSRWLockExclusive(&g_rowLock);
    ImGui::EndChild();

    // Footer: the keys, and "Saved" for two seconds after a change (or the failure, which stays four).
    ImGui::Separator();
    std::string keys = std::string("F6 close");
    if (g_cfg.ffbOn && g_cfg.stopFfb) keys += std::string(" ") + kDot + " F8 force feedback off";
    ImGui::PushFont(g_small);
    ImGui::PushStyleColor(ImGuiCol_Text, kSecondary);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(keys.c_str());
    ImGui::PopStyleColor();
    const ULONGLONG age = GetTickCount64() - g_noteAt;
    if (g_note[0] && age < (g_noteOk ? 2000u : 4000u)) {
        ImGui::SameLine(0, 16 * s);
        ImGui::PushStyleColor(ImGuiCol_Text, g_noteOk ? kGood : kError);
        ImGui::TextUnformatted(g_noteOk ? g_note : "Couldn't save. The change lasts until you quit.");
        ImGui::PopStyleColor();
    }
    ImGui::PopFont();
    ImGui::End();
}

// Toasts and on-screen labels (05-ux-simplification.md 1.8): at most two lines, at the bottom of the centre screen,
// never input.
static void drawToast(int width, int height)
{
    if (!ToastActive() || width < 64 || height < 64) return;
    char line[2][128];
    AcquireSRWLockShared(&g_toastLock);
    memcpy(line, g_toast, sizeof line);
    ReleaseSRWLockShared(&g_toastLock);
    const float s = g_scale;
    float x0 = 0, x1 = (float)width;
    if (width >= height * 2.5f) { x0 = width / 3.0f; x1 = 2.0f * width / 3.0f; }
    ImGui::SetNextWindowPos(ImVec2((x0 + x1) / 2, height - 48 * s), ImGuiCond_Always, ImVec2(0.5f, 1.0f));
    ImGui::SetNextWindowSizeConstraints(ImVec2(0, 0), ImVec2(x1 - x0 - 48 * s, (float)height / 3));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(18/255.f, 19/255.f, 21/255.f, .94f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16 * s, 10 * s));
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoFocusOnAppearing |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoSavedSettings;
    if (ImGui::Begin("##dbce-toast", nullptr, flags)) {
        ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
        for (int i = 0; i < 2; ++i) {
            if (!line[i][0]) continue;
            ImGui::PushFont(i ? g_body : g_heading);
            const float tw = ImGui::CalcTextSize(line[i]).x, avail = ImGui::GetContentRegionAvail().x;
            if (tw < avail) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail - tw) / 2);
            ImGui::TextUnformatted(line[i]);
            ImGui::PopFont();
        }
    }
    ImGui::End();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
}

// ---- the frame ----------------------------------------------------------------------------------------------
static void drawStatusLine(int width, int height)
{
    if (!StatusLineActive() || width < 64 || height < 64) return;
    char text[256];
    StatusTone tone;
    AcquireSRWLockShared(&g_statusLock);
    memcpy(text, g_statusText, sizeof text);
    tone = g_statusTone;
    ReleaseSRWLockShared(&g_statusLock);
    if (!text[0]) return;
    // Same center-display geometry as SessionStatusPresentation.Layout.
    const float scale = (std::max)(.8f, (std::min)(2.0f, height / 1080.0f));
    const float displayWidth = width >= height * 2.5f ? width / 3.0f : (float)width;
    const float boxWidth = (std::max)(1.0f, (std::min)(760 * scale, displayWidth - 24 * scale));
    const float inset = 14 * scale;
    ImGui::SetNextWindowPos(ImVec2((width - boxWidth) / 2, 12 * scale), ImGuiCond_Always);
    ImGui::SetNextWindowSizeConstraints(ImVec2(boxWidth, 48 * scale), ImVec2(boxWidth, (float)height));
    // F6Palette Window/Text/Accent/Warning; recording/problem use the existing native error tone.
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(18/255.f, 19/255.f, 21/255.f, .94f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(231/255.f, 234/255.f, 240/255.f, 1));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(inset, 12 * scale));
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoFocusOnAppearing |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoSavedSettings;
    if (ImGui::Begin("##dbce-session-status", nullptr, flags)) {
        ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow()); // display order only, never focus
        ImGui::PushFont(g_body);
        ImGui::SetWindowFontScale(scale / g_scale);
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + boxWidth - 2 * inset);
        ImGui::TextUnformatted(text);
        ImGui::PopTextWrapPos();
        ImGui::PopFont();
        const ImVec2 pos = ImGui::GetWindowPos(), size = ImGui::GetWindowSize();
        const ImVec4 color = tone == StatusTone::Recording || tone == StatusTone::Problem ? kError :
            tone == StatusTone::Busy ? ImVec4(224/255.f, 163/255.f, 62/255.f, 1) :
            tone == StatusTone::Success ? kGood : ImVec4(85/255.f, 185/255.f, 232/255.f, 1);
        ImGui::GetWindowDrawList()->AddRectFilled(pos, ImVec2(pos.x + 5 * scale, pos.y + size.y), ImGui::ColorConvertFloat4ToU32(color));
    }
    ImGui::End();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);
}

// STD-024: top-right of the centre screen (centre third of a triple-wide span), outlined; no window, so never input.
static void drawFrameCounter(int width, int height)
{
    if (!g_showFps || !g_heading || width < 64 || height < 64) return;
    char text[48] = "-- fps";
    const FrameSummary &s = g_frames.Last();
    if (g_smoothMs > 0 && s.valid) snprintf(text, sizeof text, "%.0f fps  1%% low %.0f", 1000.0 / g_smoothMs, s.lowFps);
    else if (g_smoothMs > 0) snprintf(text, sizeof text, "%.0f fps", 1000.0 / g_smoothMs);
    const float size = g_heading->FontSize, m = 12 * g_scale, o = (std::max)(1.0f, std::floor(g_scale + 0.5f));
    const ImVec2 ts = g_heading->CalcTextSizeA(size, FLT_MAX, 0.0f, text);
    const float right = width >= height * 2.5f ? 2.0f * width / 3.0f : (float)width;
    const ImVec2 p(std::floor(right - m - ts.x), std::floor(m));
    ImDrawList *dl = ImGui::GetForegroundDrawList();
    for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx)
            if (dx || dy) dl->AddText(g_heading, size, ImVec2(p.x + dx * o, p.y + dy * o), IM_COL32(0, 0, 0, 230), text);
    dl->AddText(g_heading, size, p, IM_COL32(255, 255, 255, 255), text);
}

static void drawPanel(int width, int height)
{
    if (!IsOpen()) return;
    if (g_cfg.descriptor) { drawDescriptorPanel(width, height); return; }
    const float s = g_scale;
    // Placement: on the centre screen of a three-screen span (STD-019), centred in the window otherwise.
    float x0 = 0, x1 = (float)width;
    if (width >= height * 2.5f) { x0 = width / 3.0f; x1 = 2.0f * width / 3.0f; }
    float w = (std::min)(x1 - x0 - 48 * s, 960 * s), h = (std::min)((float)height - 96 * s, 820 * s);
    w = (std::max)(w, (std::min)(x1 - x0, 480 * s));
    h = (std::max)(h, (std::min)((float)height, 360 * s));
    ImGui::SetNextWindowPos(ImVec2(x0 + (x1 - x0 - w) / 2, (height - h) / 2), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(w, h), ImGuiCond_Always);
    ImGui::Begin("##dbce-panel", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    // Header: title left; View, Stop FFB and Close right (UX-01, UX-05).
    ImGui::PushFont(g_heading);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(g_cfg.title);
    ImGui::PopFont();
    static const char *const kViews[2] = {"Simple", "Advanced"};
    const ImGuiStyle &st = ImGui::GetStyle();
    float right = buttonWidth("Close");
    const bool ffb = g_cfg.ffbOn && g_cfg.stopFfb;
    const bool ffbOn = ffb && g_cfg.ffbOn();
    const char *stopLabel = ffbOn ? "Stop FFB" : "FFB off";
    if (ffb) right += buttonWidth(stopLabel) + st.ItemSpacing.x;
    right += ImGui::CalcTextSize("View:").x + st.ItemSpacing.x + buttonWidth("Simple") + buttonWidth("Advanced") + 2 * s + st.ItemSpacing.x * 2;
    bool noteVisible = g_note[0] && GetTickCount64() - g_noteAt < 4000;
    float noteW = noteVisible ? ImGui::CalcTextSize(g_note).x + st.ItemSpacing.x * 2 : 0;
    ImGui::SameLine((std::max)(ImGui::GetCursorPosX() + st.ItemSpacing.x, ImGui::GetWindowContentRegionMax().x - right - noteW));
    if (noteVisible) {
        ImGui::AlignTextToFramePadding();
        ImGui::PushStyleColor(ImGuiCol_Text, g_noteOk ? kGood : kError);
        ImGui::TextUnformatted(g_note);
        ImGui::PopStyleColor();
        ImGui::SameLine(0, st.ItemSpacing.x * 2);
    }
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("View:");
    ImGui::SameLine();
    int view = g_advanced ? 1 : 0;
    if (segments("view", kViews, 2, &view)) { g_advanced = view == 1; savePref("View", g_advanced ? "Advanced" : "Simple"); }
    if (ffb) {
        ImGui::SameLine(0, st.ItemSpacing.x * 2);
        ImGui::BeginDisabled(!ffbOn);
        if (ImGui::Button(stopLabel)) g_cfg.stopFfb();
        ImGui::EndDisabled();
    }
    ImGui::SameLine();
    if (ImGui::Button("Close")) SetOpen(false);

    // Page tabs: flat labels with an accent underline for the selected page (UX-03).
    ImGui::Dummy(ImVec2(0, 4 * s));
    bool firstTab = true;
    for (int p = 0; p < PAGE_COUNT; ++p) {
        if (!g_cfg.pages[p]) continue;
        if (!firstTab) ImGui::SameLine(0, 24 * s);
        firstTab = false;
        const bool sel = g_page == p;
        ImVec2 sz = ImGui::CalcTextSize(kPageNames[p]);
        ImVec2 pos = ImGui::GetCursorScreenPos();
        ImGui::PushID(p);
        if (ImGui::InvisibleButton("tab", ImVec2(sz.x, sz.y + 10 * s))) {
            if (g_page != p) { g_page = (Page)p; g_pageChanged = true; savePref("Page", kPageNames[p]); }
        }
        const bool hov = ImGui::IsItemHovered();
        ImGui::PopID();
        ImDrawList *dl = ImGui::GetWindowDrawList();
        dl->AddText(pos, ImGui::GetColorU32(sel || hov ? kText : kSecondary), kPageNames[p]);
        if (sel) dl->AddRectFilled(ImVec2(pos.x, pos.y + sz.y + 6 * s), ImVec2(pos.x + sz.x, pos.y + sz.y + 9 * s), ImGui::GetColorU32(kAccent));
    }
    ImGui::Separator();

    // Page body: scrollable, scroll reset when the page changes.
    ImGui::BeginChild("##body", ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_None);
    if (g_pageChanged) { ImGui::SetScrollY(0); g_pageChanged = false; }
    if (!g_cfg.pages[g_page]) g_page = PAGE_HELP;
    if (g_page == PAGE_HELP) drawHelp(g_advanced);
    else if (g_cfg.drawPage) g_cfg.drawPage(g_page, g_advanced);
    if (g_page == PAGE_CAMERAS) drawFrameRate();
    ImGui::EndChild();
    ImGui::End();
}


void Draw(int width, int height)
{
    AcquireSRWLockShared(&g_cfgLock);
    drawPanel(width, height);
    drawStatusLine(width, height);
    drawToast(width, height);
    drawFrameCounter(width, height);
    ++g_drawFrame;
    InterlockedExchange(&g_textInput, IsOpen() && ImGui::GetIO().WantTextInput ? 1 : 0);
    ReleaseSRWLockShared(&g_cfgLock);
}

// ---- dev channel (unattended checks; Config::devFile) --------------------------------------------------------
// "open <page> [view]": a page by id or name, More options or the Simple/Advanced view; a page the build does not
// offer leaves the page shown.
static void devOpen(const char *page, const char *view)
{
    if (g_cfg.descriptor) {   // "open <page> [more]": a descriptor page by id or name, More options open or closed
        StdPage p = g_dpage;
        descriptorPageFromName(page, &p);
        SetOpen(true);
        ShowDescriptorPage(p, !_stricmp(view, "more"));
        logf("panel: dev channel opened %s%s", StdPageName(g_dpage), g_more ? " (More options)" : "");
        return;
    }
    for (int p = 0; p < PAGE_COUNT; ++p) if (!_stricmp(page, kPageNames[p]) && g_cfg.pages[p]) { g_page = (Page)p; g_pageChanged = true; }
    if (view[0]) g_advanced = !_stricmp(view, "advanced");   // not saved: a test does not change the owner's view
    SetOpen(true);
    logf("panel: dev channel opened %s (%s)", kPageNames[g_page], g_advanced ? "Advanced" : "Simple");
}

bool OpenForCheck(const char *page, const char *view)
{
    if (!page) page = "";
    if (!view) view = "";
    if (page[0]) {
        bool found = false;
        if (g_cfg.descriptor) {
            StdPage p;
            found = descriptorPageFromName(page, &p) && DescriptorPageShown(p);
        } else {
            for (int p = 0; p < PAGE_COUNT; ++p) if (!_stricmp(page, kPageNames[p]) && g_cfg.pages[p]) found = true;
        }
        if (!found) return false;
    }
    devOpen(page, view);
    return true;
}

void PollDev()
{
    static ULONGLONG nextAt;
    static FILETIME seen;
    if (g_cfg.selfTestTick) g_cfg.selfTestTick();   // every frame: frame waits count these
    if (!g_cfg.devFile) return;
    ULONGLONG now = GetTickCount64();
    if (now < nextAt) return;
    nextAt = now + 500;
    WIN32_FILE_ATTRIBUTE_DATA a;
    if (!GetFileAttributesExA(g_cfg.devFile, GetFileExInfoStandard, &a)) return;
    if (CompareFileTime(&a.ftLastWriteTime, &seen) == 0) return;
    seen = a.ftLastWriteTime;
    // A command left from an earlier run (written before this process started) is never applied: the owner's own
    // launch must not open the panel.
    static FILETIME started;
    if (!started.dwLowDateTime && !started.dwHighDateTime) {
        FILETIME exitT, kernelT, userT;
        if (!GetProcessTimes(GetCurrentProcess(), &started, &exitT, &kernelT, &userT)) started.dwLowDateTime = 1;
    }
    if (CompareFileTime(&a.ftLastWriteTime, &started) < 0) return;
    char text[128] = "";
    HANDLE f = CreateFileA(g_cfg.devFile, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return;
    DWORD got = 0;
    ReadFile(f, text, sizeof text - 1, &got, nullptr);
    CloseHandle(f);
    text[got] = 0;
    char verb[16] = "", page[16] = "", view[16] = "";
    sscanf_s(text, "%15s %15s %15s", verb, (unsigned)sizeof verb, page, (unsigned)sizeof page, view, (unsigned)sizeof view);
    if (!_stricmp(verb, "input-trace")) { TraceInput(page, g_cfg.log); return; }
    if (!_stricmp(verb, "close")) { SetOpen(false); logf("panel: dev channel closed the panel"); return; }
    if (!_stricmp(verb, "open")) devOpen(page, view);
}
}} // namespace dbce::panel
