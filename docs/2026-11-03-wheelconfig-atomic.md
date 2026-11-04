# Gravel WheelConfig CLI owned-value saves

Assignment509149; selected `tools/wheelconfig.py` after running the standards
backlog and checking ownership. Clean canonical baseline d951399. Gravel native
view/camera writer already has separate atomic-editor proof; Woden/Art/FH2 scalar
writers also have retained proof or broader full-writer/runtime gaps. This is a
bounded repair of the documented Python CLI, not an F6/native-camera change.

Source984197f430d5f6145d6b06de23eb5833c88edd53. The CLI now refuses damaged UTF-8,
unsupported UTF-16/NUL/oversize, malformed syntax, ambiguous target/template
sections and duplicate owned keys. UTF-8 BOM, per-line LF/CRLF, key spelling,
spacing, comments and all bytes outside requested values remain intact. Its
existing CRLF-input contract remains; LF-only and bare-CR files refuse unchanged.

An existing target updates only value spans. Clone creation keeps the entire
original byte prefix, including EOF and extra blank lines; it appends a new
owned section. Existing .bak files are never overwritten: first clone uses an
exclusive .bak, later backups have a unique suffix. Backups are byte copies,
not an admitted full owner-state restoration route.

Save uses an exclusive same-directory temp, flush/fsync, source identity/content
recheck and existing-file atomic replacement (ReplaceFileW on Windows, like
native/settings). Owned temp cleanup on failure; no truncation, directories or
linked/hardlinked/read-only targets. No-op avoids replacement/timestamp churn.
Independently renaming external editors remain excluded; no cross-process
transaction or live closed-game detector is claimed. Run only with game/editors
closed as before. Existing mapping/polarity/rotation/FFB defaults remain.

Native source, wheel setup UI, DLLs, dist, packages, installers and owner files
are untouched. No game, Steam, real device/input/FFB or UI action. Source-only,
local commits; no restricted public push or install.

Focused CPU tuple at draft time: source984197f, plan
D02953A21BF69FA8E286F13126CA399B5F678E2D2F86F939404737469C5E829C,
runner2F6C111E5FBBA4DDDF285ED0CDE47FD9DA1391F7C96DBB0429F378FEFC5A6624,
16 pins,90seconds/15reserve/one Python worker/zero compiler. Five direct held
Python roots: private fixture creation, exact old CLI, fixed CLI, differential
verification,15 production-function cases. CPU509377 per509425 completed after soba FINAL509446: PASS in2.4053176 seconds. The actual old CLI lost the owned inline comment as expected; the fixed actual CLI matched independently constructed exact bytes. All15 production-function test cases passed,0 skipped, including the real Win32 replacement lock and readonly refusal.

Evidence stays in this worktree `build/wheelconfig-qualification`. The source
native-camera historical proof is retained without rerunning unchanged code.
The current-toolkit adoption question is proposal-only in hub
`docs/handoffs/2026-11-03-napo-toolkit-atomic-ini-proposal.md`; no current toolkit
main change. STD-044 remains partial for the broader player UI/runtime and
other writer/adoption gates.


## Exact evidence and release

Evidence: `build/wheelconfig-qualification/cpu01`. Python3.12.10 x64; no compiler/native build.
Result SHA256 `8E1B7DA4C97760D6EF0CA3C6FD17B09C507782D73336C3BDCF99F6101D623BCE`.
Independent ALL-image scoped audit SHA256 `A841F9E65228E4D184122C93DA3A2591758019EBA73E9EEE6AF4341E6F5741E8`
passed before exact lease exit. Five explicit held Python roots,10 observed job identities
(including console infrastructure), all normal exits, no forced stop, empty job, retainedhandles0.
No lease/CPU/live custody remains. Keep original raw baseline/fixed files, logs and receipts.

Only docs/ledger changes follow tested984197f; native/dist/package/install files are unchanged
from d951399. Source/fixture proof is not real wheel-profile consumption or physical acceptance.
