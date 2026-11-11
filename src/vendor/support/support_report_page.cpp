// SPDX-License-Identifier: MIT
// Submit issue on the F6 Help page: the host side of the shared native support report (support_report.hpp). The user
// writes a title and description, may select the one known mod log, reviews the complete redacted preview, and only then
// may open a prefilled GitHub draft (or first save a new .txt for attachment). Nothing is sent, saved or submitted
// automatically. The page is shared by the native proxy games; each passes its own name, version, repository and log.
#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>
#include "support_report_page.hpp"
#include "support_report.hpp"
#include "../panel/dbce_panel.h"
#include "../third_party/imgui/imgui.h"

namespace dbce::support {
namespace {

struct ReportUi {
    bool open = false, modLog = false, reviewed = false;
    char title[801]{}, description[40001]{};
    std::optional<PreparedReport> prepared;
    std::optional<BrowserSubmission> draft;
    std::string preview, error, savedPath;

    void invalidate() {
        prepared.reset(); draft.reset(); preview.clear(); savedPath.clear();
        reviewed = false; error.clear();
    }
    void fail(const std::exception &e) {
        invalidate(); error = e.what(); // Never log raw report content.
    }
};

std::wstring toWide(const std::string &text, UINT page) {
    if (text.empty()) return {};
    const DWORD flags = page == CP_UTF8 ? MB_ERR_INVALID_CHARS : 0;
    const int size = MultiByteToWideChar(page, flags, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (!size) throw std::runtime_error("Could not decode the report path or URL.");
    std::wstring result(static_cast<size_t>(size), L'\0');
    if (MultiByteToWideChar(page, flags, text.data(), static_cast<int>(text.size()), result.data(), size) != size)
        throw std::runtime_error("Could not decode the report path or URL.");
    return result;
}
std::string toUtf8(const std::wstring &text) {
    if (text.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (!size) throw std::runtime_error("Could not display the saved report path.");
    std::string result(static_cast<size_t>(size), '\0');
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), result.data(), size, nullptr, nullptr) != size)
        throw std::runtime_error("Could not display the saved report path.");
    return result;
}
std::filesystem::path knownPath(const char *path) {
    if (!path || !*path) throw std::runtime_error("The selected log location is not available.");
    // The mod's log path comes from Win32's A APIs; filesystem::path keeps native Unicode for the shared reader.
    return std::filesystem::path(toWide(path, CP_ACP));
}

bool saveReviewed(ReportUi &ui, const char *modLog) {
    wchar_t path[32768] = L"support-report.txt";
    const auto folder = knownPath(modLog).parent_path().native();
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof dialog;
    dialog.hwndOwner = static_cast<HWND>(ImGui::GetMainViewport()->PlatformHandleRaw);
    dialog.lpstrFilter = L"Text report (*.txt)\0*.txt\0\0";
    dialog.lpstrFile = path;
    dialog.nMaxFile = static_cast<DWORD>(sizeof path / sizeof path[0]);
    dialog.lpstrInitialDir = folder.c_str();
    dialog.lpstrTitle = L"Save reviewed report to a new text file";
    dialog.lpstrDefExt = L"txt";
    dialog.Flags = OFN_EXPLORER | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_NOTESTFILECREATE;
    if (!GetSaveFileNameW(&dialog)) {
        if (CommDlgExtendedError()) throw std::runtime_error("Could not open the report save dialog.");
        return false; // A canceled dialog changes neither the snapshot nor its review.
    }
    const auto displayPath = toUtf8(path);
    ui.prepared->write_review_file(std::filesystem::path(path)); // Shared CREATE_NEW export; never overwrite.
    ui.savedPath = displayPath;
    return true;
}
} // namespace

void DrawSupportReportPage(const char *product, const char *version, const char *repository, const char *modLog,
                           const char *logName) {
    using namespace dbce::panel;
    static ReportUi ui;
    const std::string includeLabel = std::string("Include ") + (logName ? logName : product) + " mod log (optional)";
    BeginGroup("help-report");
    if (Command("Submit issue")) ui.open = !ui.open;
    if (ui.open) {
        Text("Describe the problem and choose whether to include the mod log. Nothing is sent or saved when you review the report.");
        Text("Title");
        ImGui::SetNextItemWidth(-1.0f);
        bool changed = ImGui::InputText("##report-title", ui.title, sizeof ui.title);
        Text("What happened?");
        changed |= ImGui::InputTextMultiline("##report-description", ui.description, sizeof ui.description,
                                            ImVec2(ImGui::GetContentRegionAvail().x, 100.0f * Scale()));
        changed |= ImGui::Checkbox(includeLabel.c_str(), &ui.modLog);
        Text("Only this known text log can be selected. If it is missing or unreadable, uncheck it.");
        if (changed) ui.invalidate();

        if (Command("Review report")) {
            ui.invalidate();
            try {
                std::vector<std::filesystem::path> logs;
                if (ui.modLog) logs.push_back(knownPath(modLog));
                ui.prepared.emplace(prepare(ReportProduct(product, version, repository, std::string(product) + " F6"),
                                            ui.title, ui.description, logs));
                ui.preview = ui.prepared->preview();
            } catch (const std::exception &e) { ui.fail(e); }
        }

        if (ui.prepared) {
            Text("Review the complete preview. Redaction is best effort; unusual secrets or personal information can remain.");
            ImGui::BeginChild("report-preview", ImVec2(0, 180.0f * Scale()), ImGuiChildFlags_Borders);
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextUnformatted(ui.preview.c_str());
            ImGui::PopTextWrapPos();
            ImGui::EndChild();
            if (ImGui::Checkbox("I reviewed this exact report", &ui.reviewed)) {
                ui.draft.reset(); ui.savedPath.clear();
                if (ui.reviewed) {
                    try { ui.draft.emplace(ui.prepared->create_browser_submission(ui.reviewed)); }
                    catch (const std::exception &e) { ui.fail(e); }
                }
            }
        }
        if (ui.draft) {
            Text(ui.draft->instructions.c_str());
            if (ui.draft->needs_attachment) {
                Text("Choose a new .txt filename. Existing files are never overwritten. Save this snapshot, then attach that file in GitHub.");
                if (Command("Save reviewed report...")) {
                    try { (void)saveReviewed(ui, modLog); }
                    catch (const std::exception &e) { ui.fail(e); }
                }
                if (!ui.savedPath.empty()) Status("Saved report", ui.savedPath.c_str(), Tone::Good);
            }
            if (ui.draft) { // A failed save invalidates both the preview and review admission.
                Text("Opening the draft sends its prefilled text to GitHub. Attaching a file uploads it immediately. Check and submit the issue on GitHub yourself.");
                ImGui::BeginDisabled(ui.draft->needs_attachment && ui.savedPath.empty());
                if (Command("Open reviewed draft on GitHub")) {
                    try {
                        const auto url = toWide(ui.draft->url, CP_UTF8);
                        if (reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL)) <= 32)
                            throw std::runtime_error("Could not open the reviewed GitHub draft.");
                    } catch (const std::exception &e) { ui.fail(e); }
                }
                ImGui::EndDisabled();
            }
        }
        if (!ui.error.empty()) Status("Report", ui.error.c_str(), Tone::Error);
    }
    EndGroup();
}
} // namespace dbce::support
