# Package and retained-binary provenance

October17 private successor explicitly adopts the already qualified span-fix
DLL, retaining the original wheelprobe. Exact build/receipt/result/hash/history
anchors and the separate camera-step source evidence are in
[the private package note](2026-10-17-private-package.md). The catalog remains
strictly pinned; older artifact details below are preserved as history.

2026-10-07 current **source-only review candidate**: `dist/dinput8.dll` is the
clean `df3c771` build, committed at `640c394` and adopted into the retained catalog.
SHA256 `B35F4D0B795C3CA916F98E76CC95925042FF56F2260A0F9A6C8556C3C08F54AA`,
blob `e1caab5bcc4976d87ef6408ffa8c748175a7ba15`. The ignored observed-build receipt
records compiler/inputs, six exports, x64 and no unbundled compiler dependency.
No install or runtime qualification; native recording/mute limitations are in
`docs/2026-10-07-recording-candidate.md`. The installed renderer and retained
wheelprobe are unchanged. Earlier artifact details below are historical.

The packaging-source commit is the clean HEAD used by `tools/Package.ps1`.
It identifies the scripts, setup and documentation being packaged, not the
compiler origin of copied native binaries. `SourceCommit` retains its historical
field name; `PackagingSourceCommit` explicitly states that same packaging identity.

2026-10-06: `dist/dinput8.dll` was rebuilt with triple screens and camera keys (docs/TRIPLES.md). The current
bytes are a `build.ps1` observed build of clean source `4b7fd75` (MSYS2 GCC 16.2.0; the receipt stays local in
ignored `build/`), committed in `75e4882` and adopted into the catalog in `tools/BinaryProvenance.psm1` (the first
triple build was `1d3d1e1`, committed in `f96fe25`). `dist/wheelprobe.exe` is unchanged.
The 0.2.0 binaries were retained artifacts, last changed together at
`3545810f4fd33647ff19ab87ab02a9aec0afa47a` on September 19, 2026:

| Path | SHA-256 | Git blob at the last-change commit |
|---|---|---|
| dist/dinput8.dll | d39ff1b8d4946cb145862350dfd89b1c571c9c29bbb7f4b68a178af3543f042b | e3ce6095f23533af248a359659f0860179c2f1c0 (at 75e4882) |
| dist/wheelprobe.exe | 4d9e94cc31f67f9644bfded1847b94810a9f24cbeba9cee01661ebb361c6883a | 2e0de26f48a0b003103d4f05891cbabd259ac2e5 |

Repository history and `DEPLOYMENT-2026-09-19.md` associate those artifacts with
that source commit. The deployment notes report GCC 16.2.0 and build checks;
no recovered machine-readable build receipt binds a compiler/toolchain identity
to the current bytes. Verified compiler, toolchain and build-receipt fields are
therefore null, and reproducibility is unknown. Neither a later source commit
nor passing installer tests establishes a fresh native build.

`dist/binary-provenance.json` is source-side metadata. The new packager requires
it, validates its exact reviewed retained-artifact identities against the two
actual binaries, and embeds it as `BinaryProvenance` in `package-manifest.json`.
It validates the copied payload again before ZIP creation. Missing, modified or
contradictory provenance is rejected; extra fresh-build claims are rejected.
The new provenance module and source-side metadata are not added to the installed
file allowlist. Installer/removal behavior, old package manifests, legacy product
names, version 0.2.0 and the wheel-toolkit v0.8.0 pin remain unchanged.

## Future build observations

After a future build passes x64, export and dependency checks, `build.ps1` writes
`build/build-receipt.json`: source commit/dirty state, tracked input hashes,
compiler and inspector version/executable hashes, output hashes and performed
checks. Inputs and tools are checked for changes during compilation. A previous
receipt is removed when a new compilation starts so a failed build does not
leave it masquerading as new evidence. No compiler was invoked to implement or
validate this candidate; only the pure receipt writer was tested with inert bytes.

An observed build receipt says reproducibility was not tested. It is audit
evidence, not automatic packaging authorization.

## Observed-build adoption (2026-11-05)

A new build can now be packaged without adding hashes to the retained catalog. The
retained catalog and its checks are unchanged and still apply to `Kind: retained`.

1. Commit the source, then run `.\build.ps1 -UpdateDist`. It refuses a dirty tree.
   After a passing build it copies both binaries into `dist/` and writes
   `dist/binary-provenance.json` with `SchemaVersion 2`, `Kind observed-build`, the
   whole `build/build-receipt.json` embedded as `BuildReceipt`, and the two dist hashes.
2. Review and commit `dist/`. That commit is the adoption.
3. `tools\Package.ps1` accepts the observed kind only if all of these hold:
   - the embedded receipt has exactly the receipt writer's fields, `SourceDirty false`,
     the three checks, tool hashes and `Reproducibility not tested`;
   - both dist binaries match the receipt's build outputs byte-for-byte;
   - the receipt's source commit is an ancestor of the packaging commit;
   - the packaging commit's tracked native inputs (`Get-BuildInputPathspec`) are
     exactly the receipt's input set and hashes. Any later edit to `src/`,
     `tools/wheelprobe`, `lib/toolkit/include`, `build.ps1` or this module
     needs a rebuild before packaging.

The installer is unchanged. It checks the package manifest hashes as before. An
observed build is not a reproducible build, and it is not live or physical acceptance.

## Verification

`tools/tests/Test-BinaryProvenance.ps1` uses disposable files and synthetic
metadata. It rejects absent/malformed receipts, missing/changed binaries,
wrong origins, hashes and Git blobs, duplicate/traversing paths, unsupported
fresh-build/compiler/reproducibility claims and mismatched manifest metadata.
Receipt-writing tests use fake compiler identities and inert outputs, explicitly
without invoking a compiler. The observed-build section adopts an inert clean
receipt, refuses a dirty one, rejects 20 receipt/provenance mutations and changed
dist bytes, and runs the actual packager. That run passes, then refuses a
non-ancestor build commit, an added native input and an edited native input,
leaving no output. Existing installer/setup fixture suites remain
separate evidence and do not establish binary build provenance.
