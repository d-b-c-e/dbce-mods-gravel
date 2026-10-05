param(
    [ValidateSet('powershell','pwsh')][string]$Engine = 'pwsh',
    [string]$SuiteRoot = (Join-Path $PSScriptRoot 'tests')
)
$ErrorActionPreference = 'Stop'
# SuiteRoot permits disposable runner-failure fixtures. CI uses only the default.
$command = Get-Command "$Engine.exe" -CommandType Application -ErrorAction Stop
$suites = @('Test-CiWorkflow.ps1','Test-BinaryProvenance.ps1','Test-InstallPackage.ps1','Test-SetupUx.ps1')
foreach ($suite in $suites) {
    $path = Join-Path $SuiteRoot $suite
    if (-not [IO.File]::Exists($path)) { throw "Validation suite missing: $suite" }
    Write-Host "Running $suite with $Engine"
    # Child processes isolate fixture mocks/environment. Do not infer success
    # from output, pipeline status or a later successful command.
    & $command.Source -NoProfile -NonInteractive -ExecutionPolicy Bypass -File $path
    $exitCode = $LASTEXITCODE
    if ($exitCode -ne 0) { throw "Validation failed: $suite exited $exitCode" }
}
Write-Host 'PASS: all source-validation suites completed successfully.'
