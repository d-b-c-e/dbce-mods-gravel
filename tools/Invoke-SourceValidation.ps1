param([ValidateSet('powershell','pwsh')][string]$Engine = 'pwsh')
$ErrorActionPreference = 'Stop'
$command = Get-Command "$Engine.exe" -CommandType Application -ErrorAction Stop | Select-Object -First 1
$suites = @('Test-BinaryProvenance.ps1','Test-InstallPackage.ps1','Test-SetupUx.ps1')
foreach ($suite in $suites) {
    $path = Join-Path $PSScriptRoot "tests\$suite"
    if (-not [IO.File]::Exists($path)) { throw "Validation suite missing: $suite" }
    Write-Host "Running $suite with $Engine"
    # Each suite runs in its own process so fixture mocks and environment stay isolated.
    & $command.Source -NoProfile -NonInteractive -ExecutionPolicy Bypass -File $path
    if ($LASTEXITCODE -ne 0) { throw "Validation failed: $suite exited $LASTEXITCODE" }
}
Write-Host 'PASS: all source-validation suites completed successfully.'
