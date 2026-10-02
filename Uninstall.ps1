<#
.SYNOPSIS
    Remove the Gravel wheel mod while keeping personal settings and backups.
#>
[CmdletBinding()]
param([ValidateSet('Gravel')][string]$Game, [string]$GamePath, [switch]$RemoveSettings)
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'tools\InstallPackage.psm1') -Force
if (-not $GamePath) {
    $beside = Split-Path $PSScriptRoot -Parent
    if (Test-Path -LiteralPath (Join-Path $beside 'gravel-Win64-Shipping.exe')) { $GamePath = $beside }
    else {
        Import-Module (Join-Path $PSScriptRoot 'lib\toolkit\powershell\DbceWheel.psm1') -Force
        $hits = @(foreach ($library in Get-SteamLibraries) {
            $candidate = Join-Path $library 'steamapps\common\Gravel\gravel\Binaries\Win64'
            if (Test-Path -LiteralPath (Join-Path $candidate 'milestone_mod.ini')) { $candidate }
        })
        if ($hits.Count -ne 1) { throw 'Choose a Gravel install with -GamePath pointing to its gravel\Binaries\Win64 folder.' }
        $GamePath = $hits[0]
    }
}
$GamePath = [IO.Path]::GetFullPath($GamePath)
if (-not (Test-Path -LiteralPath (Join-Path $GamePath 'gravel-Win64-Shipping.exe'))) { throw 'Select the Gravel folder containing gravel-Win64-Shipping.exe.' }
$canonical = (Split-Path $GamePath -Leaf) -eq 'Win64' -and (Split-Path (Split-Path $GamePath -Parent) -Leaf) -eq 'Binaries'
$gameRoot = if ($canonical) { Split-Path (Split-Path (Split-Path $GamePath -Parent) -Parent) -Parent } else { $GamePath }
$prefix = $gameRoot.TrimEnd('\') + '\'
function Assert-Closed {
    if (Get-Process -EA SilentlyContinue | Where-Object { $_.Path -and $_.Path.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase) }) { throw 'Close Gravel normally before uninstalling.' }
}
Assert-Closed
$receiptPath = Join-Path $GamePath 'milestone_install.json'
$dll = Join-Path $GamePath 'dinput8.dll'
$owned = @()
if (Test-Path -LiteralPath $receiptPath) {
    $receipt = Read-PackageReceipt $receiptPath $GamePath $gameRoot
    foreach ($path in $receipt.Files.Keys) {
        $owned += [pscustomobject]@{ Path=$path; Hash=$receipt.Files[$path] }
    }
} elseif (Test-Path -LiteralPath $dll) {
    $bytes = [IO.File]::ReadAllBytes($dll)
    $hash = Get-ByteHash $bytes
    if (Test-KnownLegacyProxy $hash) { $owned += [pscustomobject]@{ Path=$dll; Hash=$hash } }
    else { throw 'Unknown receiptless dinput8.dll kept; no files were removed.' }
} else {
    Write-Host 'No validated receipt or known legacy proxy; no files were removed.'
    return
}
$retained = $false
$removals = @()
foreach ($item in $owned) {
    Assert-Closed
    if (-not [IO.File]::Exists($item.Path)) { continue }
    if ((Get-ByteHash ([IO.File]::ReadAllBytes($item.Path))) -ne $item.Hash) {
        $retained = $true
        Write-Host "Modified file kept: $($item.Path)"
        continue
    }
    $removals += $item
}
foreach ($name in @('milestone_mod.log') + $(if ($RemoveSettings) { @('milestone_mod.ini') } else { @() })) {
    Assert-Closed
    $path = Assert-PackagePath (Join-Path $GamePath $name) $gameRoot
    if (Test-Path -LiteralPath $path) { $removals += [pscustomobject]@{ Path=$path; Hash=(Get-ByteHash ([IO.File]::ReadAllBytes($path))) } }
}
if (-not $retained -and (Test-Path -LiteralPath $receiptPath)) { $removals += [pscustomobject]@{ Path=$receiptPath; Hash=$receipt.Hash } }
if ($removals.Count) {
    $backup = Join-Path $GamePath ('DBCE-Wheel-Backups\uninstall-' + (Get-Date -Format 'yyyyMMdd-HHmmss') + '-' + [guid]::NewGuid().ToString('N').Substring(0,6))
    $result = Invoke-PackageRemoval $removals $gameRoot $backup -BeforeWrite { Assert-Closed }
    Write-Host "Removed $($result.RemovedFiles) files. Recovery backups: $($result.BackupRoot)"
}
Write-Host 'Wheel bindings, game calibration, presentation preferences and dated backups were kept.'
if (-not $RemoveSettings) { Write-Host 'Personal mod settings were kept for a future reinstall.' }
