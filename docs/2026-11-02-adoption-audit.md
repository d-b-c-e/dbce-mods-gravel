# Gravel standards audit and FOV-key repair — November 2

Kole497696; isolated worktree `E:/Source/_work/remi-gravel-adoption-audit-20261102`, based on local mainda6fd88.
Public repository: **local main only; nothing pushed**. No dist/package/install/owner-state change, no DLL/game launch,
device, display or input operation. The restored historical installation in `2026-11-01-mevu-handoff.md` remains
unchanged by this work; no fresh installed rehash or runtime acceptance is claimed.

All STD-038..049 ledger rows are now present. Existing native settings header matches recorded SHA256
`F1D161E3900AE66B9380DD1D5E5DA714EE7C9258AC0B6A6FD85AEFE9985052AF`, pin462680d. Source inspection confirms unique
CREATE_NEW staging/flush/atomic replace, guarded file identity and owned cleanup, with prior fixture evidence retained.
No additional small writer defect was established, so it was not rewritten or rebuilt for a new writer claim.

One real small gap was found: hardcoded g_camKeys mapped numpad+ to CamWider (positive FOV offset), and numpad− to
CamNarrower (negative), opposite STD-048. **25a70ad** swaps only those two defaults. No custom keymap exists in this
implementation, so no saved binding or config migration is needed. Existing step size, limits, reset, position keys,
framing/geometry/force settings and legacy F7 behavior are unchanged. The default correction is source-only here.

## Offline evidence

Code/fixture checkpoint **a106620** includes the NOMINMAX fixture header preparation. Existing Test-TripleSpan's
actual-source extraction approach was reused: `Test-CameraFovKeys.ps1` extracts production CameraOffsets, step
declarations, key array and applyCamera dispatcher. Its inert C++ stand-ins replace only memory locks. It calls no
window/display/input/device/game function and loads no proxy DLL.

- Baseline exact triple.cpp fromda6fd88:8 checks,3 expected failures (plus/minus/repeated-plus direction).
- Fixed actual dispatcher: **8 checks,0 failures**. Existing position/reset/unbound-key behavior is retained.
- `build.ps1` **without UpdateDist** passes native x64/export/import checks at clean committed sourcea106620
  (`build/build-receipt.json`, SourceDirty=false). Existing pragma-comment and wheelprobe strncpy warnings remain;
  zero-warning build is not claimed.
- Development dinput8.dll SHA256 **E6DAA565E2A424CED280091902C3EDAE580BB24E826F999278AC6EDE2E4828FC**.
- wheelprobe.exe SHA256 **5B890A07DE57ECCB0ADCDE5E98ED3FB7DCA13E498774EFC312CF5DE5F382C1CC**.
- Ignored evidence `build/remi-camera-keys/`: original triple.before.cpp, baseline/fixed extracted sources and
  result.json/result.log, baseline.log/fixed.log/native-build.log, final-result.json and frozen candidate-a106620.
  Original build receipt remains at `build/build-receipt.json`; a copy accompanies the frozen candidate.

GO497773; named remi leaseb4ba792e5f2e44afa4bef64cb35c6eb0 entered18:17:42.526-06:00 and released immediately after
the approximately25-second batch, before18:18:18.205. Own lease receipt absent; no process/compiler custody remains.
Build/test outputs stayed within the isolated tree. The retained dist files compare identical to the base checkout.
The latest owner assignment's source-only/no-dist rule supersedes the older CLAUDE instruction to update dist together
with a release. This change is not an installable release or admission of the obsolete F7 camera design.

The adopted STD-048 row means source/default behavior only, not physical-key or rendered acceptance. Pending per-camera
framing/curve/compositor/input/FFB/package gaps from the November1 handoff remain. Future coherent camera work must
resolve actual camera identity/consumer and current STD-041/046 before package/runtime; do not qualify the old F7 cycle
or promote owner Ready-for-Test rows from this focused fixture. No owner action or new rig interval is requested.
