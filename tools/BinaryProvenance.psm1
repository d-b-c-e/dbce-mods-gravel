Set-StrictMode -Version Latest

function Get-ProvenanceHash([string]$Path) {
    (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
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
    $evidence = 'wheelprobe.exe: repository dist history and docs/DEPLOYMENT-2026-09-19.md; dinput8.dll: build.ps1 observed build of df3c771 (2026-10-07, local receipt); source-only recording candidate, no live qualification'
    # Reviewed public artifact identities, not inferred build/compiler attestation.
    $catalog = @{
        # 2026-10-07: sampled recording source candidate df3c771; not live-qualified (docs/RECORDING.md).
        'dist/dinput8.dll' = @('b35f4d0b795c3ca916f98e76cc95925042ff56f2260a0f9a6c8556c3c08f54aa','e1caab5bcc4976d87ef6408ffa8c748175a7ba15','640c3942c55199275f53ab5f61b5e10b72be706d')
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

function Assert-PackageBinaryProvenance([string]$Root, [object]$Manifest) {
    if ($null -eq $Manifest -or @(Compare-Object @($Manifest.PSObject.Properties.Name) @('Product','Version','SourceCommit','PackagingSourceCommit','BinaryProvenance','CreatedUtc','Files')).Count) { throw 'Package provenance schema fields are invalid.' }
    if ($Manifest.SourceCommit -notmatch '^[a-f0-9]{40}$' -or
        $Manifest.PackagingSourceCommit -cne $Manifest.SourceCommit) { throw 'Packaging-source identity is invalid.' }
    Assert-RetainedBinaryProvenance $Root $Manifest.BinaryProvenance
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

Export-ModuleMember -Function Get-ProvenanceHash,Read-RetainedBinaryProvenance,Assert-RetainedBinaryProvenance,Assert-PackageBinaryProvenance,Write-ValidatedBuildReceipt
