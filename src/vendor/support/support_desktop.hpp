// Report a problem, the one-press host action of the F6 Help page (descriptor action help.report, UX-1): prepares the
// redacted support report with the mod's log (support_report.hpp), writes it to a new file on the Desktop (never
// overwriting one) and opens the mod's new-issue page. The player reads the file and attaches it there themselves;
// nothing is uploaded or submitted. Header-only; the host links support_report.cpp, shell32 and ole32 (SHGetFolderPath).
// First used by DiRT 4 (0.2.7); the same steps as GRID (2019)'s grid_report.cpp.
#pragma once
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <wchar.h>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>
#include "support_report.hpp"

namespace dbce { namespace support {

// game: the product's short name ("DiRT 4"); repository: "owner/name"; modLog: the mod's log path from the Win32 A
// APIs, or null/empty for none. False with a short reason for the player (never raw report content) when a step fails.
inline bool SaveReportToDesktop(const char *game, const char *version, const char *repository, const char *modLog,
                                std::string &error)
{
    try {
        auto wide = [](const std::string &text, UINT page) {
            if (text.empty()) return std::wstring();
            const DWORD flags = page == CP_UTF8 ? MB_ERR_INVALID_CHARS : 0;
            const int size = MultiByteToWideChar(page, flags, text.data(), static_cast<int>(text.size()), nullptr, 0);
            std::wstring result(static_cast<size_t>(size > 0 ? size : 0), L'\0');
            if (size <= 0 || MultiByteToWideChar(page, flags, text.data(), static_cast<int>(text.size()), result.data(), size) != size)
                throw std::runtime_error("Could not decode the report path or URL.");
            return result;
        };
        wchar_t desktop[MAX_PATH];
        if (FAILED(SHGetFolderPathW(nullptr, CSIDL_DESKTOPDIRECTORY, nullptr, SHGFP_TYPE_CURRENT, desktop)))
            throw std::runtime_error("The Desktop folder was not found.");
        std::vector<std::filesystem::path> logs;
        if (modLog && *modLog) logs.push_back(std::filesystem::path(wide(modLog, CP_ACP)));
        const std::string name(game ? game : "Game");
        const auto report = prepare(ReportProduct(name, version ? version : "", repository ? repository : "", name + " F6"),
                                    name + " wheel mod problem", "Describe what happened here.", logs);
        SYSTEMTIME t;
        GetLocalTime(&t);
        wchar_t stamp[32];
        swprintf_s(stamp, L" %04u-%02u-%02u %02u%02u%02u.txt", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
        report.write_review_file(std::filesystem::path(desktop) / (wide(name, CP_UTF8) + L" mod report" + stamp));   // CREATE_NEW
        const std::wstring page = L"https://github.com/" + wide(repository ? repository : "", CP_UTF8) + L"/issues/new";
        if (reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", page.c_str(), nullptr, nullptr, SW_SHOWNORMAL)) <= 32)
            throw std::runtime_error("The report is on your Desktop, but the mod's page did not open.");
        return true;
    } catch (const std::exception &e) {
        error = e.what();
        return false;
    }
}

}} // namespace dbce::support
