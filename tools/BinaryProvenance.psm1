Set-StrictMode -Version Latest

function Get-ProvenanceHash([string]$Path) {
    (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}

# One definition of the native build inputs, shared by build.ps1 (receipt) and
# Package.ps1 (proof that the packaged source still matches an observed build).
function Get-BuildInputPathspec { @('src','tools/wheelprobe','lib/toolkit/include','build.ps1','tools/BinaryProvenance.psm1') }

function Read-BinaryProvenance([string]$Root) {
    $path = Join-Path $Root 'dist/binary-provenance.json'
    if (-not [IO.File]::Exists($path)) { throw 'Binary provenance is missing; packaging refused.' }
    $text = [Text.UTF8Encoding]::new($false,$true).GetString([IO.File]::ReadAllBytes($path))
    $provenance = $text | ConvertFrom-Json
    Assert-BinaryProvenance $Root $provenance
    return $provenance
}

function Assert-BinaryProvenance([string]$Root, [object]$Provenance) {
    if ($null -ne $Provenance -and $Provenance.PSObject.Properties['Kind'] -and $Provenance.Kind -ceq 'observed-build') {
        Assert-ObservedBinaryProvenance $Root $Provenance
    } else { Assert-RetainedBinaryProvenance $Root $Provenance }
}

function Read-RetainedBinaryProvenance([string]$Root) {
    $path = Join-Path $Root 'dist/binary-provenance.json'
    if (-not [IO.File]::Exists($path)) { throw 'Binary provenance is missing; packaging refused.' }
    $text = [Text.UTF8Encoding]::new($false,$true).GetString([IO.File]::ReadAllBytes($path))
    $provenance = $text | ConvertFrom-Json
    Assert-RetainedBinaryProvenance $Root $provenance
    return $provenance
}

function Assert-RetainedBinaryProvenance([string]$Root, [object]$Provenance) {
    $origin = '3545810f4fd33647ff19ab87ab02a9aec0afa47a'
    $evidence = 'wheelprobe.exe: retained September 19 artifact, docs/DEPLOYMENT-2026-09-19.md; dinput8.dll: observed clean 0a47a2d build, retained at d2558a6; docs/2026-10-17-private-package.md; no live qualification'
    # Reviewed public artifact identities, not inferred build/compiler attestation.
    $catalog = @{
        # 2026-10-17: clean qualified span-fix build 0a47a2d; private retained adoption, no live qualification.
        'dist/dinput8.dll' = @('90efcfa6cfa939003b058fe5b6f1f7e8f9417bf2b903c25ad9755441232f9ad6','4546c794a9512d48f01fe9787431ca497490c7cb','d2558a6eefa3357391245577fbb7b62371f94ca6')
        'dist/wheelprobe.exe' = @('4d9e94cc31f67f9644bfded1847b94810a9f24cbeba9cee01661ebb361c6883a','2e0de26f48a0b003103d4f05891cbabd259ac2e5',$origin)
    }
    $fields = @('SchemaVersion','Kind','HistoricalSourceEvidenceCommit','Evidence','BuildReceipt','Compiler','Toolchain','Reproducibility','Binaries')
    if ($null -eq $Provenance -or @(Compare-Object @($Provenance.PSObject.Properties.Name) $fields).Count) { throw 'Binary provenance schema fields are invalid.' }
    if ($null -eq $Provenance -or $Provenance.SchemaVersion -ne 1 -or $Provenance.Kind -cne 'retained' -or
        $Provenance.HistoricalSourceEvidenceCommit -cne $origin -or
        $Provenance.Evidence -cne $evidence -or
        $null -ne $Provenance.BuildReceipt -or $null -ne $Provenance.Compiler -or $null -ne $Provenance.Toolchain -or
        $Provenance.Reproducibility -cne 'unknown') { throw 'Retained binary provenance identity/claims are invalid; no fresh-build claim is supported.' }
    $entries = @($Provenance.Binaries)
    if ($entries.Count -ne $catalog.Count) { throw 'Binary provenance must identify exactly both retained binaries.' }
    $seen = @{}
    foreach ($item in $entries) {
        if (@(Compare-Object @($item.PSObject.Properties.Name) @('Path','SHA256','GitBlob','LastChangedCommit')).Count) { throw 'Binary provenance entry fields are invalid.' }
        if ($item.Path -cnotin @($catalog.Keys) -or $seen.ContainsKey($item.Path)) { throw 'Unexpected or duplicate provenance path.' }
        $seen[$item.Path] = $true
        if ($item.SHA256 -cne $catalog[$item.Path][0] -or $item.GitBlob -cne $catalog[$item.Path][1] -or
            $item.LastChangedCommit -cne $catalog[$item.Path][2]) { throw 'Binary hash/history provenance mismatch.' }
        $path = Join-Path $Root $item.Path
        if (-not [IO.File]::Exists($path) -or (Get-ProvenanceHash $path) -cne $item.SHA256) { throw "Retained binary bytes mismatch: $($item.Path)" }
    }
}

function Test-ExactFields([object]$Value, [string[]]$Fields) {
    $null -ne $Value -and $Value -is [pscustomobject] -and -not @(Compare-Object @($Value.PSObject.Properties.Name) $Fields).Count
}

function Assert-ObservedBinaryProvenance([string]$Root, [object]$Provenance) {
    # An observed build is adopted only from build.ps1's own receipt of a clean
    # commit. The receipt is embedded whole; nothing is inferred or relabelled.
    if (-not (Test-ExactFields $Provenance @('SchemaVersion','Kind','Evidence','BuildReceipt','Reproducibility','Binaries'))) { throw 'Observed provenance schema fields are invalid.' }
    if ($Provenance.SchemaVersion -ne 2 -or $Provenance.Kind -cne 'observed-build' -or $Provenance.Reproducibility -cne 'not tested' -or
        $Provenance.Evidence -isnot [string] -or -not $Provenance.Evidence.Trim()) { throw 'Observed provenance identity/claims are invalid.' }
    $receipt = $Provenance.BuildReceipt
    if (-not (Test-ExactFields $receipt @('SchemaVersion','Kind','SourceCommit','SourceDirty','CreatedUtc','Compiler','Inspector','SourceInputs','Binaries','Checks','Reproducibility','PackageAdoption'))) { throw 'Embedded build receipt fields are invalid.' }
    if ($receipt.SchemaVersion -ne 1 -or $receipt.Kind -cne 'observed-build' -or $receipt.SourceCommit -cnotmatch '^[a-f0-9]{40}$' -or
        $receipt.SourceDirty -isnot [bool] -or $receipt.SourceDirty -or $receipt.Reproducibility -cne 'not tested' -or
        $receipt.PackageAdoption -cne 'requires independent provenance review' -or
        (@($receipt.Checks) -join '|') -cne 'x64 PE|six proxy exports|no unbundled compiler-runtime imports') { throw 'Embedded build receipt is not a clean observed build.' }
    foreach ($tool in $receipt.Compiler,$receipt.Inspector) {
        if (-not (Test-ExactFields $tool @('Version','SHA256')) -or $tool.Version -isnot [string] -or -not $tool.Version.Trim() -or
            $tool.SHA256 -cnotmatch '^[a-f0-9]{64}$') { throw 'Embedded build tool identity is incomplete.' }
    }
    $inputs = @($receipt.SourceInputs)
    if (-not $inputs.Count) { throw 'Embedded build receipt has no source inputs.' }
    $seen = @{}
    foreach ($item in $inputs) {
        if (-not (Test-ExactFields $item @('Path','SHA256')) -or $item.SHA256 -cnotmatch '^[a-f0-9]{64}$' -or $item.Path -isnot [string] -or
            $item.Path -cnotmatch '^[A-Za-z0-9_.\-]+(/[A-Za-z0-9_.\-]+)*$' -or $item.Path -match '(^|/)\.\.?(/|$)' -or
            $seen.ContainsKey($item.Path)) { throw 'Embedded build input entry is invalid.' }
        $seen[$item.Path] = $true
    }
    $built = @{}
    foreach ($item in @($receipt.Binaries)) {
        if (-not (Test-ExactFields $item @('Path','SHA256')) -or $item.Path -cnotin @('build/dinput8.dll','build/wheelprobe.exe') -or
            $built.ContainsKey($item.Path) -or $item.SHA256 -cnotmatch '^[a-f0-9]{64}$') { throw 'Embedded build output entry is invalid.' }
        $built[$item.Path] = $item.SHA256
    }
    if ($built.Count -ne 2) { throw 'Embedded build receipt must identify both build outputs.' }
    $entries = @($Provenance.Binaries)
    if ($entries.Count -ne 2) { throw 'Observed provenance must identify exactly both distributed binaries.' }
    $dist = @{}
    foreach ($item in $entries) {
        if (-not (Test-ExactFields $item @('Path','SHA256')) -or $item.Path -cnotin @('dist/dinput8.dll','dist/wheelprobe.exe') -or
            $dist.ContainsKey($item.Path)) { throw 'Unexpected or duplicate provenance path.' }
        $dist[$item.Path] = $true
        if ($item.SHA256 -cne $built['build/' + $item.Path.Substring(5)]) { throw 'Distributed binary is not the observed build output.' }
        $path = Join-Path $Root $item.Path
        if (-not [IO.File]::Exists($path) -or (Get-ProvenanceHash $path) -cne $item.SHA256) { throw "Observed binary bytes mismatch: $($item.Path)" }
    }
}

function Assert-ObservedBuildSource([string]$Root, [object]$Provenance, [string[]]$TrackedInputs) {
    # Package time only: the packaged commit's tracked native inputs must be the
    # exact set and bytes the observed build compiled.
    $recorded = @($Provenance.BuildReceipt.SourceInputs)
    $tracked = @($TrackedInputs | ForEach-Object { $_.Replace('\','/') } | Sort-Object -Unique -CaseSensitive)
    $expected = @($recorded | ForEach-Object { $_.Path } | Sort-Object -Unique -CaseSensitive)
    if (-not $tracked.Count -or ($tracked -join "`n") -cne ($expected -join "`n")) { throw 'Native build inputs changed since the observed build; rebuild before packaging.' }
    foreach ($item in $recorded) {
        $path = Join-Path $Root $item.Path
        if (-not [IO.File]::Exists($path) -or (Get-ProvenanceHash $path) -cne $item.SHA256) { throw "Native build input changed since the observed build: $($item.Path)" }
    }
}

function New-ObservedBinaryProvenance([string]$Root) {
    # build.ps1 -UpdateDist calls this after copying a clean build into dist/.
    $receiptPath = Join-Path $Root 'build/build-receipt.json'
    if (-not [IO.File]::Exists($receiptPath)) { throw 'Build receipt is missing; dist provenance not written.' }
    $receipt = [Text.UTF8Encoding]::new($false,$true).GetString([IO.File]::ReadAllBytes($receiptPath)) | ConvertFrom-Json
    $provenance = [pscustomobject]@{
        SchemaVersion=2; Kind='observed-build'
        Evidence='build.ps1 -UpdateDist receipt of the clean SourceCommit below; adopted by the reviewed commit of dist/ (docs/BINARY-PROVENANCE.md); no live qualification'
        BuildReceipt=$receipt; Reproducibility='not tested'
        Binaries=@(foreach ($name in 'dinput8.dll','wheelprobe.exe') { [pscustomobject]@{ Path="dist/$name"; SHA256=(Get-ProvenanceHash (Join-Path $Root "dist/$name")) } })
    }
    Assert-ObservedBinaryProvenance $Root $provenance
    $target = Join-Path $Root 'dist/binary-provenance.json'
    $temp = "$target.$([guid]::NewGuid().ToString('N')).tmp"
    [IO.File]::WriteAllText($temp,($provenance | ConvertTo-Json -Depth 8),[Text.UTF8Encoding]::new($false))
    Move-Item -LiteralPath $temp -Destination $target -Force
    return $provenance
}

function Assert-PackageBinaryProvenance([string]$Root, [object]$Manifest) {
    if ($null -eq $Manifest -or @(Compare-Object @($Manifest.PSObject.Properties.Name) @('Product','Version','SourceCommit','PackagingSourceCommit','BinaryProvenance','CreatedUtc','Files')).Count) { throw 'Package provenance schema fields are invalid.' }
    if ($Manifest.SourceCommit -notmatch '^[a-f0-9]{40}$' -or
        $Manifest.PackagingSourceCommit -cne $Manifest.SourceCommit) { throw 'Packaging-source identity is invalid.' }
    Assert-BinaryProvenance $Root $Manifest.BinaryProvenance
    foreach ($binary in $Manifest.BinaryProvenance.Binaries) {
        $file = @($Manifest.Files | Where-Object Path -CEQ $binary.Path)
        if ($file.Count -ne 1 -or $file[0].SHA256 -cne $binary.SHA256) { throw 'Package file/provenance hashes disagree.' }
    }
}

function Write-ValidatedBuildReceipt {
    param([string]$Root, [string]$SourceCommit, [bool]$SourceDirty,
          [object]$Compiler, [object]$Inspector, [object[]]$InputHashes)
    if ($SourceCommit -notmatch '^[a-f0-9]{40}$' -or -not $InputHashes.Count) { throw 'Build source identity is missing.' }
    foreach ($tool in $Compiler,$Inspector) {
        if (-not $tool.Version -or $tool.SHA256 -notmatch '^[a-f0-9]{64}$') { throw 'Observed build tool identity is incomplete.' }
    }
    foreach ($inputFile in $InputHashes) {
        if ((Get-ProvenanceHash (Join-Path $Root $inputFile.Path)) -cne $inputFile.SHA256) { throw 'Build inputs changed during compilation; no receipt written.' }
    }
    $binaries = @(foreach ($name in 'dinput8.dll','wheelprobe.exe') {
        [pscustomobject]@{ Path="build/$name"; SHA256=(Get-ProvenanceHash (Join-Path $Root "build/$name")) }
    })
    $receipt = [pscustomobject]@{
        SchemaVersion=1; Kind='observed-build'; SourceCommit=$SourceCommit; SourceDirty=$SourceDirty
        CreatedUtc=[DateTime]::UtcNow.ToString('o'); Compiler=$Compiler; Inspector=$Inspector
        SourceInputs=$InputHashes; Binaries=$binaries
        Checks=@('x64 PE','six proxy exports','no unbundled compiler-runtime imports')
        Reproducibility='not tested'; PackageAdoption='requires independent provenance review'
    }
    [IO.File]::WriteAllText((Join-Path $Root 'build/build-receipt.json'),($receipt | ConvertTo-Json -Depth 8),[Text.UTF8Encoding]::new($false))
}

Export-ModuleMember -Function Get-ProvenanceHash,Get-BuildInputPathspec,Read-BinaryProvenance,Assert-BinaryProvenance,Assert-ObservedBinaryProvenance,Assert-ObservedBuildSource,New-ObservedBinaryProvenance,Read-RetainedBinaryProvenance,Assert-RetainedBinaryProvenance,Assert-PackageBinaryProvenance,Write-ValidatedBuildReceipt
