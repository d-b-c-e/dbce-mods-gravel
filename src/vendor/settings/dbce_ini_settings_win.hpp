// Native Windows INI saves. The caller serializes in-process writers and excludes external editors during use.
#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include <utility>
#include <cstdio>

namespace dbce::settings {
using Changes = std::vector<std::pair<std::string, std::string>>;

inline std::string trim(std::string s) {
    const auto a = s.find_first_not_of(" \t");
    return a == s.npos ? "" : s.substr(a, s.find_last_not_of(" \t") - a + 1);
}
inline std::string lower(std::string s) {
    for (auto& c : s) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    return s;
}

// Operate on ASCII INI syntax while retaining every original byte outside the owned values.
// UTF-16 uses a one-byte syntax view; all other code units remain opaque in the original file.
// remove: each listed key's whole line goes instead (the values are ignored); a missing section or key is left as it is.
inline bool edit(const std::string& bytes, const std::string& section, const Changes& changes, std::string& out, bool allow_create = false, bool remove = false) {
    auto ascii = [](const std::string& s) { for (unsigned char c : s) if (c < 32 || c >= 127) return false; return true; };
    if (section.empty() || !ascii(section) || section.find_first_of("[]") != section.npos) return false;
    for (size_t j = 0; j < changes.size(); ++j) {
        const auto& key = changes[j].first;
        if (key.empty() || !ascii(key) || trim(key) != key || key.find_first_of("=[];#") != key.npos || !ascii(changes[j].second)) return false;
        for (size_t k = 0; k < j; ++k) if (lower(key) == lower(changes[k].first)) return false;
    }
    const bool wide = bytes.size() >= 2 && bytes.compare(0, 2, "\xff\xfe") == 0;
    const size_t bom = wide ? 2 : bytes.compare(0, 3, "\xef\xbb\xbf") == 0 ? 3 : 0;
    const size_t step = wide ? 2 : 1;
    if ((wide && (bytes.size() - bom) % 2) || bytes.compare(0, 2, "\xfe\xff") == 0) return false;
    std::string syntax;
    for (size_t i = bom; i < bytes.size(); i += step) {
        if (!bytes[i] && (!wide || !bytes[i + 1])) return false;
        syntax += wide && bytes[i + 1] ? '\x7f' : bytes[i];
    }
    for (size_t i = 0; i < syntax.size(); ++i) if (syntax[i] == '\r' && (i + 1 == syntax.size() || syntax[i + 1] != '\n')) return false;
    struct Replacement { size_t a, b; std::string text; };
    std::vector<Replacement> edits;
    std::vector<bool> found(changes.size(), false);
    bool active = false, seen = false;
    size_t insert = syntax.size();
    std::string eol = syntax.find("\r\n") != syntax.npos ? "\r\n" : "\n";
    for (size_t a = 0; a < syntax.size();) {
        auto end = syntax.find('\n', a);
        if (end == syntax.npos) end = syntax.size();
        size_t b = end;
        if (b > a && syntax[b - 1] == '\r') --b;
        const auto line = trim(syntax.substr(a, b - a));
        if (!line.empty() && line[0] != ';' && line[0] != '#') {
            if (line[0] == '[') {
                const auto close = line.find(']');
                if (close == line.npos || !trim(line.substr(close + 1)).empty()) return false;
                if (active) insert = a;
                active = lower(trim(line.substr(1, close - 1))) == lower(section);
                if (active && seen) return false;
                seen = seen || active;
            } else {
                const auto eq = syntax.find('=', a);
                if (eq == syntax.npos || eq >= b) return false;
                if (active) for (size_t j = 0; j < changes.size(); ++j) {
                    if (lower(trim(syntax.substr(a, eq - a))) != lower(changes[j].first)) continue;
                    if (found[j]) return false;
                    found[j] = true;
                    size_t v = eq + 1, last = b;
                    for (size_t k = v + 1; k < b; ++k)
                        if ((syntax[k] == ';' || syntax[k] == '#') && (syntax[k - 1] == ' ' || syntax[k - 1] == '\t')) { last = k; break; }
                    while (v < last && (syntax[v] == ' ' || syntax[v] == '\t')) ++v;
                    while (last > v && (syntax[last - 1] == ' ' || syntax[last - 1] == '\t')) --last;
                    if (remove) edits.push_back({a, end < syntax.size() ? end + 1 : end, ""});
                    else edits.push_back({v, last, changes[j].second});
                }
            }
        }
        a = end < syntax.size() ? end + 1 : end;
    }
    if (!seen && remove) { out = bytes; return true; }
    if (!seen) {
        if (!allow_create || changes.empty()) return false;
        // Opt-in first setup only. The scan above has already refused malformed input.
        std::string extra = !syntax.empty() && syntax.back() != '\n' ? eol : "";
        extra += "[" + section + "]" + eol;
        std::string seeded = bytes;
        for (char c : extra) { seeded += c; if (wide) seeded += '\0'; }
        return edit(seeded, section, changes, out);
    }
    std::string extra;
    for (size_t j = 0; j < changes.size(); ++j) if (!found[j] && !remove) extra += changes[j].first + "=" + changes[j].second + eol;
    if (!extra.empty()) {
        if (insert && syntax[insert - 1] != '\n') extra = eol + extra;
        edits.push_back({insert, insert, extra});
    }
    out = bytes;
    // Replacements were collected in file order, with insertion at the section end.
    for (auto it = edits.rbegin(); it != edits.rend(); ++it) {
        std::string encoded;
        for (char c : it->text) { encoded += c; if (wide) encoded += '\0'; }
        out.replace(bom + it->a * step, (it->b - it->a) * step, encoded);
    }
    return true;
}

inline bool save(const char* path, const char* section, const Changes& changes, bool allow_create = false, bool remove = false) {
    HANDLE input = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    const bool existed = input != INVALID_HANDLE_VALUE;
    if (!existed && !(allow_create && GetLastError() == ERROR_FILE_NOT_FOUND)) return false;
    BY_HANDLE_FILE_INFORMATION info{};
    LARGE_INTEGER size{};
    SetLastError(ERROR_SUCCESS);
    bool ok = !existed || (GetFileInformationByHandle(input, &info) && !(info.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)) &&
              info.nNumberOfLinks == 1 && GetFileSizeEx(input, &size) && (size.QuadPart > 0 || allow_create) && size.QuadPart <= 1024 * 1024);
    std::string bytes(ok ? size_t(size.QuadPart) : 0, '\0'), next;
    DWORD got = 0;
    ok = ok && (!existed || (ReadFile(input, bytes.data(), DWORD(bytes.size()), &got, nullptr) && got == bytes.size())) && edit(bytes, section, changes, next, allow_create, remove);
    if (ok && next == bytes) { if (existed) CloseHandle(input); return true; }
    char suffix[96];
    snprintf(suffix, sizeof suffix, ".dbce-%lu-%llu.tmp", GetCurrentProcessId(), GetTickCount64());
    const std::string temp = std::string(path) + suffix;
    HANDLE output = INVALID_HANDLE_VALUE;
    if (ok) output = CreateFileA(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    ok = ok && output != INVALID_HANDLE_VALUE;
    if (ok) ok = WriteFile(output, next.data(), DWORD(next.size()), &got, nullptr) && got == next.size() && FlushFileBuffers(output);
    if (output != INVALID_HANDLE_VALUE) CloseHandle(output);
    // Retain the read handle (denies writes) until atomic replacement, so no partial file is visible.
    if (ok && existed) {
        HANDLE current = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        BY_HANDLE_FILE_INFORMATION now{};
        ok = current != INVALID_HANDLE_VALUE && GetFileInformationByHandle(current, &now) &&
             info.dwVolumeSerialNumber == now.dwVolumeSerialNumber && info.nFileIndexHigh == now.nFileIndexHigh && info.nFileIndexLow == now.nFileIndexLow;
        if (current != INVALID_HANDLE_VALUE) CloseHandle(current);
        if (ok) ok = ReplaceFileA(path, temp.c_str(), nullptr, 0, nullptr, nullptr) != 0;
    }
    else if (ok) ok = MoveFileExA(temp.c_str(), path, MOVEFILE_WRITE_THROUGH) != 0; // Never overwrite a newly appeared file.
    const DWORD error = GetLastError();
    if (existed) CloseHandle(input);
    if (!ok && output != INVALID_HANDLE_VALUE) DeleteFileA(temp.c_str());
    if (!ok) SetLastError(error ? error : ERROR_INVALID_DATA);
    return ok;
}
} // namespace dbce::settings
