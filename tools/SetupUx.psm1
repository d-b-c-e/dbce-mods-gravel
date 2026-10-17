# Offline-testable presentation and mapping helpers. No device acquisition here.
Set-StrictMode -Version Latest
. (Join-Path (Split-Path $PSScriptRoot -Parent) 'lib\toolkit\support\SupportReport.ps1')

function Get-SettingsView([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path)) { return 'Simple' }
    try { $state = Get-Content -LiteralPath $Path -Raw | ConvertFrom-Json }
    catch { throw "Settings view could not be read. Your wheel mappings are unchanged. $($_.Exception.Message)" }
    if ($null -eq $state -or $state -isnot [pscustomobject]) { throw 'Settings view must be a JSON object. Your wheel mappings are unchanged.' }
    $property = $state.PSObject.Properties['View']
    if ($null -ne $property -and $property.Value -eq 'Advanced') { return 'Advanced' }
    return 'Simple'
}

function Save-SettingsView([string]$Path, [ValidateSet('Simple','Advanced')][string]$View) {
    $state = [pscustomobject]@{}
    $previousText = $null
    $repair = $false
    if (Test-Path -LiteralPath $Path) {
        # Only an explicit view selection reaches this function. A read failure
        # is not permission to repair; malformed JSON is, with its bytes backed up.
        $previousText = [IO.File]::ReadAllText($Path)
        try { $state = $previousText | ConvertFrom-Json }
        catch { $repair = $true }
        if ($null -eq $state -or $state -isnot [pscustomobject]) { $repair = $true }
        if ($repair) { $state = [pscustomobject]@{} }
    }
    $state | Add-Member -NotePropertyName View -NotePropertyValue $View -Force
    $folder = Split-Path $Path -Parent
    [IO.Directory]::CreateDirectory($folder) | Out-Null
    $temp = Join-Path $folder ('.view-' + [guid]::NewGuid().ToString('N') + '.tmp')
    try {
        [IO.File]::WriteAllText($temp, ($state | ConvertTo-Json -Depth 16))
        if (Test-Path -LiteralPath $Path) {
            if ([IO.File]::ReadAllText($Path) -cne $previousText) { throw 'The view preference changed in another setup window. Choose the view again.' }
            # Windows PowerShell 5.1 marshals a null backup path as an invalid empty string.
            $backup = if ($repair) { "$Path.invalid-$(Get-Date -Format 'yyyyMMdd-HHmmss')-$([guid]::NewGuid().ToString('N').Substring(0,6)).bak" } else { "$temp.previous" }
            [IO.File]::Replace($temp, $Path, $backup)
            if ($repair) { Write-Warning "View preference repaired. Previous file kept at: $backup. Game settings were not changed." }
            else { Remove-Item -LiteralPath $backup }
        }
        else { [IO.File]::Move($temp, $Path) }
    } finally { if (Test-Path -LiteralPath $temp) { Remove-Item -LiteralPath $temp } }
}

function Get-WheelBlock([string]$Text, [string]$Product) {
    [regex]::Match($Text, '(?s)\[/Wheel\.Config/' + [regex]::Escape($Product) + '\].*?(?=\r\n\[/Wheel\.Config/|\Z)')
}

function Get-WheelField([string]$Text, [string]$Product, [string]$Key) {
    $block = Get-WheelBlock $Text $Product
    $field = [regex]::Match($block.Value, '(?m)^' + [regex]::Escape($Key) + '=([^\r\n]*)')
    if ($field.Success) { return $field.Groups[1].Value }
    return ''
}

function Set-WheelField([string]$Text, [string]$Product, [string]$Key, [string]$Value) {
    $block = Get-WheelBlock $Text $Product
    if (-not $block.Success) { throw 'This device has no wheel profile.' }
    $field = [regex]::Match($block.Value, '(?m)^' + [regex]::Escape($Key) + '=[^\r\n]*')
    if (-not $field.Success) { throw "This game profile does not expose $Key. The assignment was kept." }
    if ($Value -match '[\r\n]') { throw 'A mapping must fit on one line.' }
    # Literal insertion preserves dollar signs, other devices and CRLF endings.
    $updated = $block.Value.Remove($field.Index, $field.Length).Insert($field.Index, "$Key=$Value")
    return $Text.Remove($block.Index, $block.Length).Insert($block.Index, $updated)
}

function Read-SavedWheelActions([byte[]]$b) {
    if ($b.Length -gt 4 * 1024 * 1024) { throw "The settings save exceeds the read-only binding size limit." }
    if ($b.Length -lt 4 -or [Text.Encoding]::ASCII.GetString($b, 0, 4) -ne 'GVAS') { return @{} }

    # Walk UE4 tagged properties: FString name, FString type, int64 size, u8 guid, payload
    $map = @{}
    $pos = 0
    $needle = [Text.Encoding]::ASCII.GetBytes('ignitioninput')
    for ($i = 0; $i -lt $b.Length - $needle.Length; $i++) {
        $hit = $true
        for ($j = 0; $j -lt $needle.Length; $j++) { if ($b[$i + $j] -ne $needle[$j]) { $hit = $false; break } }
        if ($hit) { $pos = $i; break }
    }
    if (-not $pos) { return @{} }

    function ReadStr([byte[]]$buf, [ref]$o) {
        if ($o.Value + 4 -gt $buf.Length) { return $null }
        $n = [BitConverter]::ToInt32($buf, $o.Value); $o.Value += 4
        if ($n -eq 0) { return '' }
        if ($n -gt 0 -and $n -lt 512 -and $o.Value + $n -le $buf.Length) {
            $s = [Text.Encoding]::ASCII.GetString($buf, $o.Value, $n - 1); $o.Value += $n; return $s
        }
        if ($n -lt 0 -and $n -gt -512 -and $o.Value + (-$n * 2) -le $buf.Length) {
            $n = -$n
            $s = [Text.Encoding]::Unicode.GetString($buf, $o.Value, ($n - 1) * 2); $o.Value += $n * 2; return $s
        }
        return $null
    }

    $o = $pos
    $action = $null
    while ($o -lt $b.Length - 8) {
        $save = $o
        $name = ReadStr $b ([ref]$o)
        if (-not $name -or $name -notmatch '^[\w]+$') { $o = $save + 1; continue }
        if ($name -eq 'None') { $action = $null; continue }
        $type = ReadStr $b ([ref]$o)
        if (-not $type -or $type -notlike '*Property') { $o = $save + 1; continue }
        if ($o + 9 -gt $b.Length) { break }
        $size = [BitConverter]::ToInt64($b, $o); $o += 9        # int64 size + guid flag
        if ($size -lt 0 -or $size -gt ($b.Length - $o)) { $o = $save + 1; continue }
        if ($name -eq 'ActionName') { $p = $o; $action = ReadStr $b ([ref]$p) }
        elseif ($name -eq 'KeyName' -and $action) {
            $p = $o; $key = ReadStr $b ([ref]$p)
            if ($key -and $key.StartsWith('Wheel_')) {
                if (-not $map.ContainsKey($action)) { $map[$action] = @() }
                if ($map[$action] -cnotcontains $key) { $map[$action] += $key }
            }
            $action = $null
        }
        $o += $size
    }
    $map
}

function Get-WheelButtonActions([hashtable]$SavedActions) {
    $actions = [ordered]@{
    'Gear up'          = @('GearUp',         'Wheel_RightShoulder')
    'Gear down'        = @('GearDown',       'Wheel_LeftShoulder')
    'Handbrake (button)' = @('Handbrake',    'Wheel_RightTrigger')
    'Rewind'           = @('RewindActivate', 'Wheel_LeftTrigger')
    'Change camera'    = @('SwitchCamera',   'Wheel_Special_Left')
    'Pause'            = @('Pause',          'Wheel_Special_Right')
    'Confirm'          = @('Confirm',        'Wheel_FaceButton_Bottom')
    'Back'             = @('Back',           'Wheel_FaceButton_Right')
    'Respawn'          = @('Respawn',        'Wheel_FaceButton_Top')
    'Photo mode'       = @('ReplayPhotoMode','Wheel_RightTrigger')
}
    # These actions have no guessed defaults: the game's own save must name them.
    $savedOnly = [ordered]@{ 'Reverse'='GearReverse'; 'Gear 1'='Gear1'; 'Gear 2'='Gear2';
        'Gear 3'='Gear3'; 'Gear 4'='Gear4'; 'Gear 5'='Gear5'; 'Gear 6'='Gear6';
        'Gear 7'='Gear7'; 'Gear 8'='Gear8'; 'Look back'='LookBackCamera' }
    foreach ($label in $savedOnly.Keys) {
        if ($SavedActions.ContainsKey($savedOnly[$label])) { $actions[$label] = @($savedOnly[$label], $null) }
    }
    return $actions
}

function Resolve-WheelActionSlot([object[]]$Definition, [hashtable]$SavedActions, [switch]$GravelDefaults) {
    $action, $fallback = $Definition
    if ($SavedActions.ContainsKey($action)) {
        $slots = @($SavedActions[$action] | Select-Object -Unique)
        if ($slots.Count -eq 1) { return $slots[0] }
        return $null # An ambiguous saved mapping must never fall back to a default.
    }
    if ($GravelDefaults) { return $fallback }
    return $null
}

function Get-WheelAxisFields([hashtable]$SavedActions) {
    $fields = [ordered]@{ 'Steering'='Wheel_Steer'; 'Throttle'='Wheel_Accelerator';
        'Brake'='Wheel_Brake'; 'Handbrake (axis)'='Wheel_Handbrake'; 'Clutch'='Wheel_Clutch' }
    foreach ($pair in @(@('Steering','Steer'), @('Throttle','Throttle'), @('Brake','Brake'), @('Clutch','Clutch'))) {
        $fields[$pair[0]] = Resolve-WheelActionSlot @($pair[1], $fields[$pair[0]]) $SavedActions -GravelDefaults
    }
    # The native handbrake axis is independent of the saved Handbrake button slot.
    return $fields
}

function Format-WheelAssignment([string]$Value, [switch]$Advanced) {
    if ([string]::IsNullOrWhiteSpace($Value)) { return 'Not bound' }
    if ($Value -match '^Button(\d+)$') { $label = "Button $($Matches[1])" }
    elseif ($Value -match '^([0-9a-fA-F]{8})_Button(\d+)$') { $label = "Button $($Matches[2]) (device $($Matches[1]))" }
    elseif ($Value -match '^Axis(\d+)(?:&(-?[0-9.]+)&(-?[0-9.]+))?$') {
        $label = "Axis $($Matches[1])"
        if ($Matches.ContainsKey(2) -and $Matches[2].StartsWith('-')) { $label += ' (inverted)' }
    } else { $label = 'Existing custom assignment' }
    if ($Advanced) { return "$label  [$Value]" }
    return $label
}

function Get-ButtonCaptureSource([string]$Assignment, [string]$WheelProduct, [object[]]$Devices) {
    $product = $WheelProduct; $prefix = ''
    if ($Assignment -match '^([0-9a-fA-F]{8})_Button(\d+)$') {
        if ([int]$Matches[2] -lt 1 -or [int]$Matches[2] -gt 128) { throw 'The existing paired-device button is outside the supported range. The assignment was kept.' }
        # Preserve an existing native pairing, never invent one for another device.
        $product = $Matches[1].ToLowerInvariant(); $prefix = $Matches[1] + '_'
    }
    $connected = @($Devices | Where-Object { $_.Key -eq $product })
    if ($connected.Count -ne 1) { throw "The binding's input device $product is missing or ambiguous. Reconnect that device; the assignment was kept." }
    return [pscustomobject]@{ Product=$product; Prefix=$prefix; Name=$connected[0].Name }
}

function Get-AxisSamples([string[]]$Lines, [switch]$AllowOutOfRange) {
    foreach ($line in $Lines) {
        if ($line -notmatch '^AX ((?:-?\d+ ){7}-?\d+)$') { continue }
        $values = @($Matches[1] -split ' ' | ForEach-Object { [int]$_ })
        if (-not $AllowOutOfRange -and @($values | Where-Object { $_ -lt 0 -or $_ -gt 65535 }).Count) { continue }
        # An object keeps each eight-value sample together in the pipeline.
        [pscustomobject]@{ Values = $values }
    }
}

function Get-DeviceInputPreview([string]$Mapping, [int[]]$Values, [switch]$Steering) {
    if ([string]::IsNullOrWhiteSpace($Mapping)) { return [pscustomobject]@{ Available=$false; Label='Not bound'; Value=$null; Raw=$null; Clamped=$false } }
    # Only the two transforms the setup tool creates are known here. Do not
    # guess the meaning of game-specific curves, button mappings or offsets.
    if ($Mapping -notmatch '^Axis([1-8])&(1(?:\.0+)?&0(?:\.0+)?|-1(?:\.0+)?&1(?:\.0+)?)$') {
        return [pscustomobject]@{ Available=$false; Label='Preview unavailable for this custom assignment'; Value=$null; Raw=$null; Clamped=$false }
    }
    if ($null -eq $Values -or $Values.Count -ne 8) { return [pscustomobject]@{ Available=$false; Label='Waiting for device input'; Value=$null; Raw=$null; Clamped=$false } }
    $axis = [int]$Matches[1] - 1
    $inverted = $Matches[2].StartsWith('-')
    $raw = $Values[$axis]
    $normalized = [Math]::Max(0.0, [Math]::Min(1.0, $raw / 65535.0))
    if ($inverted) { $normalized = 1.0 - $normalized }
    if ($Steering) {
        $value = $normalized * 2.0 - 1.0
        $percent = [Math]::Round([Math]::Abs($value) * 100)
        $label = if ($percent -eq 0) { 'Center' } elseif ($value -lt 0) { "Left $percent%" } else { "Right $percent%" }
    } else {
        $value = $normalized
        $percent = [Math]::Round($value * 100)
        $label = if ($percent -eq 0) { 'Released (0%)' } elseif ($percent -eq 100) { 'Full (100%)' } else { "$percent%" }
    }
    [pscustomobject]@{ Available=$true; Label=$label; Value=$value; Raw=$raw; Clamped=($raw -lt 0 -or $raw -gt 65535) }
}

function Read-WheelInput([string]$Probe, [string]$Product, [ValidateRange(1,30)][int]$Seconds, [scriptblock]$CancellationRequested) {
    # One owner for one input-only process. All callers get the same bounded
    # cleanup on Esc, device error, native exit or a helper that stops responding.
    $info = New-Object Diagnostics.ProcessStartInfo
    $info.FileName = $Probe
    $info.Arguments = "$Product $Seconds"
    $info.UseShellExecute = $false
    $info.CreateNoWindow = $true
    $info.RedirectStandardOutput = $true
    $info.RedirectStandardError = $true
    $process = [Diagnostics.Process]::Start($info)
    $errors = $process.StandardError.ReadToEndAsync()
    $timer = [Diagnostics.Stopwatch]::StartNew()
    $lastSample = 0.0
    try {
        $pending = $process.StandardOutput.ReadLineAsync()
        while ($true) {
            $cancel = $false
            if ($CancellationRequested) { $cancel = [bool](& $CancellationRequested) }
            else { try { if ([Console]::KeyAvailable) { $cancel = [Console]::ReadKey($true).Key -eq 'Escape' } } catch { } }
            if ($cancel) { throw [OperationCanceledException]::new('Canceled. No assignment was changed.') }
            if ($timer.Elapsed.TotalSeconds -gt ($Seconds + 1)) { throw [TimeoutException]::new('The input reader timed out. Reconnect the device and try again.') }
            # wheelprobe sends an AX heartbeat every second even without movement.
            if ($timer.Elapsed.TotalSeconds - $lastSample -gt 2.5) { throw [IO.IOException]::new('Device samples stopped. Check the connection and retry.') }
            if (-not $pending.Wait(50)) { continue }
            $line = $pending.Result
            if ($null -eq $line) { break }
            if ($line -like 'ERR *') { throw $line.Substring(4) }
            if ($line.StartsWith('AX ')) { $lastSample = $timer.Elapsed.TotalSeconds }
            $line
            $pending = $process.StandardOutput.ReadLineAsync()
        }
        $remainingMs = [Math]::Max(1, [int](($Seconds + 1 - $timer.Elapsed.TotalSeconds) * 1000))
        if (-not $process.WaitForExit($remainingMs)) { throw [TimeoutException]::new('The input reader timed out. Reconnect the device and try again.') }
        if ($process.ExitCode -ne 0) { throw "Input reader stopped. Reconnect the device and try again. $($errors.Result)" }
    } finally {
        if (-not $process.HasExited) { $process.Kill(); $process.WaitForExit() }
        $process.Dispose()
    }
}

function Find-AxisMapping([string[]]$RestLines, [string[]]$TravelLines, [switch]$Steering) {
    $rest = @(Get-AxisSamples $RestLines)
    $travel = @(Get-AxisSamples $TravelLines)
    if (-not $rest.Count -or -not $travel.Count) { throw 'No input received. Check the device connection and try again.' }
    $baseline = $rest[-1].Values
    $best = -1; $bestTravel = 0; $secondTravel = 0; $full = 0
    for ($axis = 0; $axis -lt 8; $axis++) {
        $values = @($travel | ForEach-Object { $_.Values[$axis] })
        $range = $values | Measure-Object -Minimum -Maximum
        if ($Steering) { $movement = $range.Maximum - $range.Minimum }
        else { $movement = [Math]::Max([Math]::Abs($range.Maximum - $baseline[$axis]), [Math]::Abs($range.Minimum - $baseline[$axis])) }
        if ($movement -gt $bestTravel) {
            $secondTravel = $bestTravel; $bestTravel = $movement; $best = $axis
            $full = if (($range.Maximum - $baseline[$axis]) -ge ($baseline[$axis] - $range.Minimum)) { $range.Maximum } else { $range.Minimum }
        } elseif ($movement -gt $secondTravel) { $secondTravel = $movement }
    }
    if ($bestTravel -lt 3000) { throw 'No clear movement detected. The previous assignment was kept.' }
    if ($secondTravel -ge ($bestTravel * 0.6)) { throw 'More than one axis moved. Move only the requested control and try again.' }
    $inverted = -not $Steering -and $full -lt $baseline[$best]
    $polarity = if ($inverted) { '-1.0&1.0' } else { '1.0&0.0' }
    [pscustomobject]@{ Axis = $best; Value = "Axis$($best + 1)&$polarity"; Inverted = $inverted; Rest = $baseline[$best]; Full = $full }
}

function Save-WheelConfig([string]$Path, [string]$Original, [string]$Text) {
    if ([IO.File]::ReadAllText($Path) -cne $Original) { throw 'The game or another tool changed the mappings. Close setup and reopen it before saving.' }
    if ($Text -match '(?<!\r)\n') { throw 'The mappings contain invalid line endings; nothing was saved.' }
    $temp = Join-Path (Split-Path $Path -Parent) ('.wheel-' + [guid]::NewGuid().ToString('N') + '.tmp')
    $backup = "$Path.bak-$(Get-Date -Format 'yyyyMMdd-HHmmss')-$([guid]::NewGuid().ToString('N').Substring(0,6))"
    try {
        [IO.File]::WriteAllText($temp, $Text)
        [IO.File]::Replace($temp, $Path, $backup)
    } finally { if (Test-Path -LiteralPath $temp) { Remove-Item -LiteralPath $temp } }
    return $backup
}

function Get-ModSetting([string]$Text, [string]$Section, [string]$Key) {
    $block = [regex]::Match($Text, '(?ms)^\[' + [regex]::Escape($Section) + '\][^\r\n]*\r?\n.*?(?=^\[|\z)')
    $field = [regex]::Match($block.Value, '(?m)^' + [regex]::Escape($Key) + '=([^\r\n]*)')
    if ($field.Success) { return $field.Groups[1].Value.Trim() }
    return ''
}

function Set-ModSetting([string]$Text, [string]$Section, [string]$Key, [string]$Value) {
    $block = [regex]::Match($Text, '(?ms)^\[' + [regex]::Escape($Section) + '\][^\r\n]*\r?\n.*?(?=^\[|\z)')
    if (-not $block.Success) { throw "Missing [$Section] configuration section. Existing settings were kept." }
    $field = [regex]::Match($block.Value, '(?m)^' + [regex]::Escape($Key) + '=[^\r\n]*')
    if (-not $field.Success) { throw "Missing $Key in [$Section]. Existing settings were kept." }
    $updated = $block.Value.Remove($field.Index, $field.Length).Insert($field.Index, "$Key=$Value")
    return $Text.Remove($block.Index, $block.Length).Insert($block.Index, $updated)
}

function Get-TelemetryBlock([string]$Text) {
    $blocks = [regex]::Matches($Text, '(?ims)^[ \t]*\[telemetry\][ \t]*\r?\n.*?(?=^[ \t]*\[|\z)')
    if ($blocks.Count -ne 1) { throw 'The settings need one [telemetry] section. Restore a known working configuration before editing.' }
    return $blocks[0]
}

function Get-TelemetryState([string]$Text) {
    $block = Get-TelemetryBlock $Text
    $values = @{}
    foreach ($key in 'enabled','host','port','format') {
        $fields = [regex]::Matches($block.Value, '(?im)^[ \t]*' + $key + '[ \t]*=([^\r\n]*)')
        if ($fields.Count -ne 1) { throw "Telemetry needs exactly one $key setting. Restore a known working configuration before editing." }
        $values[$key] = $fields[0].Groups[1].Value.Trim()
    }
    return [pscustomobject]$values
}

function Assert-TelemetryConnection([string]$Address, [string]$Port, [string]$Format) {
    # The unchanged runtime uses inet_pton(AF_INET), with no hostname lookup.
    if ($Address -notmatch '^\d{1,3}(\.\d{1,3}){3}$') { throw 'Use a dotted IPv4 address, such as 127.0.0.1 for this PC. Hostnames and IPv6 are not supported by this mod.' }
    foreach ($part in ($Address -split '\.')) {
        if ([int]$part -gt 255 -or ($part.Length -gt 1 -and $part.StartsWith('0'))) { throw 'The receiver IPv4 address is invalid. Use four numbers from 0 to 255 without leading zeros.' }
    }
    if ($Address -eq '0.0.0.0' -or $Address -eq '255.255.255.255' -or [int]($Address -split '\.')[0] -ge 224) { throw 'Choose the receiver PC address. Unspecified, multicast and broadcast destinations are not supported here.' }
    $number = 0
    if ($Port -notmatch '^\d+$' -or -not [int]::TryParse($Port, [ref]$number) -or $number -lt 1 -or $number -gt 65535) { throw 'Port must be a whole number from 1 to 65535, matching the receiver.' }
    if ($Format -notin @('fh4','fm7','sled')) { throw 'Choose Forza Horizon 4/5, Forza Motorsport 7, or the Sled receiver format.' }
}

function Get-TelemetryReceiver([string]$Format) {
    switch ($Format) {
        'fh4' { return 'Forza Horizon 4 / 5 (324 bytes)' }
        'fm7' { return 'Forza Motorsport 7 (311 bytes)' }
        'sled' { return 'Forza Sled (232 bytes; physics only)' }
        default { return "Unrecognized saved format: $Format" }
    }
}

function Get-TelemetryConnectionSummary($State) {
    try { Assert-TelemetryConnection $State.host $State.port $State.format }
    catch { return 'Connection needs review in Advanced; saved values were kept.' }
    if ($State.host -eq '127.0.0.1' -and $State.port -eq '5300') {
        if ($State.format -eq 'fh4') { return 'Matches the fresh-installer connection preset.' }
        if ($State.format -eq 'fm7') { return 'Matches the shipped Gravel connection preset.' }
    }
    return 'Custom connection active. Review in Advanced.'
}

function Set-TelemetryFields([string]$Text, [hashtable]$Changes) {
    $state = Get-TelemetryState $Text
    foreach ($key in $Changes.Keys) {
        if ($key -notin @('enabled','host','port','format')) { throw "Telemetry edit cannot change $key." }
        if ([string]$Changes[$key] -match '[\r\n]') { throw 'A telemetry value must fit on one line.' }
    }
    if ($Changes.ContainsKey('enabled') -and [string]$Changes.enabled -notin @('0','1')) { throw 'Telemetry must be Off or On.' }
    $connectionKeys = @($Changes.Keys | Where-Object { $_ -in @('host','port','format') })
    if ($connectionKeys.Count -gt 0 -and $connectionKeys.Count -ne 3) { throw 'Apply the receiver address, port and format together.' }
    if ($connectionKeys.Count) { Assert-TelemetryConnection $Changes.host $Changes.port $Changes.format }
    elseif ($Changes.ContainsKey('enabled') -and [string]$Changes.enabled -eq '1') { Assert-TelemetryConnection $state.host $state.port $state.format }
    foreach ($key in $Changes.Keys) {
        $block = Get-TelemetryBlock $Text
        $field = [regex]::Match($block.Value, '(?im)^([ \t]*' + $key + '[ \t]*=)([^\r\n]*)')
        $updated = $block.Value.Remove($field.Index, $field.Length).Insert($field.Index, $field.Groups[1].Value + [string]$Changes[$key])
        $Text = $Text.Remove($block.Index, $block.Length).Insert($block.Index, $updated)
    }
    return $Text
}

function Get-CameraSteps([string]$Text) {
    $blocks = [regex]::Matches($Text, '(?ims)^[ \t]*\[camera\][ \t]*\r?\n.*?(?=^[ \t]*\[|\z)')
    if ($blocks.Count -ne 1) { throw 'Camera settings need one [camera] section. Existing settings were kept.' }
    $values = @{}
    foreach ($key in 'move_step_m','tilt_step_deg','fov_step_deg') {
        $fields = [regex]::Matches($blocks[0].Value, '(?im)^[ \t]*' + $key + '[ \t]*=([^\r\n]*)')
        if ($fields.Count -ne 1) { throw "Camera settings need exactly one $key. Existing settings were kept." }
        $values[$key] = $fields[0].Groups[1].Value.Trim()
    }
    return [pscustomobject]$values
}

function Set-CameraSteps([string]$Text, [hashtable]$Changes) {
    $state = Get-CameraSteps $Text
    $keys = @('move_step_m','tilt_step_deg','fov_step_deg')
    if ($Changes.Count -ne 3 -or @($Changes.Keys | Where-Object { $_ -notin $keys }).Count) { throw 'Apply all three camera step sizes together.' }
    $values = @{}
    foreach ($key in $keys) {
        $number = 0.0
        $raw = [string]$Changes[$key]
        $max = if ($key -eq 'move_step_m') { 1.0 } else { 45.0 }
        if ($raw -match '[\r\n]' -or ![double]::TryParse($raw, [Globalization.NumberStyles]::Float, [Globalization.CultureInfo]::InvariantCulture, [ref]$number) -or
            [double]::IsNaN($number) -or [double]::IsInfinity($number) -or $number -le 0 -or $number -gt $max) {
            throw "Use a positive $key value no greater than $max, with a decimal point. No camera setting was changed."
        }
        $prior = 0.0
        $same = [double]::TryParse([string]$state.$key, [Globalization.NumberStyles]::Float, [Globalization.CultureInfo]::InvariantCulture, [ref]$prior) -and $prior -eq $number
        if (!$same) { $values[$key] = $number.ToString('R', [Globalization.CultureInfo]::InvariantCulture) }
    }
    foreach ($key in $values.Keys) {
        $block = [regex]::Match($Text, '(?ims)^[ \t]*\[camera\][ \t]*\r?\n.*?(?=^[ \t]*\[|\z)')
        $field = [regex]::Match($block.Value, '(?im)^([ \t]*' + $key + '[ \t]*=)([^\r\n]*)')
        $updated = $block.Value.Remove($field.Index, $field.Length).Insert($field.Index, $field.Groups[1].Value + $values[$key])
        $Text = $Text.Remove($block.Index, $block.Length).Insert($block.Index, $updated)
    }
    return $Text
}

function Save-ModConfig([string]$Path, [string]$Original, [string]$Text, [string]$Label) {
    if ($Text -ceq $Original) { return $null }
    if ([IO.File]::ReadAllText($Path) -cne $Original) { throw "Another tool changed the $Label settings. Cancel and reopen the editor before applying." }
    # Keep the installed INI's Unicode encoding/BOM for both existing telemetry and camera edits.
    $bytes = [IO.File]::ReadAllBytes($Path)
    $encoding = New-Object Text.UTF8Encoding($false, $true)
    $prefix = 0
    if ($bytes.Length -ge 3 -and $bytes[0] -eq 239 -and $bytes[1] -eq 187 -and $bytes[2] -eq 191) { $encoding = New-Object Text.UTF8Encoding($true, $true); $prefix=3 }
    elseif ($bytes.Length -ge 2 -and $bytes[0] -eq 255 -and $bytes[1] -eq 254) { $encoding = New-Object Text.UnicodeEncoding($false, $true, $true); $prefix=2 }
    elseif ($bytes.Length -ge 2 -and $bytes[0] -eq 254 -and $bytes[1] -eq 255) { $encoding = New-Object Text.UnicodeEncoding($true, $true, $true); $prefix=2 }
    if ($encoding.GetString($bytes, $prefix, $bytes.Length-$prefix) -cne $Original) { throw 'The configuration changed or its encoding is unsupported. Reopen the editor before applying.' }
    $temp = Join-Path (Split-Path $Path -Parent) ('.mod-settings-' + [guid]::NewGuid().ToString('N') + '.tmp')
    $backup = "$Path.bak-$(Get-Date -Format 'yyyyMMdd-HHmmss')-$([guid]::NewGuid().ToString('N').Substring(0,6))"
    try {
        [IO.File]::WriteAllText($temp, $Text, $encoding)
        [IO.File]::Replace($temp, $Path, $backup)
    } catch {
        throw [IO.IOException]::new("Could not save $Label. Close the game or other editor, then retry. The previous settings were kept.", $_.Exception)
    } finally { if (Test-Path -LiteralPath $temp) { Remove-Item -LiteralPath $temp } }
    return $backup
}

function Save-TelemetryConfig([string]$Path, [string]$Original, [string]$Text) {
    return Save-ModConfig $Path $Original $Text 'telemetry'
}

function Save-CameraSteps([string]$Path, [string]$Original, [hashtable]$Changes) {
    return Save-ModConfig $Path $Original (Set-CameraSteps $Original $Changes) 'camera'
}

function Get-ModHelpLinks {
    [pscustomobject]@{
        ModLabel      = 'Mod on GitHub'
        ModUrl        = 'https://github.com/d-b-c-e/dbce-mods-gravel'
        WheelkitLabel = 'Wheelkit on GitHub'
        WheelkitUrl   = 'https://github.com/d-b-c-e/dbce-wheelkit'
        Repository    = 'd-b-c-e/dbce-mods-gravel'
    }
}

Export-ModuleMember -Function Read-SavedWheelActions,Get-WheelButtonActions,Resolve-WheelActionSlot,Get-WheelAxisFields,Get-ButtonCaptureSource,Get-SettingsView,Save-SettingsView,Get-WheelBlock,Get-WheelField,Set-WheelField,Format-WheelAssignment,Get-AxisSamples,Get-DeviceInputPreview,Read-WheelInput,Find-AxisMapping,Save-WheelConfig,Get-ModSetting,Set-ModSetting,Get-TelemetryState,Assert-TelemetryConnection,Get-TelemetryReceiver,Get-TelemetryConnectionSummary,Set-TelemetryFields,Save-TelemetryConfig,Get-CameraSteps,Set-CameraSteps,Save-CameraSteps,Get-ModHelpLinks,Format-SupportRedactedText,Read-SupportLogTail,New-PreparedSupportReport,New-SupportBrowserSubmission,Save-SupportReviewFile
