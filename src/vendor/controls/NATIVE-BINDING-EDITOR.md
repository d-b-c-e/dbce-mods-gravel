# Native shared binding editor

This is an additive C++17 port of the managed binding capture/draft policy.
It opens no device, enumerates nothing, ticks no handle, sends no input/force,
and performs no UI work. `dbce_controls.hpp` still owns binding parse/format/fit.
The editing envelope supports Controls Schema 1; it does not invent measured
Schema 2 endpoints. Capture carries the actual read-back range, rest and travel;
inversion remains explicit. Runtime delivery uses the original active profile
until restart.

## Host contract

Include `dbce_binding_editor.hpp`; Windows explicit Save additionally includes
`dbce_controls_store_win.hpp`. Both use `dbce::controls::edit`.

1. Provide an `ActionKindPolicy(action, kind)`. For Obstacle, steering accepts
   Axis and pedals accept Axis or Button. Only the game owns delivered-action,
   foreground, menu, capture and context eligibility.
2. Copy `AcceptedObservation` immediately after the existing ordinary tick.
   Use snapshot3 only after its ABI/sizes/sequence and device-report identity are
   validated. Provide canonical nonzero instance/product GUIDs, interface identity,
   epoch/read sequence, local observation time, masks, raw state and ranges.
   Missing/stale/ended/closed/unknown input calls `remove` or `reader_stopped`.
   No accepted snapshot means capture unavailable. Only already-opened devices
   can be offered; there is no new-device discovery route here. A first device
   comes from shim extension 4 (`dbce_shim_candidate.h`): the host lists and
   selects it explicitly, then admits the candidate snapshot the same way and
   passes it here as one more observation. This header is unchanged.
3. `CaptureFlow.observe(o, now, canEdit)` accepts new observations; `devices(now)`
   lists fresh targets. `begin(action, instance, now, canEdit)` starts one bounded
   capture. Call `refresh(now, canEdit)` before drawing. Digital capture has a
   10-second bound; analog has a one-second rest and 11-second total bound.
   Freshness is 250 ms, sequences cannot refresh a cached read, and reconnects,
   changed metadata/ownership, ambiguity and lost focus cannot capture a binding.
4. Immediately call `take_result(now, canEdit)` after the accepted observation.
   Validate that candidate with `ProfileDraft.set` before displaying success.
   Candidates expire or disappear on eligibility loss. A refusal keeps prior draft.
5. `DeviceSelection` offers Saved/Explicit/Unavailable. Explicit selection retains
   exact identity, epoch and host; loss/replacement never silently picks another.
6. `ProfileDraft` preserves Active and Saved bytes separately. It imports strict
   UTF-8/BOM, duplicate-refusing Schema 1 and canonical known bindings, preserving
   unknown lines/sections and original line endings. Explicit Clear saves an empty
   `action =` row. `requireSteering` is optional and false for Obstacle; an entirely
   unbound profile cannot make capture devices appear. `discard` affects only local
   edits; `saved_pending_restart` distinguishes Saved from immutable Active.
7. On explicit Save call `save(draft, fixedAbsolutePath, ASCIIrevision)`. The host
   supplies its fixed normalized mod profile path; no UI value chooses another file.
   The Windows adapter refuses linked/nonregular paths, checks exact existing bytes,
   writes a unique new file and flushes, rechecks expected source and temporary bytes,
   uses ReplaceFile with an exact backup, then reads back new and backup bytes.
   Failure preserves draft and any backup; `replaced` distinguishes a post-replacement
   verification failure, which requires restart/reread. This is a guarded replacement
   sequence, not a cross-process compare-and-swap API. The helper never reloads the
   running input profile.

## Qualification

Product headers at `de17086859dd3c1e530f18d07d72f14c2694e7a4`; corrected fixture
source `a817ed7c8ade178edafdc1b2b1f2a3f928e8c41b`. MSVC x64 C++17 `/W4 /WX`
compiled with no warning/error lines, and **42 checks passed**. Coverage includes
held/released input, one-control ambiguity, range/mask refusal, stale/repeated reads,
epoch/host changes, focus loss, native steering rest/travel, canonical draft/Clear,
64 KiB section boundaries, UTF-8/duplicates, real disposable Windows replace/backup/
readback, external edit/lock refusal and immutable Active state.

Hub `artifacts/native-binding-editor-lola-20261017/Run-04.ps1` is the focused runner;
`build-04/result.json` and logs retain the pass. Original command setup failures
and the fixture-only C4310 diagnostic remain retained. The actual command-path
cause was a forward-slash cmd.exe argv0; native ComSpec fixed it. All seven owned
process identities closed, environment restored and exact shared lease released
at `2026-10-17T03:10:32.7422452Z`, directly to Puma before documentation.
No shim DLL was rebuilt/loaded, and no reader, device, UI, game or owner file was
used. Obstacle consumer integration has a separate affected qualification.
