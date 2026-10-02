# Gravel product contract

The organization documentation was published in commit
`d019d5a2468a1ee9c179b47bf5921e3eae0055c8`, based on main `cb7a4526`.
The canonical repository is [d-b-c-e/dbce-mods-gravel](https://github.com/d-b-c-e/dbce-mods-gravel).
The in-place rename and documentation integration are complete. Neither created
a product release. The installer ownership-safety follow-up is a review candidate.

## Repository and ownership

The existing `d-b-c-e/milestone-wheel-tools` repository was renamed in place
after independent review, retaining public repository ID 1351690526. Old web,
API and release URLs were verified to redirect. Post-action checks confirmed
main/setup refs and the v0.1.0 tag/release; the requested fresh preflight was
missed and those checks occurred afterward. Preserve history, branches, releases,
issues, license and shared ownership. Do not create a duplicate Gravel repository
or split the shared Milestone implementation. Retain motorcycle research code
and historical documentation; MXGP, MotoGP, Ride and Supercross are inactive and
excluded from the active portfolio. No new title support is implied.

The connected owner has admin permission. Remote main and the visible
`codex/simple-advanced-setup` branch were identical at this draft's baseline;
no open pull requests were returned. Local owner WIP and task ownership still
require coordination before integration. The guarded checkout remains untouched.

## One installation and setup

Gravel is the sole supported install target (Steam 558260, UE4.17).
One `dinput8.dll` contains the wheel-classification proxy, input/FFB observation,
FMOD tap, UE reflection and telemetry sender. The native wheelprobe helper belongs
to the same external setup payload, not a second game mod.

Extract the complete package and run `Install.bat` with Gravel closed.
Open `Wheel settings.bat`, bind required controls, Save mappings and exit, then
use Gravel's own calibration. Telemetry is optional and saved changes take
effect at the next launch. FFB and camera behavior remain game-owned.

## Compatibility and release identity

The reviewed organization change retained `VERSION` at 0.2.0. Preserve the existing
`milestone-wheel-tools` package Product, ZIP naming, `package-manifest.json`,
`milestone_mod.ini`, `milestone_mod.log`, `milestone_install.json`,
`DBCE-Wheel-Setup`, `DBCE-Wheel-Backups` and launcher names. Repository display
naming is separate from these installed compatibility contracts. Preserve
existing settings, receipt-owned file checks, closed-game guards and rollback.

`tools/Package.ps1` reads VERSION and a clean committed source identity, copies
the exact `Get-PackageFiles` allowlist, hashes every file, validates the manifest
and creates one ZIP. This document is source documentation; it is not added to
that allowlist. README remains part of the existing package. No binary rebuild
or release publication is proposed by this documentation change.

Keep the vendored wheel toolkit v0.8.0 pin. Shared toolkit fixes belong upstream;
do not modify vendored implementation merely to change repository branding.

## Evidence and feature boundaries

| Feature | Available evidence | Acceptance boundary |
|---|---|---|
| Wheel classification and bindings | August 30 MOZA R12 hardware notes; external setup fixture history | New setup/package physical acceptance pending |
| FFB | Native game effects pass through; proxy observes force magnitudes | No normalized/data-driven rig force implementation claimed |
| Telemetry | Historical live SimHub channels; Forza-compatible sender | Current package delivery and calibrated physical units not established |
| Triples | Optimizer records single-camera Surround research fallback | No verified three-view rendering; camera, HUD and effects work unproven |
| Recording | Shared Forza wire capture tooling can decode compatible packets | No Gravel-specific force replay or recorded-gameplay acceptance |

Deployment documentation records build 3477925 and source 3545810f on September
19; that is historical evidence, not a current installed-build inspection.
No game assets, owner configurations, recordings or generated game data belong
in this source draft or its package.

## Review and subsequent organization step

The two-file organization diff was independently reviewed and promoted to main
at `d019d5a` without forcing. Retain inactive motorcycle history and legacy
installer identity; do not repeat the rename. Review the ownership-safety candidate
separately before any further source integration or package publication.

Existing verification commands are `tools/tests/Test-SetupUx.ps1` and
`tools/tests/Test-InstallPackage.ps1`; they use disposable/synthetic fixtures.
They are not gameplay or physical force tests. The ownership-safety candidate
changes installer/removal checks, not the native runtime or toolkit pin. It
validates complete prior receipt path/hash sets and recognizes only the exact
known 0.1.0/0.2.0 proxy hashes without a receipt. Unknown/modified files block upgrades;
removal keeps edited owned files. Ownership snapshots are rechecked by the
transaction before writes; existing rollback and external-edit protections remain.
