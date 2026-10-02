param()
$ErrorActionPreference = 'Stop'
$root = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
Import-Module (Join-Path $root 'tools/ci/SourceValidationPolicy.psm1') -Force
Import-Module (Join-Path $root 'tools/InstallPackage.psm1') -Force
$checks = 0
function Assert($Value,[string]$Message) { if (-not $Value) {throw $Message}; $script:checks++ }
function Reject([scriptblock]$Action,[string]$Message) { $failed=$false; try {& $Action | Out-Null} catch {$failed=$true}; Assert $failed $Message }
$workflowPath=Join-Path $root '.github/workflows/source-validation.yml'
$workflowFiles=@(Get-ChildItem -LiteralPath (Split-Path $workflowPath -Parent) -File | Where-Object { $_.Extension -in @('.yml','.yaml') })
Assert ($workflowFiles.Count -eq 1 -and $workflowFiles[0].Name -ceq 'source-validation.yml') 'An unreviewed additional workflow exists.'
$workflow=[Text.UTF8Encoding]::new($false,$true).GetString([IO.File]::ReadAllBytes($workflowPath))
Assert-SourceValidationWorkflow $workflow
Assert ($workflow -notmatch '[\x00-\x08\x0B\x0C\x0E-\x1F\x7F\uFFFD]') 'Workflow text is corrupted.'
foreach ($mutation in 'write-token','artifact-upload','secret','native-build','game-download','game-launch','skip-failure','continue-error','persistent-token','extra-job') {
    $bad = switch ($mutation) {
        'write-token' { $workflow.Replace('contents: read','contents: write') }
        'artifact-upload' { $workflow + "`n      - uses: actions/upload-artifact@v4`n" }
        'secret' { $workflow + '`n    env: {TOKEN: ${{ secrets.PUBLISH_TOKEN }}}' }
        'native-build' { $workflow.Replace("if (-not `$?) { exit 1 }",'& .\build.ps1 -UpdateDist') }
        'game-download' { $workflow + "`n      - run: Invoke-WebRequest https://example.invalid/game.zip`n" }
        'game-launch' { $workflow + "`n      - run: Start-Process gravel-Win64-Shipping.exe`n" }
        'skip-failure' { $workflow.Replace("if (-not `$?) { exit 1 }",'exit 0') }
        'continue-error' { $workflow.Replace('shell: pwsh',"continue-on-error: true`n        shell: pwsh") }
        'persistent-token' { $workflow.Replace('persist-credentials: false','persist-credentials: true') }
        'extra-job' { $workflow + "`n  publish:`n    runs-on: windows-2022`n" }
    }
    Reject { Assert-SourceValidationWorkflow $bad } "Workflow policy accepted $mutation"
}
Assert-SourcePackageSafety @(Get-PackageFiles)
foreach ($privatePath in 'games/gravel/settings.sav','game.iso','generated-game.bin','recordings/owner.json','milestone_mod.log','milestone_install.json') {
    Reject { Assert-SourcePackageSafety (@(Get-PackageFiles) + @($privatePath)) } "Private asset policy accepted $privatePath"
}
# Test the actual runner in child processes with disposable inert suites.
# Deliberately failing each position must stay nonzero and skip later suites.
$fixture=Join-Path ([IO.Path]::GetTempPath()) ('gravel-ci-runner-'+[guid]::NewGuid().ToString('N'))
[IO.Directory]::CreateDirectory($fixture) | Out-Null
$shell = if ($PSVersionTable.PSEdition -eq 'Desktop') {'powershell'} else {'pwsh'}
$command=Get-Command "$shell.exe" -CommandType Application
$suiteNames=@('Test-CiWorkflow.ps1','Test-BinaryProvenance.ps1','Test-InstallPackage.ps1','Test-SetupUx.ps1')
$marker=Join-Path $fixture 'later-suite-ran.txt'
function Invoke-FixtureRunner([string]$LogPath) {
    # Windows PowerShell turns expected child stderr into NativeCommandError.
    # Relax only this negative-test invocation; assert its captured exit below.
    $savedPreference=$ErrorActionPreference
    try {
        $ErrorActionPreference='Continue'
        & $command.Source -NoProfile -NonInteractive -ExecutionPolicy Bypass -File (Join-Path $root 'tools/Invoke-SourceValidation.ps1') -Engine $shell -SuiteRoot $fixture *> $LogPath
        return $LASTEXITCODE
    } finally { $ErrorActionPreference=$savedPreference }
}
foreach ($failureIndex in 0..3) {
    if ([IO.File]::Exists($marker)) {Remove-Item -LiteralPath $marker}
    foreach ($index in 0..3) {
        $text = if ($index -eq $failureIndex) {'exit 23'} elseif ($index -gt $failureIndex) {"[IO.File]::WriteAllText('$marker','unexpected later suite'); exit 0"} else {'exit 0'}
        [IO.File]::WriteAllText((Join-Path $fixture $suiteNames[$index]),$text)
    }
    $code=Invoke-FixtureRunner (Join-Path $fixture "failure-$failureIndex.log")
    Assert ($code -ne 0) "Runner swallowed failure at suite $failureIndex"
    Assert (-not [IO.File]::Exists($marker)) "Runner continued after failed suite $failureIndex"
}
foreach ($suite in $suiteNames) {[IO.File]::WriteAllText((Join-Path $fixture $suite),'exit 0')}
$code=Invoke-FixtureRunner (Join-Path $fixture 'success.log')
Assert ($code -eq 0) 'Runner rejected successful disposable suites.'
Remove-Item -LiteralPath (Join-Path $fixture $suiteNames[-1])
$code=Invoke-FixtureRunner (Join-Path $fixture 'missing-suite.log')
Assert ($code -ne 0) 'Runner accepted missing suite.'
Write-Host "PASS: $checks CI policy/failure-propagation checks. No uploads, secrets, game or native build."
Write-Host "Fixture: $fixture"
