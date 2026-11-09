# Native reviewed support reports

`support_report.hpp` / `support_report.cpp` are the Windows C++17 counterpart of
[`shared/support/SupportReport.cs`](../../shared/support/SupportReport.cs).
Vendor both exact files and the toolkit MIT license with source/hash provenance.
No managed runtime, game/provider, panel, HTTP client, browser, picker, clipboard
or process-launch dependency. Win32 is used only for strict Unicode conversion,
the current home path, bounded local-file IO and create-new export.

`support_desktop.hpp` (header-only, host side) is the one-press **Report a problem** action of the F6 Help page
(descriptor action `help.report`): `SaveReportToDesktop(game, version, repository, modLog, error)` prepares the
report with the mod's log, writes it to a new file on the Desktop and opens the repository's new-issue page. Link
`support_report.cpp`, `shell32.lib` and `ole32.lib`. It is kept out of `support_report.hpp`, which opens nothing.

The shared [host contract](../../shared/support/README.md) applies unchanged:
package known product/version/repository metadata; let the user explicitly
select known relevant text logs and enter a title/description; prepare only on
Review; show best-effort redaction wording and the complete preview. A host must
invalidate the snapshot and review flag on every input/selection change, and
clear an old preview when preparation fails. No automatic directory/config/save
collection or broad support ZIP is accepted by this API.

```cpp
#include "support_report.hpp"
using namespace dbce::support;
ReportProduct product("Packaged game", "1.2.3", "owner/repository");
auto report = prepare(product, title_utf8, description_utf8, selected_known_logs);
// Host displays report.preview(); input changes discard report/review state.
auto draft = report.create_browser_submission(user_reviewed_this_snapshot);
// Host shows draft.instructions. For attachment fallback it first asks for a
// fresh local .txt path and calls report.write_review_file(path).
// Only an explicit host action opens draft.url. The helper opens nothing.
```

`ReportProduct` exposes validated HTTPS `mod_page()` / `wheelkit_page()` links.
All public text is strict UTF-8. Paths are `std::filesystem::path` (use a native
wide path or `std::filesystem::u8path` for UTF-8 input). Optional explicit `home`
is useful for deterministic fixtures; absent means current `USERPROFILE`, empty
means no explicit home substitution (generic Windows user-path redaction stays).
`PreparedReport` owns private copies with const accessors and no setters. Its
URL and export methods never reread logs. Errors throw standard exceptions;
hosts show the message without logging raw report data.

Limits match managed behavior: title200, description10000, metadata120 and body
50000 UTF-16 code units; up to five logs share 24000 bytes of tails, plus four
encoding-prefix bytes per log. UTF-8 and BOM-marked UTF-16 LE/BE are strict;
UTF-32, incomplete characters and binary controls refuse. A truncated first line
is omitted before decoding, even if no complete line remains. Log headers use
basenames only; log fences/HTML delimiters are escaped. Unknown extensions,
duplicate normalized paths, UNC/device/alternate-stream paths and reparse points
refuse. Reserved DOS device components also refuse, even with extensions such
as `NUL.txt`; output is checked as a regular disk handle before any write.
Export requires an existing nonlinked local parent and `.txt`, uses
`CREATE_NEW` with no sharing, never creates folders or overwrites existing bytes.
An IO failure may leave a partial newly created file; it is never retried over.
Path checks are conservative, not a sandbox against concurrent parent replacement.

Redaction covers the same home/Windows user paths, GitHub token forms, credential
labels/line remainder and ASCII email forms as the managed fixtures. Bounded
scans avoid an uninterruptible C++ regex engine; this is best effort, not a proof
that arbitrary secrets are absent. Native redaction additionally refuses input
over1MiB or expanded output over1Mi UTF-16 units to bound allocations. Report
fields/logs have much tighter limits. URL percent escaping uses UTF-8 bytes and
the same unreserved set. The4096-character URL policy is local, not a promised
GitHub/browser maximum. A large report retains its full immutable local preview
and uses the managed short description/setup/one-attachment instructions.
Opening a draft transmits its prefilled text; attaching uploads immediately;
neither action submits an issue. Both actions remain with the host/user.

The native token-label parity successor also handles plain `token`,
`refresh_token` and `auth_token`, including quoted and mixed-case keys, just as
the managed helper does. It retains the label and redacts the line's value.
Source `5090bf9` passed the focused MSVC 19.44 Release `/W4 /WX /utf-8
/permissive-` build with zero warnings/errors and all94 native fixture checks
(no skips). Original stdout/stderr, JUnit and cleanup result are at
`E:/Source/dbce-project-mgmt/artifacts/native-support-token-mivu-20261017`.
The literal lease was released after verified scoped-process cleanup at
18:43:24.8880393Z; Mubi and Kobe received the direct handoff before docs.
Consumer adoption/packaging and rendered Help checks remain with their owners;
no owner install, runtime, issue submission or public push occurred.
Qualified successor `support_report.cpp` SHA-256 (normal checkout bytes):
`64630BEE1C01117800B939E0FBDD3E7750B8BBBBA4AD728774D972D5E9A4D0D6`;
Git blob `4e881d73f49f4e8e73f3fe82552adc8a1f2cb740`. The header is unchanged.

## Focused qualification

`test_support_report.cpp` ports the existing inputs/assertions from
`tests/SupportReport/Program.cs`: redaction, destination rejection, exact query
escaping, frozen preview, create-new export, five-log budget, split UTF tails,
binary refusal and useful long-draft fallback. Additional cases cover native
path/stream refusal, UTF-32/invalid surrogates/C1 controls, UTF-8 emoji, missing
parent/no folder creation and real symlink refusal when Windows permits creating
the fixture link. No game, device/provider, browser, network or GUI is opened.

Build only this target in a developer environment under the coordinated lease:

```text
cmake -S native/support -B build-native-support -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-native-support --target dbce_support_report_tests
ctest --test-dir build-native-support --output-on-failure
```

Initial implementation3ea0a9c: MSVC19.44 Release `/W4 /WX /utf-8 /permissive-`
build passed with zero warnings/errors; all85 checks passed, including real
symlink refusal (no skipped fixture). Original logs/result are
`E:/native-support-research/nemo-20261016-01`; cleanup.json separately closes the
exact owned idle compiler server and releases the lease. Review195592 then
required reserved-device export rejection; that source correction and its one
extra fixture are qualified at source `38a1d163a6d5bdb2fd1d92f7f14c98c8c6970066`:
the incremental library/fixture build passed with zero warnings/errors, all86
checks passed with no skips, and final source review195705 found no remaining
issue. Original result/stdout/stderr/JUnit are at
`E:/native-support-research/nemo-20261016-02`; all owned children were quiescent,
query complete and lease released/absent at21:24:26.3014298Z. Mako received the
direct handoff before documentation; ADRIFT owns the first adapter. Initial
passing result remains intact. No broad toolkit/panel suite, host browser/UI,
native game/provider load or consumer runtime acceptance follows.

Qualified source SHA-256 pins (normal repository bytes; preserve when vendoring):

- `support_report.hpp`: `C96C3B37A58E7AE0EDF519EE8EA3A74173E001083F56C10A58E35C1D17FB58F7`
- `support_report.cpp`: `63B778EADDA25043925F10BC75287858A7CF13CE71BCA1D675DE662829BD8C64`
