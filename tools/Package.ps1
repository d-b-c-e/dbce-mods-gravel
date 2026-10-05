param([string]$OutputRoot = (Join-Path (Split-Path $PSScriptRoot -Parent) 'artifacts'))
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
Import-Module (Join-Path $PSScriptRoot 'InstallPackage.psm1') -Force
Import-Module (Join-Path $PSScriptRoot 'BinaryProvenance.psm1') -Force
$version = ([IO.File]::ReadAllText((Join-Path $root 'VERSION'))).Trim()
$source = (& git -C $root rev-parse HEAD).Trim()
if ($LASTEXITCODE) { throw 'Source commit could not be read.' }
$sourceState = @(& git -C $root status --porcelain --untracked-files=no)
if ($LASTEXITCODE) { throw 'Packaging source state could not be read.' }
if ($sourceState.Count) { throw 'Commit the built binaries and source before packaging.' }
$provenance = Read-RetainedBinaryProvenance $root
$name = "milestone-wheel-tools-$version-$($source.Substring(0,8))"
$destination = Join-Path $OutputRoot $name
if (Test-Path -LiteralPath $destination) { throw "Package already exists: $destination" }
[IO.Directory]::CreateDirectory($destination) | Out-Null
$files = @(Get-PackageFiles)
$manifest = @()
foreach ($relative in ($files | Sort-Object -Unique)) {
    $from = Join-Path $root $relative
    if (-not (Test-Path -LiteralPath $from)) { throw "Package input missing: $relative" }
    $to = Join-Path $destination $relative
    [IO.Directory]::CreateDirectory((Split-Path $to -Parent)) | Out-Null
    Copy-Item -LiteralPath $from -Destination $to
    $manifest += [pscustomobject]@{ Path=$relative.Replace('\','/'); SHA256=(Get-FileHash -LiteralPath $to -Algorithm SHA256).Hash.ToLowerInvariant() }
}
$metadata = [pscustomobject]@{ Product='milestone-wheel-tools'; Version=$version; SourceCommit=$source; PackagingSourceCommit=$source; BinaryProvenance=$provenance; CreatedUtc=[DateTime]::UtcNow.ToString('o'); Files=$manifest }
[IO.File]::WriteAllText((Join-Path $destination 'package-manifest.json'), ($metadata | ConvertTo-Json -Depth 6))
Assert-PackageManifest $destination
Assert-PackageBinaryProvenance $destination ([IO.File]::ReadAllText((Join-Path $destination 'package-manifest.json')) | ConvertFrom-Json)
$finalSource = (& git -C $root rev-parse HEAD).Trim()
if ($LASTEXITCODE -or $finalSource -cne $source) { throw 'Packaging source commit changed; ZIP creation refused.' }
$finalState = @(& git -C $root status --porcelain --untracked-files=no)
if ($LASTEXITCODE -or $finalState.Count) { throw 'Packaging source state changed; ZIP creation refused.' }
$zip = "$destination.zip"
Compress-Archive -Path (Join-Path $destination '*') -DestinationPath $zip -CompressionLevel Optimal
Write-Output $destination
Write-Output "Packaging source: $source; retained binaries: $($provenance.HistoricalSourceEvidenceCommit); compiler/build receipt unknown."
Get-FileHash -LiteralPath $zip -Algorithm SHA256
