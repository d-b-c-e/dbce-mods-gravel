# Package and retained-binary provenance

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
evidence, not automatic packaging authorization. This candidate supports retained
provenance only. Rebuilt bytes require a separately reviewed provenance adoption;
`-UpdateDist` does not rewrite the retained catalog or manufacture a fresh-build
claim. No rebuild reproducibility or game/physical acceptance is claimed.

## Verification

`tools/tests/Test-BinaryProvenance.ps1` uses disposable files and synthetic
metadata. It rejects absent/malformed receipts, missing/changed binaries,
wrong origins, hashes and Git blobs, duplicate/traversing paths, unsupported
fresh-build/compiler/reproducibility claims and mismatched manifest metadata.
Receipt-writing tests use fake compiler identities and inert outputs, explicitly
without invoking a compiler. Existing installer/setup fixture suites remain
separate evidence and do not establish binary build provenance.
