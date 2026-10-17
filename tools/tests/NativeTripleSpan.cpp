// Test-TripleSpan.ps1 inserts the real tripleAttach blocks at the marked locations.
#include <cstdio>
#include <cwchar>
#include <cwctype>
#include <string>

struct { bool separate; struct { long left, top, right, bottom; } span; } g_layout;
struct { bool span; } g_tc;
std::wstring g_commandLine;
int starts = 0;
void spanThread() {}
void CreateThread(void *, int, void (*)(), void *, int, void *) { ++starts; }

void production(std::wstring line)
{
    std::wstring lower = line;
    for (auto &ch : lower) ch = static_cast<wchar_t>(towlower(ch));
    /* SPAN_SETUP */
    /* SPAN_START */
}

int main()
{
    int checks = 0, failed = 0;
    const wchar_t *const lines[] = {
        L"Gravel.exe", L"Gravel.exe -ResX=2560 -ResY=1440",
        L"Gravel.exe -resx=7680 -resy=1440 -windowed",
        L"Gravel.exe -RESX=1920 -fullscreen"
    };
    // Separate/Surround/single behavior, opt-out, existing resolution and mixed case.
    // Both negative and positive span origins must produce width, not right coordinate.
    for (bool separate : {false, true}) for (bool enabled : {false, true})
    for (long left : {-2560L, 2560L}) for (const auto *args : lines) {
        g_layout = {separate, {left, 0, left + 7680, 1440}};
        g_tc.span = enabled;
        starts = 0;
        production(args);
        const std::wstring original(args);
        const bool eligible = separate && enabled;
        const bool noResolution = original == L"Gravel.exe";
        const std::wstring expected = original + (eligible && noResolution
            ? L" -windowed -ResX=7680 -ResY=1440" : L"");
        ++checks;
        if (starts != (eligible ? 1 : 0) || g_commandLine != expected) {
            ++failed;
            std::printf("FAIL case %d: separate=%d enabled=%d left=%ld starts=%d\n",
                        checks, separate, enabled, left, starts);
        }
    }
    std::printf("%s %d span launch cases (%d failed)\n", failed ? "FAIL" : "PASS", checks, failed);
    return failed ? 1 : 0;
}
