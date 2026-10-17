# Private span and camera-settings package

The next private package combines the qualified span-maintenance fix with the
camera-step editor, using the existing `tools/Package.ps1` route. No new native
or installer behavior is introduced. Owner install and previous candidates stay
unchanged; this is not public distribution or live acceptance.

Native build source `0a47a2d24f8cd718b3f647784f9b8d38b6333e3e`, clean,
produced DLL SHA-256
`90EFCFA6CFA939003B058FE5B6F1F7E8F9417BF2B903C25AD9755441232F9AD6`.
The original build receipt remains in worktree
`vunu-gravel-span-maintenance-20261017/build/build-receipt.json`, SHA-256
`97C216C2F0734D445162A0526AF7245C3A06A75D86E5B31F40B690E4B4976606`.
Original passing result/logs remain in hub
`artifacts/gravel-span-vunu-20261017/run-01/`; result SHA-256
`613C6D5CCA1EC445026FCB619C706F82B8C179F25A16EEB3302AC67AAF36B78C`.
That batch passed all32 actual-source span cases and the existing native build,
including x64 PE, six proxy exports and compiler-runtime dependency checks.

Before explicit retained adoption, every original build-receipt source input
was compared with the package source and matched. The DLL was copied without
rebuilding and retained at `d2558a6eefa3357391245577fbb7b62371f94ca6`, Git blob
`4546c794a9512d48f01fe9787431ca497490c7cb`. The exact catalog/hash/history
entries in `tools/BinaryProvenance.psm1` and `dist/binary-provenance.json`
adopt those bytes together. This does not relax any provenance guard.

The September19 wheelprobe is retained unchanged, SHA-256
`4D9E94CC31F67F9644BFDED1847B94810A9F24CBEBA9CEE01661EBB361C6883A`.
The newly compiled wheelprobe mentioned in the original receipt is not used.
The existing retained schema keeps compiler/toolchain/build-receipt fields
null and reproducibility unknown; it does not manufacture new build evidence.
The original observed receipt above separately records the actual DLL build.

Camera-step source is `3650a60`, documented at `6eb480e`; existing39 focused
checks passed in both PowerShell5.1 and7. Those passing checks are retained.
No physical camera, display/scanout, driving or force acceptance is inferred.

## Package result

Packaging source `9db3bd573817d48b50dabf0117ee66997ec70175` produced private
`milestone-wheel-tools-0.2.0-9db3bd57.zip`, SHA-256
`CAF36D0822E6E6909692E663B6A9EE10C32B747B7B31667E149D68EEE1B13D72`.
ZIP, original stage, extraction and logs/result remain in this worktree's
`artifacts/private-package-20261017/run-01/` (ZIP under `package/`).

All43 existing provenance checks passed, then the existing real packager emitted
the candidate. Extraction passed the exact25-file manifest and retained-binary
validation. All243 existing installer/package checks passed against those
extracted bytes, using disposable targets only, including preservation of
settings/bindings, dry-run refusal, rollback/removal and modified-file retention.
No compiler, DLL/game, hardware, OS input/focus, owner state or exclusive rig
slot was used. No child or cleanup remains. Original native span and camera
checks were not repeated. This private package is ready for a separate bounded
runtime handoff; the installed owner package remains unchanged.
