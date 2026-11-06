param([string]$SourceRoot = (Split-Path (Split-Path $PSScriptRoot -Parent) -Parent))
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $SourceRoot 'tools/BinaryProvenance.psm1') -Force
$fixture = Join-Path ([IO.Path]::GetTempPath()) ('gravel-provenance-' + [guid]::NewGuid().ToString('N'))
[IO.Directory]::CreateDirectory((Join-Path $fixture 'dist')) | Out-Null
foreach ($name in 'dinput8.dll','wheelprobe.exe','binary-provenance.json') { Copy-Item -LiteralPath (Join-Path $SourceRoot "dist/$name") -Destination (Join-Path $fixture "dist/$name") }
$checks = 0
function Assert($Value,[string]$Message) { if (-not $Value) { throw $Message }; $script:checks++ }
function Reject([scriptblock]$Action,[string]$Message) { $failed=$false; try { & $Action | Out-Null } catch { $failed=$true }; Assert $failed $Message }
function Copy-Object($Value) { $Value | ConvertTo-Json -Depth 10 | ConvertFrom-Json }
$provenance = Read-RetainedBinaryProvenance $fixture
$manifest = [pscustomobject]@{
    Product='milestone-wheel-tools';Version='0.2.0';SourceCommit=('a'*40);PackagingSourceCommit=('a'*40)
    CreatedUtc=[DateTime]::UtcNow.ToString('o');BinaryProvenance=$provenance
    Files=@($provenance.Binaries | ForEach-Object { [pscustomobject]@{Path=$_.Path;SHA256=$_.SHA256} })
}
Assert-PackageBinaryProvenance $fixture $manifest
Assert ($manifest.PackagingSourceCommit -cne $provenance.HistoricalSourceEvidenceCommit) 'Packaging source conflated with retained origin.'
foreach ($mutation in 'missing','kind','origin','compiler','toolchain','build-receipt','reproducibility','duplicate','missing-binary','path','hash','blob','history','extra-claim') {
    $bad = Copy-Object $manifest
    switch ($mutation) {
        'missing' { $bad.BinaryProvenance = $null }
        'kind' { $bad.BinaryProvenance.Kind = 'freshly-built' }
        'origin' { $bad.BinaryProvenance.HistoricalSourceEvidenceCommit = $bad.SourceCommit }
        'compiler' { $bad.BinaryProvenance.Compiler = 'GCC 16.2 inferred from docs' }
        'toolchain' { $bad.BinaryProvenance.Toolchain = 'inferred' }
        'build-receipt' { $bad.BinaryProvenance.BuildReceipt = 'fabricated receipt' }
        'reproducibility' { $bad.BinaryProvenance.Reproducibility = 'verified' }
        'duplicate' { $bad.BinaryProvenance.Binaries[1] = $bad.BinaryProvenance.Binaries[0] }
        'missing-binary' { $bad.BinaryProvenance.Binaries = @($bad.BinaryProvenance.Binaries[0]) }
        'path' { $bad.BinaryProvenance.Binaries[0].Path = '../owner-save.sav' }
        'hash' { $bad.BinaryProvenance.Binaries[0].SHA256 = 'b'*64 }
        'blob' { $bad.BinaryProvenance.Binaries[0].GitBlob = 'b'*40 }
        'history' { $bad.BinaryProvenance.Binaries[0].LastChangedCommit = $bad.SourceCommit }
        'extra-claim' { $bad.BinaryProvenance | Add-Member -NotePropertyName FreshlyBuilt -NotePropertyValue $true }
    }
    Reject { Assert-PackageBinaryProvenance $fixture $bad } "Accepted provenance mutation: $mutation"
}
foreach ($mutation in 'packaging-source','missing-source','file-hash','missing-file','extra-claim') {
    $bad = Copy-Object $manifest
    switch ($mutation) {
        'packaging-source' { $bad.PackagingSourceCommit = 'b'*40 }
        'missing-source' { $bad.SourceCommit = $null }
        'file-hash' { $bad.Files[0].SHA256 = 'b'*64 }
        'missing-file' { $bad.Files = @($bad.Files[0]) }
        'extra-claim' { $bad | Add-Member -NotePropertyName FreshlyBuilt -NotePropertyValue $true }
    }
    Reject { Assert-PackageBinaryProvenance $fixture $bad } "Accepted manifest provenance mutation: $mutation"
}
$receiptPath = Join-Path $fixture 'dist/binary-provenance.json'
$receiptBytes = [IO.File]::ReadAllBytes($receiptPath)
Remove-Item -LiteralPath $receiptPath
Reject { Read-RetainedBinaryProvenance $fixture } 'Absent provenance file accepted.'
[IO.File]::WriteAllText($receiptPath,'{malformed')
Reject { Read-RetainedBinaryProvenance $fixture } 'Malformed provenance JSON accepted.'
[IO.File]::WriteAllBytes($receiptPath,$receiptBytes)
foreach ($name in 'dinput8.dll','wheelprobe.exe') {
    $path=Join-Path $fixture "dist/$name"
    $bytes=[IO.File]::ReadAllBytes($path)
    [IO.File]::AppendAllText($path,'synthetic modification')
    Reject { Assert-PackageBinaryProvenance $fixture $manifest } "Modified $name was accepted."
    [IO.File]::WriteAllBytes($path,$bytes)
    Remove-Item -LiteralPath $path
    Reject { Assert-PackageBinaryProvenance $fixture $manifest } "Absent $name was accepted."
    [IO.File]::WriteAllBytes($path,$bytes)
}
# Exercise persistent receipt writing with inert synthetic bytes, not a compiler.
[IO.Directory]::CreateDirectory((Join-Path $fixture 'build')) | Out-Null
foreach ($name in 'dinput8.dll','wheelprobe.exe') { [IO.File]::WriteAllText((Join-Path $fixture "build/$name"),'inert synthetic output') }
[IO.File]::WriteAllText((Join-Path $fixture 'fixture.cpp'),'synthetic input')
$inputs=@([pscustomobject]@{Path='fixture.cpp';SHA256=(Get-ProvenanceHash (Join-Path $fixture 'fixture.cpp'))})
$tool=@{Version='synthetic test tool, not executed';SHA256=('a'*64)}
Write-ValidatedBuildReceipt -Root $fixture -SourceCommit ('a'*40) -SourceDirty $true -Compiler $tool -Inspector $tool -InputHashes $inputs
$observed = [IO.File]::ReadAllText((Join-Path $fixture 'build/build-receipt.json')) | ConvertFrom-Json
Assert ($observed.SourceDirty -and $observed.Reproducibility -ceq 'not tested') 'Synthetic receipt overstated source/reproducibility.'
Assert ($observed.Binaries[0].SHA256 -ceq (Get-ProvenanceHash (Join-Path $fixture $observed.Binaries[0].Path))) 'Persistent receipt hash mismatch.'
Reject { Assert-RetainedBinaryProvenance $fixture $observed } 'Observed build receipt silently adopted as retained provenance.'
Remove-Item -LiteralPath (Join-Path $fixture 'build/build-receipt.json')
[IO.File]::AppendAllText((Join-Path $fixture 'fixture.cpp'),'changed during synthetic build')
Reject { Write-ValidatedBuildReceipt -Root $fixture -SourceCommit ('a'*40) -SourceDirty $false -Compiler $tool -Inspector $tool -InputHashes $inputs } 'Changed inputs allowed a build receipt.'
Assert (-not [IO.File]::Exists((Join-Path $fixture 'build/build-receipt.json'))) 'Failed receipt writer left fresh evidence.'

# Run the actual packager over a disposable source copy. Git identity is a
# synthetic fixture; copied public binary bytes are never executed.
Import-Module (Join-Path $SourceRoot 'tools/InstallPackage.psm1') -Force
$packageSource = Join-Path $fixture 'package-source'
foreach ($relative in @(Get-PackageFiles) + @('tools/Package.ps1','tools/BinaryProvenance.psm1','dist/binary-provenance.json')) {
    $target = Join-Path $packageSource $relative
    [IO.Directory]::CreateDirectory((Split-Path $target -Parent)) | Out-Null
    Copy-Item -LiteralPath (Join-Path $SourceRoot $relative) -Destination $target
}
$fixtureGitState = @{Mode='clean';Reads=0;StatusReads=0}
function git {
    $global:LASTEXITCODE = 0
    if ($args -contains 'rev-parse') {
        $fixtureGitState.Reads++
        if ($fixtureGitState.Mode -eq 'head-change' -and $fixtureGitState.Reads -gt 1) { 'b'*40 } else { 'a'*40 }
    } elseif ($args -contains 'status') {
        $fixtureGitState.StatusReads++
        if ($fixtureGitState.Mode -eq 'status-error') { $global:LASTEXITCODE = 1 }
        elseif ($fixtureGitState.Mode -eq 'late-dirty' -and $fixtureGitState.StatusReads -gt 1) { ' M synthetic source' }
    } elseif ($args -contains 'merge-base') {
        if ($fixtureGitState.Mode -eq 'not-ancestor') { $global:LASTEXITCODE = 1 }
    } elseif ($args -contains 'ls-files') {
        $fixtureGitState.Inputs
    }
}
$output = Join-Path $fixture 'packages'
& (Join-Path $packageSource 'tools/Package.ps1') -OutputRoot $output | Out-Null
$made = Join-Path $output ('milestone-wheel-tools-0.2.0-' + ('a'*8))
$madeManifest = [IO.File]::ReadAllText((Join-Path $made 'package-manifest.json')) | ConvertFrom-Json
Assert-PackageManifest $made
Assert-PackageBinaryProvenance $made $madeManifest
Assert ([IO.File]::Exists("$made.zip")) 'Actual packager did not emit validated fixture ZIP.'
Assert ($madeManifest.BinaryProvenance.Kind -ceq 'retained' -and $null -eq $madeManifest.BinaryProvenance.Compiler) 'Actual packager presented retained bytes as freshly built.'
Remove-Item -LiteralPath (Join-Path $packageSource 'dist/binary-provenance.json')
$rejectedOutput = Join-Path $fixture 'missing-provenance-package'
Reject { & (Join-Path $packageSource 'tools/Package.ps1') -OutputRoot $rejectedOutput } 'Actual packager accepted absent provenance.'
Assert (-not (Test-Path -LiteralPath $rejectedOutput)) 'Missing provenance created package output.'
[IO.File]::WriteAllBytes((Join-Path $packageSource 'dist/binary-provenance.json'),$receiptBytes)
[IO.File]::AppendAllText((Join-Path $packageSource 'dist/wheelprobe.exe'),'synthetic changed helper')
Reject { & (Join-Path $packageSource 'tools/Package.ps1') -OutputRoot $rejectedOutput } 'Actual packager accepted mismatched helper bytes.'
Assert (-not (Test-Path -LiteralPath $rejectedOutput)) 'Mismatched bytes created package output.'
Copy-Item -LiteralPath (Join-Path $SourceRoot 'dist/wheelprobe.exe') -Destination (Join-Path $packageSource 'dist/wheelprobe.exe')
foreach ($mode in 'status-error','head-change','late-dirty') {
    $fixtureGitState.Mode=$mode; $fixtureGitState.Reads=0; $fixtureGitState.StatusReads=0
    $guardedOutput=Join-Path $fixture "guarded-$mode"
    Reject { & (Join-Path $packageSource 'tools/Package.ps1') -OutputRoot $guardedOutput } "Actual packager accepted $mode."
    $zips = if (Test-Path -LiteralPath $guardedOutput) { @(Get-ChildItem -LiteralPath $guardedOutput -Filter '*.zip' -Recurse) } else { @() }
    Assert ($zips.Count -eq 0) "Rejected $mode produced a ZIP."
}

# Observed-build adoption: build.ps1's own receipt of a clean commit, embedded
# whole by New-ObservedBinaryProvenance. Inert bytes; no compiler is executed.
$observedRoot = Join-Path $fixture 'observed-source'
foreach ($relative in @(Get-PackageFiles) + @('tools/Package.ps1','tools/BinaryProvenance.psm1','dist/binary-provenance.json')) {
    $target = Join-Path $observedRoot $relative
    [IO.Directory]::CreateDirectory((Split-Path $target -Parent)) | Out-Null
    Copy-Item -LiteralPath (Join-Path $packageSource $relative) -Destination $target
}
foreach ($dir in 'build','src') { [IO.Directory]::CreateDirectory((Join-Path $observedRoot $dir)) | Out-Null }
$observedInput = Join-Path $observedRoot 'src/fixture.cpp'
[IO.File]::WriteAllText($observedInput,'synthetic observed input')
foreach ($name in 'dinput8.dll','wheelprobe.exe') {
    [IO.File]::WriteAllText((Join-Path $observedRoot "build/$name"),"inert observed $name")
    Copy-Item -LiteralPath (Join-Path $observedRoot "build/$name") -Destination (Join-Path $observedRoot "dist/$name") -Force
}
$observedInputs=@([pscustomobject]@{Path='src/fixture.cpp';SHA256=(Get-ProvenanceHash $observedInput)})
Write-ValidatedBuildReceipt -Root $observedRoot -SourceCommit ('c'*40) -SourceDirty $true -Compiler $tool -Inspector $tool -InputHashes $observedInputs
Reject { New-ObservedBinaryProvenance $observedRoot } 'Dirty observed build was adopted.'
Assert ((Get-Content -LiteralPath (Join-Path $observedRoot 'dist/binary-provenance.json') -Raw) -match '"retained"') 'Refused dirty adoption replaced dist provenance.'
Write-ValidatedBuildReceipt -Root $observedRoot -SourceCommit ('c'*40) -SourceDirty $false -Compiler $tool -Inspector $tool -InputHashes $observedInputs
$null = New-ObservedBinaryProvenance $observedRoot
$read = Read-BinaryProvenance $observedRoot
Assert ($read.Kind -ceq 'observed-build' -and $read.BuildReceipt.SourceCommit -ceq ('c'*40) -and $read.Reproducibility -ceq 'not tested') 'Observed provenance did not round-trip.'
Reject { Read-RetainedBinaryProvenance $observedRoot } 'Observed provenance accepted as retained.'
Assert-ObservedBuildSource $observedRoot $read @('src/fixture.cpp')
$checks++
foreach ($mutation in 'no-receipt','schema','reproducibility','extra-claim','receipt-extra','dirty','dirty-string','receipt-kind','receipt-repro',
                      'adoption','commit','checks','compiler','no-inputs','input-path','receipt-binary','dist-hash','missing-binary','duplicate','path') {
    $bad = Copy-Object $read
    $r = $bad.BuildReceipt
    switch ($mutation) {
        'no-receipt' { $bad.BuildReceipt = $null }
        'schema' { $bad.SchemaVersion = 1 }
        'reproducibility' { $bad.Reproducibility = 'verified' }
        'extra-claim' { $bad | Add-Member -NotePropertyName FreshlyBuilt -NotePropertyValue $true }
        'receipt-extra' { $r | Add-Member -NotePropertyName Qualified -NotePropertyValue $true }
        'dirty' { $r.SourceDirty = $true }
        'dirty-string' { $r.SourceDirty = 'false' }
        'receipt-kind' { $r.Kind = 'retained' }
        'receipt-repro' { $r.Reproducibility = 'verified' }
        'adoption' { $r.PackageAdoption = 'approved' }
        'commit' { $r.SourceCommit = 'C'*40 }
        'checks' { $r.Checks = @('x64 PE') }
        'compiler' { $r.Compiler.SHA256 = 'not-a-hash' }
        'no-inputs' { $r.SourceInputs = @() }
        'input-path' { $r.SourceInputs[0].Path = '../owner.sav' }
        'receipt-binary' { $r.Binaries[0].SHA256 = 'b'*64 }
        'dist-hash' { $bad.Binaries[0].SHA256 = 'b'*64 }
        'missing-binary' { $bad.Binaries = @($bad.Binaries[0]) }
        'duplicate' { $bad.Binaries[1] = $bad.Binaries[0] }
        'path' { $bad.Binaries[0].Path = 'dist/other.dll' }
    }
    Reject { Assert-BinaryProvenance $observedRoot $bad } "Accepted observed provenance mutation: $mutation"
}
foreach ($name in 'dinput8.dll','wheelprobe.exe') {
    $path = Join-Path $observedRoot "dist/$name"
    $bytes = [IO.File]::ReadAllBytes($path)
    [IO.File]::AppendAllText($path,'synthetic modification')
    Reject { Read-BinaryProvenance $observedRoot } "Modified observed $name was accepted."
    [IO.File]::WriteAllBytes($path,$bytes)
}
Reject { Assert-ObservedBuildSource $observedRoot $read @('src/fixture.cpp','src/added.cpp') } 'Added native input accepted without rebuild.'
Reject { Assert-ObservedBuildSource $observedRoot $read @() } 'Empty native input list accepted.'
[IO.File]::AppendAllText($observedInput,'edited after build')
Reject { Assert-ObservedBuildSource $observedRoot $read @('src/fixture.cpp') } 'Edited native input accepted without rebuild.'
[IO.File]::WriteAllText($observedInput,'synthetic observed input')

# Actual packager over the observed source.
$fixtureGitState.Mode='clean'; $fixtureGitState.Reads=0; $fixtureGitState.StatusReads=0; $fixtureGitState.Inputs=@('src/fixture.cpp')
$observedOutput = Join-Path $fixture 'observed-packages'
& (Join-Path $observedRoot 'tools/Package.ps1') -OutputRoot $observedOutput | Out-Null
$observedMade = Join-Path $observedOutput ('milestone-wheel-tools-0.2.0-' + ('a'*8))
$observedManifest = [IO.File]::ReadAllText((Join-Path $observedMade 'package-manifest.json')) | ConvertFrom-Json
Assert-PackageManifest $observedMade
Assert-PackageBinaryProvenance $observedMade $observedManifest
Assert ([IO.File]::Exists("$observedMade.zip") -and $observedManifest.BinaryProvenance.Kind -ceq 'observed-build' -and
        $observedManifest.BinaryProvenance.BuildReceipt.SourceCommit -ceq ('c'*40)) 'Actual packager did not emit the observed-build package.'
foreach ($case in 'not-ancestor','added-input','edited-input') {
    $fixtureGitState.Mode='clean'; $fixtureGitState.Reads=0; $fixtureGitState.StatusReads=0; $fixtureGitState.Inputs=@('src/fixture.cpp')
    switch ($case) {
        'not-ancestor' { $fixtureGitState.Mode = 'not-ancestor' }
        'added-input' { $fixtureGitState.Inputs = @('src/fixture.cpp','src/added.cpp') }
        'edited-input' { [IO.File]::AppendAllText($observedInput,'edited after build') }
    }
    $guardedOutput = Join-Path $fixture "observed-guarded-$case"
    Reject { & (Join-Path $observedRoot 'tools/Package.ps1') -OutputRoot $guardedOutput } "Actual packager accepted observed $case."
    Assert (-not (Test-Path -LiteralPath $guardedOutput)) "Rejected observed $case created package output."
    [IO.File]::WriteAllText($observedInput,'synthetic observed input')
}
Write-Host "PASS: $checks provenance checks. Disposable bytes only; no compiler, game or hardware executed."
Write-Host "Fixture: $fixture"
