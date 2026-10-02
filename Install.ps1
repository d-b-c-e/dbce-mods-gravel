<#
.SYNOPSIS
    Install or update the supported Gravel wheel and telemetry package.
.DESCRIPTION
    Close the game first. Personal settings are preserved unless a specific
    override is supplied. The proxy, external setup tools and wheel profile
    are installed with backups and rollback; another proxy is never replaced.
#>
[CmdletBinding(SupportsShouldProcess=$true,ConfirmImpact='Medium')]
param(
    [ValidateSet('Gravel')][string]$Game,
    [string]$GamePath,
    [ValidateRange(1,65535)][int]$Port = 5300,
    [ValidateSet('fh4','fm7','sled')][string]$Format = 'fh4',
    [string]$Product,
    [switch]$SkipWheelConfig,
    [Alias('DryRun')][switch]$Check
)
$ErrorActionPreference = 'Stop'
$planOnly = $Check -or $WhatIfPreference
$root = $PSScriptRoot
Import-Module (Join-Path $root 'tools\SetupUx.psm1') -Force
Import-Module (Join-Path $root 'tools\InstallPackage.psm1') -Force
Assert-PackageManifest $root
Import-Module (Join-Path $root 'lib\toolkit\powershell\DbceWheel.psm1') -Force
function Say($Message) { Write-Host $Message }
function Fail($Message) { throw $Message }
Say 'Gravel wheel mod - install / update'
if ($planOnly) { Say 'Check only: no writes, backups or hardware enumeration. Blocking conflicts are reported as errors.' }

$dll = Join-Path $root 'dist\dinput8.dll'
if (-not (Test-Path -LiteralPath $dll)) { Fail 'The package is incomplete. Extract the whole ZIP, including dist, and retry.' }
$setupFiles = @(Get-SetupPackageFiles)
foreach ($relative in $setupFiles) {
    if (-not (Test-Path -LiteralPath (Join-Path $root $relative))) { Fail "The package is incomplete: $relative. Extract the whole ZIP and retry." }
}
if (-not $GamePath) {
    $found = @(foreach ($library in Get-SteamLibraries) {
        $candidate = Join-Path $library 'steamapps\common\Gravel\gravel\Binaries\Win64'
        if (Test-Path -LiteralPath (Join-Path $candidate 'gravel-Win64-Shipping.exe')) { $candidate }
    })
    if ($found.Count -eq 0) { Fail 'Gravel was not found. Use -GamePath with its gravel\Binaries\Win64 folder.' }
    if ($found.Count -gt 1) { Fail 'Several Gravel installs were found. Use -GamePath to choose the one to update.' }
    $GamePath = $found[0]
}
$GamePath = [IO.Path]::GetFullPath($GamePath)
if (-not (Test-Path -LiteralPath (Join-Path $GamePath 'gravel-Win64-Shipping.exe'))) { Fail 'This package supports Gravel only. Select its folder containing gravel-Win64-Shipping.exe.' }
$canonical = (Split-Path $GamePath -Leaf) -eq 'Win64' -and (Split-Path (Split-Path $GamePath -Parent) -Leaf) -eq 'Binaries'
$inner = if ($canonical) { Split-Path (Split-Path $GamePath -Parent) -Parent } else { $GamePath }
$gameRoot = if ($canonical) { Split-Path $inner -Parent } else { $GamePath }
$gamePrefix = $gameRoot.TrimEnd('\') + '\'
$checkClosed = {
    $running = Get-Process -EA SilentlyContinue | Where-Object { $_.Path -and $_.Path.StartsWith($gamePrefix, [StringComparison]::OrdinalIgnoreCase) }
    if ($running) { Fail 'Gravel is running. Close it normally, then retry the install.' }
}
& $checkClosed

$destDll = Join-Path $GamePath 'dinput8.dll'
$ownership = Get-InstallOwnership $GamePath $gameRoot
$destIni = Assert-PackagePath (Join-Path $GamePath 'milestone_mod.ini') $gameRoot -FileTarget
$existing = if (Test-Path -LiteralPath $destIni) { [IO.File]::ReadAllText($destIni) } else { $null }
$savedProduct = if ($null -ne $existing) { Get-ModSetting $existing 'proxy' 'product' } else { '' }
$productName = 'Steering wheel'
if ($Product) {
    if ($Product -notmatch '^[a-fA-F0-9]{8}$') { Fail 'Product must be the eight-digit device product key.' }
    $productKey = $Product.ToLowerInvariant()
} elseif ($null -ne $existing) {
    if ($savedProduct -notmatch '^[a-fA-F0-9]{8}$') { Fail 'The saved wheel identity needs repair. Existing settings were kept; use an explicit -Product to replace it.' }
    $productKey = $savedProduct
} else {
    if ($planOnly) { Fail 'Check mode requires -Product or a valid saved wheel identity; hardware will not be enumerated.' }
    $devices = @(Get-DirectInputDevices | Where-Object { $_.ForceFeedback -and $_.Type -ne 0x15 -and $_.Name -notmatch '(?i)vjoy|vigem|xoutput|vxbox' })
    if ($devices.Count -eq 0) { Fail 'Connect the steering wheel, then retry. An explicit -Product can select a known device identity.' }
    if ($devices.Count -eq 1) { $selected = $devices[0] }
    else {
        for ($i=0; $i -lt $devices.Count; $i++) { Say "  [$($i+1)] $($devices[$i].Name)" }
        $choice = 0
        if (-not [int]::TryParse((Read-Host 'Choose the steering wheel'), [ref]$choice) -or $choice -lt 1 -or $choice -gt $devices.Count) { Fail 'No wheel selected; nothing was installed.' }
        $selected = $devices[$choice-1]
    }
    if (@($devices | Where-Object Key -eq $selected.Key).Count -ne 1) { Fail 'Several devices share this product identity. Disconnect the duplicate before setup.' }
    $productKey = $selected.Key
    $productName = $selected.Name
}
$ini = if ($null -ne $existing) { $existing } else { [IO.File]::ReadAllText((Join-Path $root 'games\gravel\milestone_mod.ini')) }
if ($null -eq $existing -or $PSBoundParameters.ContainsKey('Product')) { $ini = Set-ModSetting $ini 'proxy' 'product' $productKey }
if ($null -eq $existing -or $PSBoundParameters.ContainsKey('Port')) { $ini = Set-ModSetting $ini 'telemetry' 'port' ([string]$Port) }
if ($null -eq $existing -or $PSBoundParameters.ContainsKey('Format')) { $ini = Set-ModSetting $ini 'telemetry' 'format' $Format }
# Identical owner INIs are copied as bytes, so even encoding/BOM stays unchanged.
$iniBytes = if ($null -ne $existing -and $ini -ceq $existing) { [IO.File]::ReadAllBytes($destIni) } else { [Text.UTF8Encoding]::new($false).GetBytes($ini) }
$entries = @(
    [pscustomobject]@{ Path=$destDll; Bytes=[IO.File]::ReadAllBytes($dll) }
    [pscustomobject]@{ Path=$destIni; Bytes=$iniBytes }
)
if (-not $SkipWheelConfig) {
    $wheelConfig = Assert-PackagePath (Join-Path $inner 'Config\WindowsNoEditor\WheelConfig.ini') $gameRoot -FileTarget
    if (-not (Test-Path -LiteralPath $wheelConfig)) { Fail 'Gravel WheelConfig.ini was not found. Restore the game file before installing, or explicitly use -SkipWheelConfig.' }
    $wheelText = [IO.File]::ReadAllText($wheelConfig)
    $wheelNew = Add-WheelProfile $wheelText $productKey $productName
    $wheelBytes = if ($wheelNew -ceq $wheelText) { [IO.File]::ReadAllBytes($wheelConfig) } else { [Text.UTF8Encoding]::new($false).GetBytes($wheelNew) }
    $entries += [pscustomobject]@{ Path=$wheelConfig; Bytes=$wheelBytes }
}
$setupRoot = Join-Path $GamePath 'DBCE-Wheel-Setup'
$owned = @([pscustomobject]@{ Path=$destDll; Hash=(Get-ByteHash ([IO.File]::ReadAllBytes($dll))) })
foreach ($relative in $setupFiles) {
    $bytes = [IO.File]::ReadAllBytes((Join-Path $root $relative))
    $target = Join-Path $setupRoot $relative
    $entries += [pscustomobject]@{ Path=$target; Bytes=$bytes }
    $owned += [pscustomobject]@{ Path=$target; Hash=(Get-ByteHash $bytes) }
}
$launcher = Join-Path $gameRoot 'Wheel settings.bat'
$launchText = "@echo off`r`nREM DBCE Milestone wheel settings`r`npowershell.exe -NoProfile -ExecutionPolicy Bypass -File `"$setupRoot\WheelSetup.ps1`" -GamePath `"$inner`" -ModConfig `"$destIni`"`r`npause`r`n"
$launchBytes = [Text.UTF8Encoding]::new($false).GetBytes($launchText)
$entries += [pscustomobject]@{ Path=$launcher; Bytes=$launchBytes }
$owned += [pscustomobject]@{ Path=$launcher; Hash=(Get-ByteHash $launchBytes) }
$version = ([IO.File]::ReadAllText((Join-Path $root 'VERSION'))).Trim()
$receipt = [pscustomobject]@{ Product='milestone-wheel-tools'; Version=$version; Game='Gravel'; GameRoot=$gameRoot; InstalledUtc=[DateTime]::UtcNow.ToString('o'); OwnedFiles=$owned }
$entries += [pscustomobject]@{ Path=(Join-Path $GamePath 'milestone_install.json'); Bytes=[Text.UTF8Encoding]::new($false).GetBytes(($receipt | ConvertTo-Json -Depth 6)) }
foreach ($entry in $entries) {
    if ($ownership.ContainsKey($entry.Path)) { $entry | Add-Member -NotePropertyName ExpectedHash -NotePropertyValue $ownership[$entry.Path] }
}
if ($planOnly) {
    $plan = @(Get-PackageInstallPlan -Entries $entries -AllowedRoot $gameRoot -BeforeRead $checkClosed)
    foreach ($item in $plan) { Say "$($item.Action): $($item.Path)" }
    $overrides=@('Product','Port','Format') | Where-Object { $PSBoundParameters.ContainsKey($_) }
    Say "Explicit settings overrides: $(if($overrides){$overrides -join ', '}else{'none'})"
    Say 'Unspecified existing INI settings and existing wheel bindings are preserved.'
    Say 'Plan only; no files, receipts or backups were written. Re-run the installer without Check/DryRun/WhatIf to apply after revalidation.'
    [pscustomobject]@{Mode='Check';Game='Gravel';GameRoot=$gameRoot;Product=$productKey;Files=$plan;SettingsOverrides=@($overrides);HardwareEnumerated=$false;WritesPerformed=0}
    return
}
if (-not $PSCmdlet.ShouldProcess($gameRoot,'Install/update the Gravel package with backups and rollback')) { return }
$backup = Join-Path $GamePath ('DBCE-Wheel-Backups\' + (Get-Date -Format 'yyyyMMdd-HHmmss') + '-' + [guid]::NewGuid().ToString('N').Substring(0,6))
$result = Invoke-PackageInstall -Entries $entries -AllowedRoot $gameRoot -BackupRoot $backup -BeforeWrite $checkClosed
Say "Installed $version. Backups: $($result.BackupRoot)"
Say "Open: $launcher"
Say 'Simple setup: bind Steering, Throttle and Brake; optional controls stay optional.'
Say 'Save mappings and exit, then run the game wheel calibration once.'
try {
    $telemetry = Get-TelemetryState $ini
    $enabledLabel = switch ($telemetry.enabled) { '0' { 'Off' }; '1' { 'On' }; default { 'Custom value' } }
    Say "Telemetry: $enabledLabel - $($telemetry.host):$($telemetry.port), $(Get-TelemetryReceiver $telemetry.format). Saved for the next game launch; delivery unverified."
} catch { Say 'Telemetry settings were preserved but need review in Wheel settings. Installation completed.' }
