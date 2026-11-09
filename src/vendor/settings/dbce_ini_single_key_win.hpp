// Atomic owned-key saves shared by panel, force and consumer writers across translation units.
#pragma once
#include "dbce_ini_settings_win.hpp"
#include <mutex>

namespace dbce::settings {
// One inline-function static for the entire program: single-key, batch and erase callers.
inline std::mutex& writerLock() { static std::mutex writers; return writers; }
inline bool saveKeys(const char* path, const char* section, const Changes& changes) {
    const std::lock_guard<std::mutex> guard(writerLock());
    if (!path || !section || changes.empty()) { SetLastError(ERROR_INVALID_PARAMETER); return false; }
    // First use may create the INI or section, never its directory. Malformed input is refused.
    return save(path, section, changes, true);
}
inline bool saveKey(const char* path, const char* section, const char* key, const char* value) {
    if (!key || !value) { SetLastError(ERROR_INVALID_PARAMETER); return false; }
    return saveKeys(path, section, {{key, value}});
}
// Removes key's line from section, as WritePrivateProfileStringA(section, key, nullptr, path) does. A missing file,
// section or key is already removed (true); malformed files are refused as for saves.
inline bool eraseKey(const char* path, const char* section, const char* key) {
    if (!path || !section || !key) { SetLastError(ERROR_INVALID_PARAMETER); return false; }
    const std::lock_guard<std::mutex> guard(writerLock());
    if (GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES && GetLastError() == ERROR_FILE_NOT_FOUND) return true;
    return save(path, section, {{key, ""}}, false, true);
}
} // namespace dbce::settings
