param([string]$OutputRoot = (Join-Path (Split-Path $PSScriptRoot -Parent) 'artifacts'), [string]$Toolchain = 'E:\msys64\mingw64\bin')
# Builds the committed source, then writes the complete player ZIP with a per-file SHA256 manifest.
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
Import-Module (Join-Path $PSScriptRoot 'InstallPackage.psm1') -Force
$version = ([IO.File]::ReadAllText((Join-Path $root 'VERSION'))).Trim()
$source = (& git -C $root rev-parse HEAD).Trim()
if ($LASTEXITCODE) { throw 'Source commit could not be read.' }
$sourceState = @(& git -C $root status --porcelain --untracked-files=no)
if ($LASTEXITCODE) { throw 'Packaging source state could not be read.' }
if ($sourceState.Count) { throw 'Commit source before packaging; the package records the commit it was built from.' }
# Build this commit so the package never carries stale binaries.
& (Join-Path $root 'build.ps1') -Toolchain $Toolchain | Out-Null
$name = "milestone-wheel-tools-$version-$($source.Substring(0,8))"
$destination = Join-Path $OutputRoot $name
if (Test-Path -LiteralPath $destination) { throw "Package already exists: $destination" }
[IO.Directory]::CreateDirectory($destination) | Out-Null
$manifest = @()
foreach ($relative in @(Get-PackageFiles)) {
    # The package keeps its binaries under dist\ (the installed layout); they come from the build output.
    $from = if ($relative -like 'dist\*') { Join-Path $root ('build\' + $relative.Substring(5)) } else { Join-Path $root $relative }
    if (-not (Test-Path -LiteralPath $from)) { throw "Package input missing: $from" }
    $to = Join-Path $destination $relative
    [IO.Directory]::CreateDirectory((Split-Path $to -Parent)) | Out-Null
    Copy-Item -LiteralPath $from -Destination $to
    $manifest += [pscustomobject]@{ Path=$relative.Replace('\','/'); SHA256=(Get-FileHash -LiteralPath $to -Algorithm SHA256).Hash.ToLowerInvariant() }
}
$metadata = [pscustomobject]@{ Product='milestone-wheel-tools'; Version=$version; SourceCommit=$source; CreatedUtc=[DateTime]::UtcNow.ToString('o'); Files=$manifest }
[IO.File]::WriteAllText((Join-Path $destination 'package-manifest.json'), ($metadata | ConvertTo-Json -Depth 6))
Assert-PackageManifest $destination
$finalSource = (& git -C $root rev-parse HEAD).Trim()
if ($LASTEXITCODE -or $finalSource -cne $source) { throw 'Packaging source commit changed; ZIP creation refused.' }
$finalState = @(& git -C $root status --porcelain --untracked-files=no)
if ($LASTEXITCODE -or $finalState.Count) { throw 'Packaging source state changed; ZIP creation refused.' }
$zip = "$destination.zip"
Compress-Archive -Path (Join-Path $destination '*') -DestinationPath $zip -CompressionLevel Optimal
Write-Output $destination
Get-FileHash -LiteralPath $zip -Algorithm SHA256
