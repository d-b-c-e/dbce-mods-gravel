#include "../../src/vendor/settings/dbce_ini_settings_win.hpp"
#include <cassert>
#include <cstdio>
using namespace dbce::settings;
std::string wide(const std::string& s) {
    std::string result("\xff\xfe", 2);
    for (char c : s) { result += c; result += '\0'; }
    return result;
}
int main(int argc, char** argv) {
    assert(argc == 2);
    const std::string original = "; retained\r\n[triple]\r\n  VIEW = angle  \r\nunknown=42\r\n[camera]\r\nforward_m=0\r\nright_m=0\r\n";
    const std::string expected = "; retained\r\n[triple]\r\n  VIEW = original  \r\nunknown=42\r\n[camera]\r\nforward_m=0\r\nright_m=0\r\n";
    std::string out;
    for (const auto& prefix : {std::string(), std::string("\xef\xbb\xbf", 3)}) {
        assert(edit(prefix + original, "triple", {{"view", "original"}}, out));
        assert(out == prefix + expected);
    }
    assert(edit(wide(original), "triple", {{"view", "original"}}, out));
    assert(out == wide(expected));
    auto unicode = wide(original + "; ") + std::string("\xe9\x00", 2);
    assert(edit(unicode, "triple", {{"view", "original"}}, out));
    assert(out == wide(expected + "; ") + std::string("\xe9\x00", 2));
    assert(edit("[triple]\nview=angle  ; my note\n", "triple", {{"view", "original"}}, out));
    assert(out == "[triple]\nview=original  ; my note\n");
    assert(edit("[triple]\n; note\n[camera]\nx=0", "triple", {{"view", "angle"}}, out));
    assert(out == "[triple]\n; note\nview=angle\n[camera]\nx=0");
    assert(edit("[triple]", "triple", {{"view", "angle"}}, out));
    assert(out == "[triple]\nview=angle\n");
    assert(edit(original, "camera", {{"forward_m", "0.020"}, {"right_m", "0.100"}, {"up_m", "0.200"}}, out));
    assert(out == original.substr(0, original.find("forward_m")) + "forward_m=0.020\r\nright_m=0.100\r\nup_m=0.200\r\n");
    for (const auto& invalid : {"[triple]\nview=angle\nVIEW=original", "[triple]\n[TRIPLE]\n", "[triple\nview=angle", "[triple]\ngarbage", "[camera]\nx=0"})
        assert(!edit(invalid, "triple", {{"view", "original"}}, out));
    assert(!edit(original, "triple", {{"view", "original"}, {"VIEW", "angle"}}, out));
    assert(!edit(original, "triple", {{"view", "angle\nmode=off"}}, out));
    assert(!edit("[triple]\rview=angle", "triple", {{"view", "original"}}, out));
    const std::string path = std::string(argv[1]) + "/settings.ini";
    auto write = [&](const std::string& s) { FILE* f = fopen(path.c_str(), "wb"); assert(f); assert(fwrite(s.data(), 1, s.size(), f) == s.size()); fclose(f); };
    auto read = [&]() { FILE* f = fopen(path.c_str(), "rb"); assert(f); std::string s; int c; while ((c = fgetc(f)) != EOF) s += char(c); fclose(f); return s; };
    for (const auto& bytes : {original, wide(original)}) {
        write(bytes);
        assert(save(path.c_str(), "triple", {{"view", "original"}}));
        assert(read() == (bytes == original ? expected : wide(expected)));
    }
    write(original);
    HANDLE lock = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    assert(lock != INVALID_HANDLE_VALUE);
    assert(!save(path.c_str(), "triple", {{"view", "original"}}));
    CloseHandle(lock);
    assert(read() == original);
    write("[triple]\nview=angle\nview=original");
    assert(!save(path.c_str(), "triple", {{"view", "original"}}));
    assert(read() == "[triple]\nview=angle\nview=original");
    DeleteFileA(path.c_str());
    puts("PASS: INI byte preservation, batched camera saves, malformed/locked refusal and atomic disk replacement");
}
