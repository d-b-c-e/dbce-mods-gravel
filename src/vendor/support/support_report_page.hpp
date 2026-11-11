// SPDX-License-Identifier: MIT
#pragma once

// The F6 panel's Help "Submit issue" page for native games, drawn with Dear ImGui and the shared panel (needs
// ../panel/dbce_panel.h and ../third_party/imgui; support_report.hpp itself stays free of both). The player writes a
// title and description, may include the one known mod log, reviews the complete redacted preview, and only then may
// open a prefilled GitHub draft (or first save a new .txt for attachment). Nothing is read, saved, sent or opened
// without that explicit action. Call it from the panel's Help page; the page keeps its own state between frames.
namespace dbce::support {
// product:   the game's name as the player sees it ("DiRT 3"); also the report's game and the "<product> F6" client label.
// version:   the mod version.   repository: "d-b-c-e/dbce-mods-<game>".   modLog: path of the mod's log file.
// logName:   names the log in the checkbox ("Include <logName> mod log (optional)"); null means product.
void DrawSupportReportPage(const char *product, const char *version, const char *repository, const char *modLog,
                           const char *logName = nullptr);
} // namespace dbce::support
