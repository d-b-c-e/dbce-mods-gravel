<#
.SYNOPSIS
    Map wheel controls for a Milestone game without using its in-game menu.

.DESCRIPTION
    Milestone's rebind screen is unusable with many direct-drive wheels: an axis
    resting at an extreme reads as permanently deflected, so the "press a
    control" listener latches onto it and never sees the button you press. This
    does the same job from outside the game.

    It is action-oriented on purpose. These games bind in two layers -

        physical button  ->  logical slot   (WheelConfig.ini)
        logical slot     ->  game action    (settings.sav)

    - so putting "rewind" on a button actually means finding which Wheel_* slot
    RewindActivate listens to and pointing that slot at your button. You should
    not have to know that, so this works it out for you.

    Pedals are measured, never guessed: it watches which axis moves and in which
    direction, and derives the polarity from that. Copying another wheel's block
    is what leaves a game convinced a pedal is permanently held.

.PARAMETER GamePath
    Folder containing the game's WheelConfig.ini, if auto-detection fails.

.PARAMETER Product
    Device product key in hex, to skip the device menu.

.PARAMETER View
    Simple or Advanced. An explicit choice is remembered without changing mappings.

.PARAMETER SettingsSave
    Exact settings.sav for the selected game when its save cannot be discovered.

.PARAMETER PreferencesPath
    Optional presentation-preference location. Does not replace game configuration.

.PARAMETER ModConfig
    Exact installed milestone_mod.ini when the selected game's Win64 location differs.

.EXAMPLE
    .\WheelSetup.ps1
#>
[CmdletBinding()]
param(
    [string]$GamePath,
    [string]$Product,
    [string]$SettingsSave,
    [string]$ModConfig,
    [ValidateSet('Simple', 'Advanced')][string]$View,
    [string]$PreferencesPath = (Join-Path $env:LOCALAPPDATA 'DBCE\MilestoneWheelTools\settings-view.json')
)

$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$probe = Join-Path $root 'tools\wheelprobe\wheelprobe.exe'
if (-not (Test-Path $probe)) { $probe = Join-Path $root 'dist\wheelprobe.exe' }
Import-Module (Join-Path $root 'tools\SetupUx.psm1') -Force

function Say($m, $c = 'Gray') { Write-Host $m -ForegroundColor $c }
function Head($m) { Write-Host "`n$m" -ForegroundColor Cyan }
function Ok($m) { Write-Host "  OK   $m" -ForegroundColor Green }
function Warn($m) { Write-Host "  !    $m" -ForegroundColor Yellow }
function Die($m) { Write-Host "  X    $m" -ForegroundColor Red; exit 1 }

try {
    if ($View) { Save-SettingsView $PreferencesPath $View }
    else { $View = Get-SettingsView $PreferencesPath }
} catch { Warn $_.Exception.Message; $View = 'Simple' }

function Pick-Number([string]$Prompt, [int]$Count) {
    $answer = Read-Host "$Prompt (1-$Count, Enter to cancel)"
    $number = 0
    if ([int]::TryParse($answer, [ref]$number) -and $number -ge 1 -and $number -le $Count) { return ($number - 1) }
    return -1
}

if (-not (Test-Path $probe)) {
    Die 'The input reader is missing. Restore dist\wheelprobe.exe from the release, then reopen setup.'
}

# ---------------------------------------------------------------- the game
# Steam discovery and DirectInput enumeration come from dbce-wheel-mod-toolkit's
# PowerShell module, vendored under lib\toolkit (tools\Sync-Toolkit.ps1).
$toolkit = Join-Path $root 'lib\toolkit\powershell\DbceWheel.psm1'
if (-not (Test-Path $toolkit)) { Die "lib\toolkit is missing - run tools\Sync-Toolkit.ps1, or re-download the release" }
Import-Module $toolkit -Force

Say "Wheel settings - Milestone" 'White'
Say "View: $View | External setup - close the game before making changes."
Head "Finding the game"
$wheelConfigs = @()
if ($GamePath) {
    $wheelConfigs = @(Get-ChildItem $GamePath -Recurse -Depth 4 -Filter 'WheelConfig.ini' -EA SilentlyContinue)
} else {
    # Always at <game>\<inner>\Config\WindowsNoEditor\WheelConfig.ini, so go
    # straight there. A blind -Recurse walks entire multi-GB game folders.
    foreach ($lib in Get-SteamLibraries) {
        $common = Join-Path $lib 'steamapps\common'
        if (-not (Test-Path $common)) { continue }
        foreach ($game in (Get-ChildItem $common -Directory -EA SilentlyContinue)) {
            foreach ($inner in (Get-ChildItem $game.FullName -Directory -EA SilentlyContinue)) {
                $c = Join-Path $inner.FullName 'Config\WindowsNoEditor\WheelConfig.ini'
                if (Test-Path $c) { $wheelConfigs += (Get-Item $c) }
            }
        }
    }
}
if (-not $wheelConfigs) { Die "No WheelConfig.ini found. Pass -GamePath <game folder>." }
if ($wheelConfigs.Count -gt 1) {
    Say "  Found several:"
    for ($i = 0; $i -lt $wheelConfigs.Count; $i++) { Say "    [$($i+1)] $($wheelConfigs[$i].FullName)" }
    $choice = Pick-Number '  Choose a game' $wheelConfigs.Count
    if ($choice -lt 0) { return }
    $wc = $wheelConfigs[$choice].FullName
} else { $wc = $wheelConfigs[0].FullName }
Ok $wc

# the game rewrites this file when it exits
$gameDir = $wc
while ($gameDir -and -not (Get-ChildItem $gameDir -Filter '*.exe' -EA SilentlyContinue)) { $gameDir = Split-Path $gameDir -Parent }
$running = Get-Process -EA SilentlyContinue | Where-Object { $_.Path -and $gameDir -and $_.Path.StartsWith($gameDir, [StringComparison]::OrdinalIgnoreCase) }
if ($running) { Die "$($running[0].ProcessName) is running - it rewrites WheelConfig.ini on exit. Close it first." }
$innerGameFolder = Split-Path (Split-Path (Split-Path $wc -Parent) -Parent) -Parent
$telemetryPath = if ($ModConfig) { [IO.Path]::GetFullPath($ModConfig) } else { Join-Path $innerGameFolder 'Binaries\Win64\milestone_mod.ini' }

# ------------------------------------------------- action -> slot, from the save
# settings.sav is a UE4 GVAS file whose ignitioninput block stores every binding
# as ActionName / KeyName property pairs. KeyName is a keyboard key, a Gamepad_*
# key, or the Wheel_* slot we care about.
function Get-ActionSlots {
    if ($SettingsSave) { $sav = Get-Item -LiteralPath $SettingsSave }
    else {
        # Never take the first other game's save from LocalAppData.
        $inner = Split-Path (Split-Path (Split-Path $wc -Parent) -Parent) -Parent | Split-Path -Leaf
        $candidates = @(Get-ChildItem "$env:LOCALAPPDATA" -Directory -EA SilentlyContinue |
            Where-Object { ($_.Name -replace '[^a-z0-9]', '') -eq ($inner -replace '[^a-z0-9]', '') } |
            ForEach-Object { Get-ChildItem $_.FullName -Recurse -Filter 'settings.sav' -Depth 4 -EA SilentlyContinue })
        $sav = if ($candidates.Count -eq 1) { $candidates[0] } else { $null }
    }
    if (-not $sav) { return @{} }
    Read-SavedWheelActions ([IO.File]::ReadAllBytes($sav.FullName))
}

Head "Reading the game's action list"
$actionSlot = Get-ActionSlots
if ($actionSlot.Count) { Ok "$($actionSlot.Count) actions have a wheel binding" }
else { Warn "Saved button layout unavailable. Only Gravel's verified default actions can be offered." }
$isGravel = $wc -match '(?i)[\\/]gravel[\\/]'

# Gravel's defaults, verified by dumping settings.sav. Used when the save cannot
# be read, and to give friendly names to the actions worth binding.
$FRIENDLY = Get-WheelButtonActions $actionSlot

# --------------------------------------------------------------- the device
Head "Choosing the device"
$devs = @()
foreach ($line in (& $probe DEVICES)) {
    if ($line -match '^DEV (\w+) (\S+) (\d) (.+)$') {
        $devs += [pscustomobject]@{ Key = $Matches[1]; Type = $Matches[2]; Name = $Matches[4] }
    }
}
if (-not $devs) { Die "No DirectInput controllers attached." }
if ($Product) { $dev = $devs | Where-Object Key -eq $Product.ToLower() | Select-Object -First 1 }
if ($Product -and -not $dev) { Die "The selected device is not connected. Reconnect it or choose a device explicitly." }
if (-not $dev) {
    if ($devs.Count -eq 1) { $dev = $devs[0] }
    else {
        for ($i = 0; $i -lt $devs.Count; $i++) { Say "    [$($i+1)] $($devs[$i].Name)" }
        $choice = Pick-Number '  Choose a device' $devs.Count
        if ($choice -lt 0) { return }
        $dev = $devs[$choice]
    }
}
if (-not $dev) { Die "invalid choice" }
if (@($devs | Where-Object Key -eq $dev.Key).Count -gt 1) { Die 'Two connected devices share the same product identity. Disconnect the duplicate before setup.' }
Ok $dev.Name

# ------------------------------------------------------------------ helpers
function Sample([int]$Seconds, [string]$InputProduct = $dev.Key) {
    Read-WheelInput -Probe $probe -Product $InputProduct -Seconds $Seconds
}

$AXES = Get-WheelAxisFields $actionSlot

# ------------------------------------------------- WheelConfig read / write
$raw = [IO.File]::ReadAllText($wc)
$original = $raw
if ($raw -notmatch "\r\n") { Die 'WheelConfig.ini is not CRLF. Restore its backup before setup.' }
$section = "[/Wheel.Config/$($dev.Key)]"
if (-not (Get-WheelBlock $raw $dev.Key).Success) {
    Warn 'A profile will be created when you choose Save mappings and exit.'
    $tpl = Get-WheelBlock $raw 'c262046d'
    if (-not $tpl.Success) { $tpl = [regex]::Match($raw, '\[/Wheel\.Config/\w+\].*?(?=\r\n\[/Wheel\.Config/|\Z)', 'Singleline') }
    if (-not $tpl.Success) { Die 'No supported wheel template found. Restore the game file before setup.' }
    $block = $tpl.Value -replace '^\[/Wheel\.Config/\w+\]', $section
    # A new profile must not inherit another wheel's unverified axis/button bindings.
    $block = $block -replace '(?m)^(Wheel_\w+)=[^\r\n]*', '$1='
    $block = [regex]::Replace($block, '(?m)^ProductName=[^\r\n]*', { "ProductName=$($dev.Name)" })
    $raw = $raw.TrimEnd("`r", "`n") + "`r`n`r`n" + $block.TrimEnd("`r", "`n") + "`r`n"
}

function Set-Field($Key, $Value) { $script:raw = Set-WheelField $script:raw $dev.Key $Key $Value }
function Get-Field($Key) { Get-WheelField $script:raw $dev.Key $Key }
function Assignment($Value) { Format-WheelAssignment $Value -Advanced:($View -eq 'Advanced') }
function Resolve-Slot($Name) {
    $slot = Resolve-WheelActionSlot $FRIENDLY[$Name] $actionSlot -GravelDefaults:$isGravel
    if ($slot -and [regex]::IsMatch((Get-WheelBlock $script:raw $dev.Key).Value,
        '(?m)^' + [regex]::Escape($slot) + '=')) { return $slot }
    return $null
}

function Edit-Axis([string]$Name) {
    $field = $AXES[$Name]
    if (-not $field -or [regex]::Matches((Get-WheelBlock $script:raw $dev.Key).Value,
        '(?m)^' + [regex]::Escape($field) + '=').Count -ne 1) {
        Warn 'No unique saved axis mapping and existing wheel field. The previous assignment was kept.'; return
    }
    Head "$Name - $(Assignment (Get-Field $field))"
    $choice = Read-Host '  [B] Bind  [I] Invert direction  [C] Clear  [Enter] Cancel'
    if ($choice -eq 'i') {
        try {
            $value = Get-InvertedAxisMapping (Get-Field $field)
            if ((Read-Host "  $(Assignment $value). [S] Save binding  [Enter] Cancel") -eq 's') {
                Set-Field $field $value
                Ok "$Name ready to save"
            } else { Say '  Canceled. The previous assignment was kept.' }
        } catch { Warn $_.Exception.Message }
        return
    }
    if ($choice -eq 'c') {
        if ((Read-Host "  Clear $Name? Type Clear, or Enter to cancel") -eq 'Clear') { Set-Field $field '' }
        return
    }
    if ($choice -ne 'b') { return }
    try {
        Say '  Finish or cancel this binding before changing view.' 'DarkGray'
        Say '  Keep other controls still. Esc cancels input capture.'
        if ($Name -eq 'Steering') { $prompt = 'Center the wheel' }
        else { $prompt = 'Release this pedal or handbrake' }
        if ((Read-Host "  $prompt. Enter to measure rest, or C to cancel") -eq 'c') { return }
        $rest = @(Sample 2)
        $prompt = if ($Name -eq 'Steering') { 'Turn fully left, then fully right' } else { 'Press or pull fully, hold briefly, then release' }
        if ((Read-Host "  $prompt during the next 6 seconds. Enter to begin, or C to cancel") -eq 'c') { return }
        $travel = @(Sample 6)
        $mapping = Find-AxisMapping $rest $travel -Steering:($Name -eq 'Steering')
        Say "  Detected: $(Assignment $mapping.Value)"
        Say '  This sets the axis and its direction. Finish endpoint/deadzone calibration in the game.'
        if ($View -eq 'Advanced') { Say "  Raw rest: $($mapping.Rest); full: $($mapping.Full)" }
        $choice = Read-Host '  [S] Save binding  [I] Invert direction  [Enter] Cancel'
        if ($choice -eq 'i') {
            $mapping.Inverted = -not $mapping.Inverted
            $polarity = if ($mapping.Inverted) { '-1.0&1.0' } else { '1.0&0.0' }
            $mapping.Value = "Axis$($mapping.Axis + 1)&$polarity"
            $choice = Read-Host "  $(Assignment $mapping.Value). [S] Save binding  [Enter] Cancel"
        }
        if ($choice -eq 's') { Set-Field $field $mapping.Value; Ok "$Name ready to save" }
        else { Say '  Canceled. The previous assignment was kept.' }
    } catch { Warn $_.Exception.Message }
}

function Edit-Button([string]$Name) {
    $slot = Resolve-Slot $Name
    if (-not $slot) { Warn 'No unique saved action mapping and existing wheel field. Bind this action once in Gravel, close the game, then reopen setup. Mappings were kept.'; return }
    Head "$Name - $(Assignment (Get-Field $slot))"
    try {
        $choice = Read-Host '  [B] Bind  [C] Clear  [Enter] Cancel'
        if ($choice -eq 'c') {
            if ((Read-Host "  Clear $Name? Type Clear, or Enter to cancel") -eq 'Clear') { Set-Field $slot '' }
            return
        }
        if ($choice -ne 'b') { return }
        $source = Get-ButtonCaptureSource (Get-Field $slot) $dev.Key $devs
        Say "  Input device: $($source.Name)"
        Say '  Release the button first, then press it during the next 6 seconds. Esc cancels.'
        $captured = @(Sample 6 $source.Product | Where-Object { $_ -match '^BTN \d+$' })
        if (-not $captured.Count) { Warn 'No button pressed. The previous assignment was kept.'; return }
        $button = [int]($captured[0].Substring(4))
        if ($button -lt 1 -or $button -gt 128) { Warn 'The input reader returned an unsupported button. The previous assignment was kept.'; return }
        $value = $source.Prefix + "Button$button"
        $shared = @($FRIENDLY.Keys | Where-Object { $_ -ne $Name -and (Resolve-Slot $_) -eq $slot })
        if ($shared.Count) { Warn "The game shares this assignment with: $($shared -join ', ')." }
        $other = @($FRIENDLY.Keys | Where-Object { $_ -ne $Name -and (Resolve-Slot $_) -and (Get-Field (Resolve-Slot $_)) -eq $value })
        if ($other.Count) { Warn "Button $button also triggers: $($other -join ', ')." }
        if ((Read-Host "  Button $button. [S] Save binding  [Enter] Cancel") -eq 's') {
            Set-Field $slot $value
            Ok "$Name ready to save"
        } else { Say '  Canceled. The previous assignment was kept.' }
    } catch { Warn $_.Exception.Message }
}

function Show-DeviceInputPreview {
    $selectedGameFolder = $innerGameFolder.TrimEnd('\') + '\'
    $running = Get-Process -EA SilentlyContinue | Where-Object { $_.Path -and $_.Path.StartsWith($selectedGameFolder, [StringComparison]::OrdinalIgnoreCase) }
    if ($running) { Warn 'Close the game before previewing device input.'; return }
    Head 'Device input preview - read only'
    Say '  Move the wheel, pedals or handbrake. Esc returns; preview ends after 10 seconds.'
    Say '  Uses the current staged assignments, before the game calibration.'
    Say '  Game-final input, endpoints and deadzones are not verified here.'
    if ($View -eq 'Advanced') { Say '  Device range: 0-65535; normalized values are clamped to this range.' }
    $names = @($AXES.Keys | Where-Object { $_ -ne 'Clutch' -or (Get-Field $AXES[$_]) })
    $anchor = $null
    $seen = $false
    $lastDraw = -1.0
    $clock = [Diagnostics.Stopwatch]::StartNew()
    try {
        if (-not [Console]::IsOutputRedirected) {
            foreach ($name in $names) { Write-Host '' }
            $anchor = [Math]::Max(0, [Console]::CursorTop - $names.Count)
        }
    } catch { $anchor = $null }
    try {
        # This is one helper invocation for the whole preview, not one device
        # connection per row. Sample owns cancellation and process cleanup.
        Sample 10 | ForEach-Object {
            $samples = @(Get-AxisSamples @($_) -AllowOutOfRange)
            if ($samples.Count) {
                $seen = $true
                if ($lastDraw -lt 0 -or $clock.Elapsed.TotalSeconds - $lastDraw -ge 0.1) {
                    $lastDraw = $clock.Elapsed.TotalSeconds
                    $rows = @()
                    foreach ($name in $names) {
                        $value = Get-DeviceInputPreview (Get-Field $AXES[$name]) $samples[0].Values -Steering:($name -eq 'Steering')
                        $line = "  ${name}: $($value.Label)"
                        if ($value.Clamped) { $line += ' (outside default range; clamped)' }
                        if ($View -eq 'Advanced' -and $value.Available) { $line += " | Raw: $($value.Raw)" }
                        $rows += $line
                    }
                    if ($null -ne $anchor) {
                        try {
                            $width = [Math]::Max(1, [Console]::WindowWidth - 1)
                            [Console]::SetCursorPosition(0, $anchor)
                            foreach ($row in $rows) { Write-Host $row.Substring(0, [Math]::Min($row.Length, $width)).PadRight($width) }
                        } catch { $anchor = $null; $rows | ForEach-Object { Say $_ } }
                    } else { $rows | ForEach-Object { Say $_ } }
                }
            }
        }
        if (-not $seen) { Warn 'No device samples received. Check the connection and retry.' }
        else { Say '  Preview finished. No settings were saved.' }
    } catch [OperationCanceledException] { Say '  Preview canceled. No settings were saved.' }
    catch { Warn "Preview stopped. No settings were saved. $($_.Exception.Message)" }
    finally {
        if ($null -ne $anchor) { try { [Console]::SetCursorPosition(0, $anchor + $names.Count + 1) } catch { } }
    }
}

function Read-TelemetrySnapshot {
    if (-not (Test-Path -LiteralPath $telemetryPath -PathType Leaf)) { throw 'Telemetry settings are not installed for this game. Run Install.bat, then reopen setup.' }
    $text = [IO.File]::ReadAllText($telemetryPath)
    [pscustomobject]@{ Text = $text; State = (Get-TelemetryState $text) }
}

function Assert-ModGameClosed([string]$Label) {
    $configFolder = (Split-Path $telemetryPath -Parent).TrimEnd('\') + '\'
    $selectedGameFolder = $innerGameFolder.TrimEnd('\') + '\'
    $running = Get-Process -EA SilentlyContinue | Where-Object {
        $_.Path -and ($_.Path.StartsWith($configFolder, [StringComparison]::OrdinalIgnoreCase) -or $_.Path.StartsWith($selectedGameFolder, [StringComparison]::OrdinalIgnoreCase))
    }
    if ($running) { throw "Close the game before saving $Label. Settings are read at the next game launch." }
}

function Write-TelemetryChange([string]$OriginalText, [string]$NewText) {
    Assert-ModGameClosed 'telemetry'
    $backup = Save-TelemetryConfig $telemetryPath $OriginalText $NewText
    if ($backup) { Ok 'Saved for the next game launch. Runtime delivery is unverified.' }
    else { Say '  No connection values changed.' }
}

function Edit-CameraSteps {
    try {
        if (!(Test-Path -LiteralPath $telemetryPath -PathType Leaf)) { throw 'Mod settings are not installed. Run Install.bat, then reopen setup.' }
        $original = [IO.File]::ReadAllText($telemetryPath)
        $state = Get-CameraSteps $original
    } catch { Warn $_.Exception.Message; return }
    $draft = @{ move_step_m=$state.move_step_m; tilt_step_deg=$state.tilt_step_deg; fov_step_deg=$state.fov_step_deg }
    while ($true) {
        Head 'Wheel settings - Cameras - Adjustment steps'
        Say '  Draft only. Saved steps apply at the next game launch, on triple layouts in a race.'
        Say "  [1] Move: $($draft.move_step_m) m per press (positive, up to 1 m)"
        Say "  [2] Tilt: $($draft.tilt_step_deg) degrees per press (positive, up to 45 degrees)"
        Say "  [3] FOV:  $($draft.fov_step_deg) degrees per press (positive, up to 45 degrees)"
        Say '  [D] Default steps  [A] Apply steps  [C] Cancel'
        Say '  Camera offsets, triple geometry, wheel mappings and telemetry stay as saved.'
        switch (Read-Host '  Choose') {
            '1' { $value=Read-Host '  Move step in meters (decimal point; Enter keeps draft)'; if ($value) { $draft.move_step_m=$value.Trim() } }
            '2' { $value=Read-Host '  Tilt step in degrees (decimal point; Enter keeps draft)'; if ($value) { $draft.tilt_step_deg=$value.Trim() } }
            '3' { $value=Read-Host '  FOV step in degrees (decimal point; Enter keeps draft)'; if ($value) { $draft.fov_step_deg=$value.Trim() } }
            'd' { $draft=@{move_step_m='0.02';tilt_step_deg='1';fov_step_deg='2'} }
            'a' {
                try {
                    Assert-ModGameClosed 'camera steps'
                    $backup=Save-CameraSteps $telemetryPath $original $draft
                    if ($backup) { Ok 'Camera steps saved for the next game launch.'; Say "  Backup: $backup" }
                    else { Say '  No camera step values changed.' }
                    return
                } catch { Warn "Camera steps were not applied. $($_.Exception.Message)" }
            }
            'c' { Say '  Canceled. Saved camera settings were kept.'; return }
            default { Warn 'Apply or cancel the current edit to return.' }
        }
    }
}

function Edit-TelemetryConnection {
    try { $snapshot = Read-TelemetrySnapshot } catch { Warn $_.Exception.Message; return }
    $draftAddress = $snapshot.State.host
    $draftPort = $snapshot.State.port
    $draftFormat = $snapshot.State.format
    while ($true) {
        Head 'Wheel settings - Telemetry - Connection settings'
        Say '  View: Simple [Advanced]'
        Say '  Draft only. Finish or cancel the current edit to change view.'
        Say "  [H] Receiver address: $draftAddress (IPv4 only; 127.0.0.1 is this PC)"
        Say "  [P] Port: $draftPort"
        Say "  [F] Receiver: $(Get-TelemetryReceiver $draftFormat)"
        Say '  Match the receiver game and UDP port in SimHub. Telemetry Off/On stays unchanged.'
        Say '  [A] Apply connection  [C] Cancel'
        switch (Read-Host '  Choose') {
            'h' { $value = Read-Host '  Receiver IPv4 address (Enter keeps current draft)'; if ($value) { $draftAddress = $value.Trim() } }
            'p' { $value = Read-Host '  Port 1-65535 (Enter keeps current draft)'; if ($value) { $draftPort = $value.Trim() } }
            'f' {
                Say '  [1] Forza Horizon 4 / 5  [2] Forza Motorsport 7  [3] Forza Sled (physics only)'
                $choice = Pick-Number '  Choose the matching receiver format' 3
                if ($choice -ge 0) { $draftFormat = @('fh4','fm7','sled')[$choice] }
            }
            'a' {
                try {
                    $candidate = Set-TelemetryFields $snapshot.Text @{ host = $draftAddress; port = $draftPort; format = $draftFormat }
                    Write-TelemetryChange $snapshot.Text $candidate
                    return
                } catch { Warn "Connection was not applied. $($_.Exception.Message)" }
            }
            'c' { Say '  Canceled. The saved connection was kept.'; return }
            default { Warn 'Finish or cancel the current edit to change view.' }
        }
    }
}

function Open-HelpDestination([string]$Url, [string]$Label) {
    if ($Url -notmatch '^https://github\.com/') { throw 'Only HTTPS GitHub destinations can be opened from Help.' }
    if ($env:MILESTONE_UX_BROWSER_OUT) {
        [IO.File]::AppendAllText($env:MILESTONE_UX_BROWSER_OUT, "$Url`r`n")
    } else {
        Start-Process $Url | Out-Null
    }
    Ok "Opened $Label"
}

function Invoke-SupportIssueFlow {
    Head 'Help - Submit issue'
    Say '  Prepare a diagnostic report. Nothing is sent or written until you review the full preview.'
    $title = Read-Host '  Issue title (Enter to cancel)'
    if ([string]::IsNullOrWhiteSpace($title)) { Say '  Canceled. No report was created.'; return }
    $description = Read-Host '  What happened and steps to reproduce (Enter to cancel)'
    if ([string]::IsNullOrWhiteSpace($description)) { Say '  Canceled. No report was created.'; return }
    $logCandidate = Join-Path (Split-Path $telemetryPath -Parent) 'milestone_mod.log'
    $selectedLogs = @()
    if (Test-Path -LiteralPath $logCandidate -PathType Leaf) {
        $includeLog = Read-Host '  Include milestone_mod.log tail (up to 24 KB, paths redacted)? [y/N]'
        if ($includeLog -eq 'y') { $selectedLogs += $logCandidate }
    }
    try {
        $versionFile = Join-Path $root 'VERSION'
        $version = if (Test-Path -LiteralPath $versionFile -PathType Leaf) { ([IO.File]::ReadAllText($versionFile)).Trim() } else { 'unknown' }
        $envSummary = [ordered]@{
            'OS'         = [Environment]::OSVersion.VersionString
            'PowerShell' = $PSVersionTable.PSVersion.ToString()
            'View'       = $View
        }
        $telemetrySummary = try {
            $t = (Read-TelemetrySnapshot).State
            "enabled=$($t.enabled), format=$($t.format), port=$($t.port)"
        } catch { 'not installed or unreadable' }
        $selectedSummary = [ordered]@{
            'Game'      = if ($isGravel) { 'Gravel' } else { 'Milestone game' }
            'Device'    = "$($dev.Name) ($($dev.Key))"
            'Steering'  = (Assignment (Get-Field $AXES['Steering']))
            'Throttle'  = (Assignment (Get-Field $AXES['Throttle']))
            'Brake'     = (Assignment (Get-Field $AXES['Brake']))
            'Telemetry' = $telemetrySummary
        }
        $report = New-PreparedSupportReport -ProductName 'Gravel' -ProductVersion $version -Repository 'd-b-c-e/dbce-mods-gravel' -EntryPoint 'WheelSetup.ps1' -Title $title -Description $description -EnvironmentSummary $envSummary -SelectedSummary $selectedSummary -LogPaths $selectedLogs
        Head 'Review prepared issue report'
        Say $report.Preview
        Say '  Redaction is best effort; unusual secrets can remain. Check the complete preview.' 'DarkGray'
        Say '  Opening a draft sends the prefilled text to GitHub; it does not create an issue.' 'DarkGray'
        $action = Read-Host '  [O] Open GitHub issue draft (reviewed)  [E] Export reviewed .txt report  [Enter] Cancel'
        if ($action -eq 'o') {
            $submission = New-SupportBrowserSubmission $report -UserReviewed
            if ($submission.NeedsAttachment) {
                Say '  This report needs one local support-report.txt attachment. Nothing is uploaded automatically.'
                $reportFolder = Read-Host '  Existing folder for support-report.txt (Enter to cancel)'
                if ([string]::IsNullOrWhiteSpace($reportFolder)) { Say '  Canceled. No draft was opened.'; return }
                $reportPath = Join-Path $reportFolder 'support-report.txt'
                Save-SupportReviewFile $report $reportPath -UserReviewed
                Say "  Saved this reviewed snapshot: $reportPath"
            }
            Open-HelpDestination $submission.Url 'GitHub issue draft'
            Say "  $($submission.Notice)"
        } elseif ($action -eq 'e') {
            $exportPath = Read-Host '  Save .txt path (must not exist, Enter to cancel)'
            if ([string]::IsNullOrWhiteSpace($exportPath)) { Say '  Canceled. No report was saved.'; return }
            Save-SupportReviewFile $report $exportPath -UserReviewed
            Ok "Saved reviewed report: $exportPath"
        } else {
            Say '  Canceled. No report was sent or saved.'
        }
    } catch { Warn $_.Exception.Message }
}

$page = 'Setup'
while ($true) {
    Head "Wheel settings - $page"
    Say "  View: $(if ($View -eq 'Simple') { '[Simple] Advanced' } else { 'Simple [Advanced]' })    Device: $($dev.Name)"
    Say '  [1] Setup  [2] Controls  [3] FFB  [4] Cameras  [5] Telemetry  [6] Help'
    if ($raw -cne $original) { Say '  Changes pending - Save mappings and exit to apply them.' 'Yellow' }
    switch ($page) {
        'Setup' {
            $missing = @('Steering','Throttle','Brake' | Where-Object { -not (Get-Field $AXES[$_]) })
            if ($missing.Count) { Say "  Next: Bind $($missing[0]). [B] Begin" }
            else { Say '  Main axes assigned. Run the game wheel calibration once, then drive.' }
            Say '  [2] Controls for optional handbrake, clutch and buttons.'
            Say '  Settings are applied when saved. The game must stay closed.'
        }
        'Controls' {
            $names = @($AXES.Keys)
            for ($i = 0; $i -lt 4; $i++) { Say "  [A$($i+1)] $($names[$i]): $(Assignment (Get-Field $AXES[$names[$i]]))" }
            Say '  [B] Driving and menu buttons  [C] Clutch  [P] Preview device input'
            Say '  Handbrake axis and button assignments are independent; final combination is controlled by the game.'
            if ($View -eq 'Advanced') { Say '  [R] Raw mapping details' }
        }
        'FFB' {
            Say '  Force feedback is provided by the game. Choose its wheel and Strength in the game settings.'
            Say '  This setup tool reads input only. It cannot enable, stop or test game force feedback.'
            if ($View -eq 'Advanced') { Say "  Profile force scale: $(Get-Field 'ForceFeedback'); rotation: $(Get-Field 'MaxRotationAngle') degrees" }
        }
        'Cameras' {
            Say "  Change camera: $(Assignment (Get-Field (Resolve-Slot 'Change camera')))"
            Say '  [B] Bind Change camera'
            Say '  The game provides the camera cycle. No added Bonnet/Bumper mounts.'
            Say '  On a triple layout in a race: numpad 8/2 forward/back, 9/3 up/down, 4/6 left/right,'
            Say '  7/1 tilt, +/- FOV, 5 reset. All three views move together.'
            Say '  [A] Adjustment steps (Advanced; next game launch)'
            if ($View -eq 'Advanced') {
                try {
                    $steps=Get-CameraSteps ([IO.File]::ReadAllText($telemetryPath))
                    Say "  Saved steps: move $($steps.move_step_m) m; tilt $($steps.tilt_step_deg) degrees; FOV $($steps.fov_step_deg) degrees."
                } catch { Warn $_.Exception.Message }
            }
        }
        'Telemetry' {
            try {
                $telemetry = (Read-TelemetrySnapshot).State
                $enabledLabel = if ($telemetry.enabled -eq '0') { '[Off] On' } elseif ($telemetry.enabled -eq '1') { 'Off [On]' } else { "Unrecognized saved value: $($telemetry.enabled)" }
                Say "  Telemetry: $enabledLabel    [O] Off  [N] On"
                Say "  Receiver: $(Get-TelemetryReceiver $telemetry.format)"
                Say "  Destination: $($telemetry.host):$($telemetry.port)"
                Say '  Saved configuration for the next game launch; runtime delivery unverified.'
                Say "  $(Get-TelemetryConnectionSummary $telemetry)"
                Say '  Match the receiver game and port in SimHub. Telemetry is optional.'
                Say '  [A] Connection settings (Advanced)'
                if ($View -eq 'Advanced') {
                    Say "  Configuration file: $telemetryPath"
                    Say '  Rate and channel diagnostics: docs\forza-format.md and docs\troubleshooting.md.'
                }
            } catch { Warn $_.Exception.Message; Say '  No telemetry setting has been changed.' }
        }
        'Help' {
            $links = Get-ModHelpLinks
            Say '  Close the game before setup. Select an axis to bind; Save mappings and exit writes a backup.'
            Say '  No movement? Reconnect the device and reopen setup. Wrong direction? Choose that axis, then Invert direction.'
            Say '  Gravel is hardware-verified. Other Milestone games still need validation.'
            Say '  The tool uses the terminal text size. Ctrl+C exits without saving pending mappings.'
            Say '  F6 is not available in this mod. Reopen WheelSetup.bat to change controls.'
            Say '  [I] Submit issue (review diagnostics before opening GitHub or saving .txt)'
            # One link per line so a 120-column console never wraps a URL.
            Say "  $($links.ModLabel): $($links.ModUrl)" 'DarkGray'
            Say "  $($links.WheelkitLabel): $($links.WheelkitUrl)" 'DarkGray'
            Say '  [M] Mod on GitHub  [W] Wheelkit on GitHub' 'DarkGray'
            if ($View -eq 'Advanced') { Say "  Device key: $($dev.Key) ($($dev.Type))"; Say "  Mapping file: $wc"; Say "  View preference: $PreferencesPath" }
        }
    }
    Say '  [V] Choose view  [S] Save mappings and exit  [X] Exit (discard pending mappings)'
    $command = Read-Host '  Choose'
    if ($command -match '^[1-6]$') { $page = @('Setup','Controls','FFB','Cameras','Telemetry','Help')[[int]$command - 1]; continue }
    switch ($command) {
        'i' { if ($page -eq 'Help') { Invoke-SupportIssueFlow } }
        'm' { if ($page -eq 'Help') { try { $links = Get-ModHelpLinks; Open-HelpDestination $links.ModUrl $links.ModLabel } catch { Warn $_.Exception.Message } } }
        'w' { if ($page -eq 'Help') { try { $links = Get-ModHelpLinks; Open-HelpDestination $links.WheelkitUrl $links.WheelkitLabel } catch { Warn $_.Exception.Message } } }
        { $_ -in @('o','n') } {
            if ($page -eq 'Telemetry') {
                try {
                    $snapshot = Read-TelemetrySnapshot
                    $value = if ($command -eq 'o') { '0' } else { '1' }
                    $candidate = Set-TelemetryFields $snapshot.Text @{ enabled = $value }
                    Write-TelemetryChange $snapshot.Text $candidate
                } catch { Warn "Telemetry was not changed. $($_.Exception.Message)" }
            }
        }
        'a' {
            if ($page -eq 'Telemetry') {
                try {
                    if ($View -ne 'Advanced') { Save-SettingsView $PreferencesPath 'Advanced'; $View = 'Advanced' }
                    Edit-TelemetryConnection
                } catch { Warn $_.Exception.Message }
            }
            elseif ($page -eq 'Cameras') {
                try {
                    if ($View -ne 'Advanced') { Save-SettingsView $PreferencesPath 'Advanced'; $View = 'Advanced' }
                    Edit-CameraSteps
                } catch { Warn $_.Exception.Message }
            }
        }
        'v' {
            $choice = Read-Host '  View: [1] Simple  [2] Advanced  [Enter] Cancel'
            $next = if ($choice -eq '1') { 'Simple' } elseif ($choice -eq '2') { 'Advanced' } else { $null }
            if ($next) {
                try { Save-SettingsView $PreferencesPath $next; $View = $next }
                catch { Warn "Could not save the view. $($_.Exception.Message)" }
            }
        }
        'b' {
            if ($page -eq 'Setup' -and $missing.Count) { Edit-Axis $missing[0] }
            elseif ($page -eq 'Cameras') { Edit-Button 'Change camera' }
            elseif ($page -eq 'Controls') {
                $names = @($FRIENDLY.Keys)
                for ($i = 0; $i -lt $names.Count; $i++) {
                    $slot = Resolve-Slot $names[$i]
                    $value = if ($slot) { Assignment (Get-Field $slot) } else { 'Unavailable - saved layout needed' }
                    Say "  [$($i+1)] $($names[$i]): $value"
                }
                $choice = Pick-Number '  Choose a button action' $names.Count
                if ($choice -ge 0) { Edit-Button $names[$choice] }
            }
        }
        'c' { if ($page -eq 'Controls') { Edit-Axis 'Clutch' } }
        'p' { if ($page -eq 'Controls') { Show-DeviceInputPreview } }
        { $_ -match '^a([1-4])$' } {
            if ($page -eq 'Controls') {
                $axisChoice = [int]$command.Substring(1) - 1
                Edit-Axis (@($AXES.Keys)[$axisChoice])
            }
        }
        'r' {
            if ($page -eq 'Controls' -and $View -eq 'Advanced') {
                Head 'Raw mapping details'
                Say (Get-WheelBlock $raw $dev.Key).Value
                Read-Host '  Enter to return' | Out-Null
            }
        }
        's' {
            try {
                if ($raw -cne $original) {
                    $running = Get-Process -EA SilentlyContinue | Where-Object { $_.Path -and $gameDir -and $_.Path.StartsWith($gameDir, [StringComparison]::OrdinalIgnoreCase) }
                    if ($running) { throw 'The game is running. Close it before saving.' }
                    $backup = Save-WheelConfig $wc $original $raw
                    Ok "Saved. Backup: $backup"
                    Say '  Now run the game wheel calibration once.'
                } else { Say '  No mappings changed.' }
                return
            } catch { Warn "Save failed. Your previous mappings are still on disk. $($_.Exception.Message)" }
        }
        'x' { Say '  Pending mappings discarded. Saved telemetry and your explicit view choice are kept.'; return }
        default { Warn 'Choose a listed action.' }
    }
}
