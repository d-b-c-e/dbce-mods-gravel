# Source validation

`.github/workflows/source-validation.yml` runs only by manual `workflow_dispatch`. It calls
`tools/Invoke-SourceValidation.ps1` once with Windows PowerShell and once with pwsh. The runner starts each suite in
its own shell and stops on the first non-zero exit. The suites use disposable fixtures only: no native build, game or
device is touched.

Run the same check locally with `.\tools\Invoke-SourceValidation.ps1 -Engine pwsh` (or `-Engine powershell`).
