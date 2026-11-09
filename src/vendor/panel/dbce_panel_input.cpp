// See dbce_panel_input.h.
#include "dbce_panel_input.h"
#include "dbce_panel.h"
#include "../third_party/imgui/imgui.h"
#include <windowsx.h>
#include <cstdio>
#include <cstring>

namespace dbce { namespace panel {

namespace {
enum EvType { EV_KEY, EV_CHAR, EV_MOVE, EV_BUTTON, EV_WHEEL, EV_RESET };
struct Ev { EvType type; int a, b; };
const int kQueue = 512;
Ev g_q[kQueue];
int g_head, g_count;
CRITICAL_SECTION g_cs;
INIT_ONCE g_once = INIT_ONCE_STATIC_INIT;
HWND g_hwnd;
HHOOK g_hook;
DWORD g_hookThread;
bool g_wasOpen;
bool g_shift;   // the Shift key is down (tracked from the hook's messages): Shift+Tab passes through

struct InputTrace {
    bool enabled = false, open = false;
    HWND window = nullptr;
    HHOOK hook = nullptr;
    DWORD thread = 0;
    unsigned attach = 0, attachFailed = 0, detached = 0;
    unsigned callbacks = 0, action = 0, remove = 0, noRemove = 0;
    unsigned owned = 0, child = 0, foreign = 0, nullWindow = 0, nullMessage = 0;
    unsigned f6Down = 0, f6Up = 0, f6Repeat = 0, escDown = 0, escUp = 0, escRepeat = 0;
    unsigned mouseSeen = 0, mouseQueued = 0, opened = 0, closed = 0, consumed = 0;
};
InputTrace g_trace;
unsigned g_traceReports;
constexpr unsigned kTraceLimit = 1000000, kTraceReports = 8;
void bump(unsigned &value) { if (value < kTraceLimit) ++value; }
void traceOpen(bool open)
{
    if (!g_trace.enabled || open == g_trace.open) return;
    bump(open ? g_trace.opened : g_trace.closed);
    g_trace.open = open;
}
void traceMessage(const MSG &m)
{
    const bool down = m.message == WM_KEYDOWN || m.message == WM_SYSKEYDOWN;
    const bool up = m.message == WM_KEYUP || m.message == WM_SYSKEYUP;
    if ((down || up) && (m.wParam == VK_F6 || m.wParam == VK_ESCAPE)) {
        const bool repeat = down && (m.lParam & (1 << 30));
        if (m.wParam == VK_F6) bump(up ? g_trace.f6Up : repeat ? g_trace.f6Repeat : g_trace.f6Down);
        else bump(up ? g_trace.escUp : repeat ? g_trace.escRepeat : g_trace.escDown);
    }
    switch (m.message) {
    case WM_LBUTTONDOWN: case WM_LBUTTONUP: case WM_LBUTTONDBLCLK:
    case WM_RBUTTONDOWN: case WM_RBUTTONUP: case WM_RBUTTONDBLCLK:
    case WM_MBUTTONDOWN: case WM_MBUTTONUP: case WM_MBUTTONDBLCLK:
    case WM_XBUTTONDOWN: case WM_XBUTTONUP: case WM_XBUTTONDBLCLK:
        bump(g_trace.mouseSeen); break;
    default: break;
    }
}

BOOL CALLBACK initOnce(PINIT_ONCE, PVOID, PVOID *) { InitializeCriticalSection(&g_cs); return TRUE; }

void push(EvType t, int a, int b)
{
    EnterCriticalSection(&g_cs);
    if (g_count < kQueue) {
        g_q[(g_head + g_count) % kQueue] = {t, a, b}; ++g_count;
        if (g_trace.enabled && t == EV_BUTTON) bump(g_trace.mouseQueued);
    }
    LeaveCriticalSection(&g_cs);
}

bool ownsWindow(HWND w) { return g_hwnd && (w == g_hwnd || IsChild(g_hwnd, w)); }

POINT toClient(HWND from, LPARAM lp)
{
    POINT p = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
    if (from != g_hwnd) MapWindowPoints(from, g_hwnd, &p, 1);
    return p;
}

// Returns true when the message is consumed (the game sees WM_NULL).
bool handle(MSG *m)
{
    const UINT msg = m->message;
    const bool open = IsOpen();
    if (msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN || msg == WM_KEYUP || msg == WM_SYSKEYUP) {
        const int vk = (int)m->wParam;
        const bool down = msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN;
        const bool repeat = (m->lParam & (1 << 30)) != 0;
        const bool alt = (m->lParam & (1 << 29)) != 0;
        if (vk == VK_F6) { if (down && !repeat) Toggle(); return true; }
        if (vk == VK_F8 && StopFfbAvailable()) { if (down && !repeat) StopFfbFromKey(); return true; }
        if (vk == VK_F7 && CompareAvailable()) { if (down && !repeat) CompareFromKey(); return true; }
        if (vk == VK_SHIFT) g_shift = down;                         // tracked whenever seen, open or closed
        if (!open) return CameraKeyFromMessage(vk, down, repeat);   // the mod's camera keys, else the game's
        if (alt && (vk == VK_TAB || vk == VK_F4)) return false;     // leave window switching and closing alone
        if (g_shift && vk == VK_TAB) return false;                  // Shift+Tab reaches the Steam overlay
        if (vk == VK_LWIN || vk == VK_RWIN) return false;
        if (vk == VK_ESCAPE) { if (down && !repeat && !CancelBindingCapture()) SetOpen(false); return true; }
        if (NavigateFromKey(vk, down, repeat)) return true;          // descriptor pages: the same path as the wheel
        push(EV_KEY, vk, down ? 1 : 0);
        return true;
    }
    if (!open) return false;
    switch (msg) {
    case WM_CHAR: case WM_SYSCHAR:
        push(EV_CHAR, (int)m->wParam, 0); return true;
    case WM_MOUSEMOVE: {
        POINT p = toClient(m->hwnd, m->lParam); push(EV_MOVE, p.x, p.y); return true; }
    case WM_LBUTTONDOWN: case WM_LBUTTONDBLCLK: push(EV_BUTTON, 0, 1); return true;
    case WM_LBUTTONUP: push(EV_BUTTON, 0, 0); return true;
    case WM_RBUTTONDOWN: case WM_RBUTTONDBLCLK: push(EV_BUTTON, 1, 1); return true;
    case WM_RBUTTONUP: push(EV_BUTTON, 1, 0); return true;
    case WM_MBUTTONDOWN: case WM_MBUTTONDBLCLK: push(EV_BUTTON, 2, 1); return true;
    case WM_MBUTTONUP: push(EV_BUTTON, 2, 0); return true;
    case WM_MOUSEWHEEL: push(EV_WHEEL, GET_WHEEL_DELTA_WPARAM(m->wParam), 0); return true;
    case WM_MOUSEHWHEEL: return true;
    case WM_XBUTTONDOWN: case WM_XBUTTONUP: case WM_XBUTTONDBLCLK: return true;
    case WM_INPUT: return true;   // the game's raw keyboard/mouse while the panel owns input
    default: return false;
    }
}

LRESULT CALLBACK getMessageHook(int code, WPARAM wp, LPARAM lp)
{
    if (code < 0) return CallNextHookEx(nullptr, code, wp, lp);
    // Negative codes and NOREMOVE may carry an unusable MSG pointer: never read it.
    EnterCriticalSection(&g_cs);
    if (g_trace.enabled) {
        bump(g_trace.callbacks);
        if (code == HC_ACTION) {
            bump(g_trace.action);
            if (wp == PM_REMOVE) bump(g_trace.remove);
            else if (wp == PM_NOREMOVE) bump(g_trace.noRemove);
        }
    }
    if (code == HC_ACTION && wp == PM_REMOVE) {
        MSG *m = (MSG *)lp;
        // The window check and the handling under the lock DetachInput takes: once it returns, nothing it let go of is
        // handled (a callback may still be running after UnhookWindowsHookEx returns). Not held across CallNextHookEx.
        const bool owned = m && ownsWindow(m->hwnd);
        if (g_trace.enabled) {
            if (!m) bump(g_trace.nullMessage);
            else {
                if (!m->hwnd) bump(g_trace.nullWindow);
                else if (owned) bump(m->hwnd == g_hwnd ? g_trace.owned : g_trace.child);
                else bump(g_trace.foreign);
                traceMessage(*m);
            }
        }
        const bool consumed = owned && handle(m);
        if (g_trace.enabled) { if (consumed) bump(g_trace.consumed); traceOpen(IsOpen()); }
        if (consumed) m->message = WM_NULL;
    }
    LeaveCriticalSection(&g_cs);
    return CallNextHookEx(nullptr, code, wp, lp);
}

ImGuiKey keyOf(int vk)
{
    switch (vk) {
    case VK_TAB: return ImGuiKey_Tab;
    case VK_LEFT: return ImGuiKey_LeftArrow;
    case VK_RIGHT: return ImGuiKey_RightArrow;
    case VK_UP: return ImGuiKey_UpArrow;
    case VK_DOWN: return ImGuiKey_DownArrow;
    case VK_PRIOR: return ImGuiKey_PageUp;
    case VK_NEXT: return ImGuiKey_PageDown;
    case VK_HOME: return ImGuiKey_Home;
    case VK_END: return ImGuiKey_End;
    case VK_INSERT: return ImGuiKey_Insert;
    case VK_DELETE: return ImGuiKey_Delete;
    case VK_BACK: return ImGuiKey_Backspace;
    case VK_SPACE: return ImGuiKey_Space;
    case VK_RETURN: return ImGuiKey_Enter;
    case VK_SHIFT: return ImGuiKey_LeftShift;
    case VK_CONTROL: return ImGuiKey_LeftCtrl;
    case VK_MENU: return ImGuiKey_LeftAlt;
    default:
        if (vk >= '0' && vk <= '9') return (ImGuiKey)(ImGuiKey_0 + (vk - '0'));
        if (vk >= 'A' && vk <= 'Z') return (ImGuiKey)(ImGuiKey_A + (vk - 'A'));
        return ImGuiKey_None;
    }
}
} // namespace

bool AttachInput(HWND w)
{
    InitOnceExecuteOnce(&g_once, initOnce, nullptr, nullptr);
    if (!w) return false;
    DWORD thread = GetWindowThreadProcessId(w, nullptr);
    EnterCriticalSection(&g_cs);
    bool ok = true;
    if (w != g_hwnd || thread != g_hookThread) {
        if (g_hook && thread != g_hookThread) { UnhookWindowsHookEx(g_hook); g_hook = nullptr; }
        if (!g_hook) {
            // Thread-local hook in this process: no DLL injection, no window-procedure replacement.
            g_hook = SetWindowsHookExW(WH_GETMESSAGE, getMessageHook, nullptr, thread);
            ok = g_hook != nullptr;
            g_hookThread = ok ? thread : 0;
        }
        g_hwnd = w;
    }
    if (g_trace.enabled) {
        bump(g_trace.attach);
        if (!ok) bump(g_trace.attachFailed);
        g_trace.window = w; g_trace.thread = thread; g_trace.hook = g_hook;
    }
    LeaveCriticalSection(&g_cs);
    return ok;
}

void DetachInput()
{
    InitOnceExecuteOnce(&g_once, initOnce, nullptr, nullptr);
    EnterCriticalSection(&g_cs);
    if (g_hook) { UnhookWindowsHookEx(g_hook); g_hook = nullptr; }
    g_hookThread = 0;
    g_hwnd = nullptr;
    g_head = g_count = 0;   // queued events belong to the window let go of
    g_wasOpen = false;      // the next open starts from fresh input state
    g_shift = false;        // no carry-over of a Shift held while the window was let go of
    SetOpen(false);         // inside the lock: no handler admitted before this can reopen it after
    if (g_trace.enabled) { bump(g_trace.detached); traceOpen(false); }
    LeaveCriticalSection(&g_cs);
}

HWND InputWindow() { return g_hwnd; }

void TraceInput(const char *command, void (*log)(const char *))
{
    if (!command) return;
    const bool on = !std::strcmp(command, "on"), off = !std::strcmp(command, "off");
    if (!on && !off && std::strcmp(command, "snapshot")) return;
    InitOnceExecuteOnce(&g_once, initOnce, nullptr, nullptr);
    EnterCriticalSection(&g_cs);
    if (on) {
        g_trace = InputTrace{};
        g_trace.enabled = true; g_trace.open = IsOpen();
        g_trace.window = g_hwnd; g_trace.thread = g_hookThread; g_trace.hook = g_hook;
    } else traceOpen(IsOpen());
    if (off) g_trace.enabled = false;
    const InputTrace s = g_trace;
    const HWND current = g_hwnd;
    const HHOOK hook = g_hook;
    const DWORD thread = g_hookThread;
    const bool report = g_traceReports < kTraceReports;
    if (report) ++g_traceReports;
    LeaveCriticalSection(&g_cs);
    if (!report || !log) return;
    char line[512];
    // Fixed, bounded output only at the frame/dev boundary, never in the hook.
    sprintf_s(line, "input-trace: enabled=%d current hwnd=%p thread=%lu hook=%p; last hwnd=%p thread=%lu hook=%p attach=%u failed=%u detach=%u",
              s.enabled, (void *)current, thread, (void *)hook, (void *)s.window, s.thread, (void *)s.hook, s.attach, s.attachFailed, s.detached); log(line);
    sprintf_s(line, "input-trace: callbacks=%u action=%u remove=%u noremove=%u root=%u child=%u foreign=%u null-window=%u null-msg=%u consumed=%u",
              s.callbacks, s.action, s.remove, s.noRemove, s.owned, s.child, s.foreign, s.nullWindow, s.nullMessage, s.consumed); log(line);
    sprintf_s(line, "input-trace: F6 down/up/repeat=%u/%u/%u Escape=%u/%u/%u mouse-seen=%u queued=%u open=%d opened=%u closed=%u",
              s.f6Down, s.f6Up, s.f6Repeat, s.escDown, s.escUp, s.escRepeat, s.mouseSeen, s.mouseQueued, s.open, s.opened, s.closed); log(line);
}

void DrainInput(int bw, int bh)
{
    InitOnceExecuteOnce(&g_once, initOnce, nullptr, nullptr);
    ImGuiIO &io = ImGui::GetIO();
    const bool open = IsOpen();
    // Releasing a cursor clip changes desktop state: only the panel's foreground
    // root may do it. Hidden/inactive hosts and missing window roots leave it alone.
    const HWND foreground = open && g_hwnd ? GetForegroundWindow() : nullptr;
    const HWND panelRoot = foreground ? GetAncestor(g_hwnd, GA_ROOT) : nullptr;
    const HWND foregroundRoot = foreground ? GetAncestor(foreground, GA_ROOT) : nullptr;
    const bool ownsForeground = panelRoot && foregroundRoot && panelRoot == foregroundRoot;
    if (open && !g_wasOpen) {
        // Fresh state on open: no stale keys, the pointer where the system cursor is, the cursor free to move.
        io.ClearInputKeys();
        POINT p;
        if (GetCursorPos(&p) && g_hwnd && ScreenToClient(g_hwnd, &p)) push(EV_MOVE, p.x, p.y);
        if (ownsForeground) ClipCursor(nullptr);
    }
    if (!open && g_wasOpen) io.ClearInputKeys();
    g_wasOpen = open;
    RECT rc = {0, 0, bw, bh};
    if (g_hwnd) GetClientRect(g_hwnd, &rc);
    const float sx = rc.right > 0 ? (float)bw / (float)rc.right : 1.0f;
    const float sy = rc.bottom > 0 ? (float)bh / (float)rc.bottom : 1.0f;
    Ev local[kQueue];
    int n = 0;
    EnterCriticalSection(&g_cs);
    traceOpen(IsOpen());   // current under the lock; the render snapshot may predate an F6 transition
    for (; n < g_count; ++n) local[n] = g_q[(g_head + n) % kQueue];
    g_head = 0; g_count = 0;
    LeaveCriticalSection(&g_cs);
    for (int i = 0; i < n; ++i) {
        const Ev &e = local[i];
        switch (e.type) {
        case EV_KEY: {
            ImGuiKey k = keyOf(e.a);
            if (e.a == VK_SHIFT) io.AddKeyEvent(ImGuiMod_Shift, e.b != 0);
            if (e.a == VK_CONTROL) io.AddKeyEvent(ImGuiMod_Ctrl, e.b != 0);
            if (e.a == VK_MENU) io.AddKeyEvent(ImGuiMod_Alt, e.b != 0);
            if (k != ImGuiKey_None) io.AddKeyEvent(k, e.b != 0);
            break; }
        case EV_CHAR: if (e.a > 0 && e.a < 0x10000) io.AddInputCharacterUTF16((unsigned short)e.a); break;
        case EV_MOVE: io.AddMousePosEvent(e.a * sx, e.b * sy); break;
        case EV_BUTTON: io.AddMouseButtonEvent(e.a, e.b != 0); break;
        case EV_WHEEL: io.AddMouseWheelEvent(0.0f, (float)e.a / (float)WHEEL_DELTA); break;
        default: break;
        }
    }
    if (open && ownsForeground) ClipCursor(nullptr);   // games re-clip the cursor every frame; the panel needs the whole window
}

}} // namespace dbce::panel
