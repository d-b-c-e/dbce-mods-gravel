# Source-validation CI review candidate

The workflow runs the existing disposable source suites on `windows-2022`
using Windows PowerShell and pwsh as separate matrix entries. A pwsh step calls
`tools/Invoke-SourceValidation.ps1`, which launches each suite in a fresh selected
shell, checks its exit code immediately and stops on any nonzero result or
missing suite. A later passing command cannot hide a previous suite failure.

Suites are CI policy/failure propagation, retained binary provenance, installer
ownership/package fixtures and external setup/migration fixtures. The setup
suite compiles and runs an inert synthetic C# input reader with mocked discovery;
it does not execute the distributed native input reader or open a device. The
provenance suite exercises packaging in a disposable copy with synthetic Git
identity, not a published artifact. Native game inputs are never built.

The job has only `contents: read`, uses an exact official checkout v4.2.2 commit
and disables persisted checkout credentials. It requires no configured secrets.
There are no game downloads, installed-game execution, native builds, uploads,
release publication, branch-protection changes or account-setting operations.
Fixture files stay on the ephemeral runner; they are not uploaded.

The local workflow guard accepts only the reviewed workflow structure and a
single workflow file. Additional actions/jobs/commands, write permission, secret
references, artifact upload, native build/game operations, skipped error exits
and continue-on-error require a policy change and independent review. Package
allowlist checks reject private saves, generated game data, recordings and
installed receipts/logs. Disposable runner tests explicitly fail every suite
position and assert nonzero termination with no subsequent execution.

Local source-suite success is not hosted-CI success. This candidate is held
before publication/triggering for independent review. Publishing the workflow
later will enable push, pull-request and manual validation; verify actual matrix
results before claiming CI passes. It supplies source/setup evidence only, not
fresh binary-build provenance, reproducibility, gameplay or physical acceptance.
