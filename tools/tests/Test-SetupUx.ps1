param()
$ErrorActionPreference = 'Stop'
$repo = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
Import-Module (Join-Path $repo 'tools\SetupUx.psm1') -Force
$fixture = Join-Path ([IO.Path]::GetTempPath()) ('milestone-ux-' + [guid]::NewGuid().ToString('N'))
[IO.Directory]::CreateDirectory($fixture) | Out-Null
$checks = 0
function Assert($Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
    $script:checks++
}
function Assert-Throws([scriptblock]$Action, [string]$Message) {
    $threw = $false
    try { & $Action | Out-Null } catch { $threw = $true }
    Assert $threw $Message
}
$oldLocal = $env:LOCALAPPDATA
$oldFixture = $env:MILESTONE_UX_FIXTURE
$oldPreviewMode = $env:MILESTONE_UX_PREVIEW
$oldProbePid = $env:MILESTONE_UX_PROBE_PID
try {
    $preference = Join-Path $fixture 'settings-view.json'
    Assert ((Get-SettingsView $preference) -eq 'Simple') 'New users must start in Simple.'
    [IO.File]::WriteAllText($preference, '{}')
    Assert ((Get-SettingsView $preference) -eq 'Simple') 'An empty preference object must start in Simple.'
    [IO.File]::WriteAllText($preference, '{"Other":"kept"}')
    Assert ((Get-SettingsView $preference) -eq 'Simple') 'Upgrades without a view must start in Simple.'
    [IO.File]::WriteAllText($preference, '{"Other":"kept","View":"Unknown"}')
    Assert ((Get-SettingsView $preference) -eq 'Simple') 'Unknown view must fall back to Simple.'
    Save-SettingsView $preference 'Advanced'
    Assert ((Get-SettingsView $preference) -eq 'Advanced') 'Explicit Advanced must persist.'
    Assert ((Get-Content $preference -Raw | ConvertFrom-Json).Other -eq 'kept') 'View save lost another preference.'
    Save-SettingsView $preference 'Simple'
    Assert ((Get-SettingsView $preference) -eq 'Simple') 'Explicit Simple must persist.'
    $lock = [IO.File]::Open($preference, 'Open', 'Read', 'None')
    try { Assert-Throws { Save-SettingsView $preference 'Advanced' } 'A denied view save must report failure.' }
    finally { $lock.Dispose() }
    Assert ((Get-SettingsView $preference) -eq 'Simple') 'A failed view save changed the previous choice.'
    [IO.File]::WriteAllText($preference, '{broken')
    Assert-Throws { Get-SettingsView $preference } 'Malformed preference needs a visible failure.'
    $invalidPreference = [IO.File]::ReadAllBytes($preference)
    $lock = [IO.File]::Open($preference, 'Open', 'Read', [IO.FileShare]::ReadWrite)
    try { Assert-Throws { Save-SettingsView $preference 'Advanced' } 'A denied preference repair must report failure.' }
    finally { $lock.Dispose() }
    Assert ([Convert]::ToBase64String([IO.File]::ReadAllBytes($preference)) -eq [Convert]::ToBase64String($invalidPreference)) 'A failed repair changed the invalid preference bytes.'
    Save-SettingsView $preference 'Advanced'
    Assert ((Get-SettingsView $preference) -eq 'Advanced') 'Explicit view selection must recover a malformed preference.'
    $invalidBackup = @(Get-ChildItem $fixture -Filter 'settings-view.json.invalid-*.bak')
    Assert ($invalidBackup.Count -eq 1) 'Successful repair must keep one reported invalid-file backup.'
    Assert ([Convert]::ToBase64String([IO.File]::ReadAllBytes($invalidBackup[0].FullName)) -eq [Convert]::ToBase64String($invalidPreference)) 'Repair backup did not preserve the invalid bytes.'
    Remove-Item -LiteralPath $preference
    Assert-Throws { Save-SettingsView (Join-Path $preference 'child.json') 'Wrong' } 'Invalid view cannot be saved.'

    $text = "[/Wheel.Config/0006346e]`r`nProductName=Fixture wheel`r`nWheel_Steer=Axis1&1.0&0.0`r`nWheel_Accelerator=Axis3&1.0&0.0`r`nWheel_Brake=Axis6&1.0&0.0`r`nWheel_Handbrake=Axis7&1.0&0.0`r`nWheel_Clutch=`r`nWheel_RightTrigger=Button9`r`nWheel_Special_Left=Button8`r`nForceFeedback=0.0`r`nMaxRotationAngle=540.0`r`nCustomOwnerValue=42`r`n`r`n[/Wheel.Config/second]`r`nWheel_Handbrake=Axis2&1.0&0.0`r`n"
    $changed = Set-WheelField $text '0006346e' 'Wheel_Handbrake' 'Axis3&-1.0&1.0'
    Assert ((Get-WheelField $changed '0006346e' 'Wheel_RightTrigger') -eq 'Button9') 'Axis edits must preserve handbrake button.'
    Assert ((Get-WheelField $changed 'second' 'Wheel_Handbrake') -eq 'Axis2&1.0&0.0') 'Other device was changed.'
    Assert ((Get-WheelField $changed '0006346e' 'ForceFeedback') -eq '0.0') 'FFB Off must be preserved.'
    Assert ($changed -notmatch '(?<!\r)\n') 'CRLF was damaged.'
    Assert-Throws { Set-WheelField $text '0006346e' 'Unknown' '1' } 'Missing fields must not report a false save.'
    Assert ((Format-WheelAssignment 'Button128') -eq 'Button 128') 'Button labels must be one-based.'
    Assert ((Format-WheelAssignment 'Axis3&-1.0&1.0') -eq 'Axis 3 (inverted)') 'Simple must not dump raw transforms.'

    $low = Find-AxisMapping @('AX 0 0 0 0 0 0 0 0') @('AX 0 0 65535 0 0 0 0 0')
    Assert ($low.Value -eq 'Axis3&1.0&0.0') 'Low-rest polarity incorrect.'
    $high = Find-AxisMapping @('AX 0 0 65535 0 0 0 0 0') @('AX 0 0 0 0 0 0 0 0')
    Assert ($high.Value -eq 'Axis3&-1.0&1.0') 'High-rest polarity incorrect.'
    Assert-Throws { Find-AxisMapping @('AX 0 0 0 0 0 0 0 0') @('AX 0 0 0 0 0 0 0 0') } 'No movement must preserve the old mapping.'
    Assert-Throws { Find-AxisMapping @('AX 0 0 0 0 0 0 0 0') @('AX 65535 65535 0 0 0 0 0 0') } 'Ambiguous movement must not bind arbitrarily.'
    Assert-Throws { Find-AxisMapping @('AX -1 0 0 0 0 0 0 0') @('AX 0 0 65535 0 0 0 0 0') } 'Malformed device input must fail visibly.'

    $neutralPreview = Get-DeviceInputPreview 'Axis1&1.0&0.0' @(32767,0,0,0,0,0,0,0) -Steering
    Assert ($neutralPreview.Label -eq 'Centre') 'Steering neutral should be readable as Centre.'
    Assert ((Get-DeviceInputPreview 'Axis1&1.0&0.0' @(0,0,0,0,0,0,0,0) -Steering).Label -eq 'Left 100%') 'Steering left preview is incorrect.'
    Assert ((Get-DeviceInputPreview 'Axis1&1.0&0.0' @(65535,0,0,0,0,0,0,0) -Steering).Label -eq 'Right 100%') 'Steering right preview is incorrect.'
    Assert ((Get-DeviceInputPreview 'Axis1&-1.0&1.0' @(0,0,0,0,0,0,0,0) -Steering).Label -eq 'Right 100%') 'Steering inversion was not applied to preview.'
    Assert ((Get-DeviceInputPreview 'Axis3&1.0&0.0' @(0,0,0,0,0,0,0,0)).Label -eq 'Released (0%)') 'Pedal released preview is incorrect.'
    Assert ((Get-DeviceInputPreview 'Axis3&1.0&0.0' @(0,0,65535,0,0,0,0,0)).Label -eq 'Full (100%)') 'Pedal full preview is incorrect.'
    Assert ((Get-DeviceInputPreview 'Axis3&-1.0&1.0' @(0,0,65535,0,0,0,0,0)).Label -eq 'Released (0%)') 'Inverted pedal rest preview is incorrect.'
    Assert ((Get-DeviceInputPreview 'Axis3&-1.0&1.0' @(0,0,0,0,0,0,0,0)).Label -eq 'Full (100%)') 'Inverted pedal full preview is incorrect.'
    $clamped = Get-DeviceInputPreview 'Axis3&1.0&0.0' @(0,0,90000,0,0,0,0,0)
    Assert ($clamped.Value -eq 1.0 -and $clamped.Clamped) 'Out-of-range high input must be clamped and labeled.'
    $clamped = Get-DeviceInputPreview 'Axis3&1.0&0.0' @(0,0,-5000,0,0,0,0,0)
    Assert ($clamped.Value -eq 0.0 -and $clamped.Clamped) 'Out-of-range low input must be clamped and labeled.'
    Assert (-not (Get-DeviceInputPreview 'Axis3&0.5&0.2' @(0,0,10000,0,0,0,0,0)).Available) 'Custom transforms must not be guessed.'
    Assert (-not (Get-DeviceInputPreview 'Button9' @(0,0,10000,0,0,0,0,0)).Available) 'A button mapping must not pretend to be an axis.'
    Assert ((Get-DeviceInputPreview '' @(0,0,0,0,0,0,0,0)).Label -eq 'Not bound') 'Unbound preview must be explicit.'

    $wc = Join-Path $fixture 'Gravel\gravel\Config\WindowsNoEditor\WheelConfig.ini'
    [IO.Directory]::CreateDirectory((Split-Path $wc -Parent)) | Out-Null
    [IO.File]::WriteAllText($wc, $text)
    $backup = Save-WheelConfig $wc $text $changed
    Assert ([IO.File]::ReadAllText($backup) -ceq $text) 'Save must back up original mappings.'
    Assert ([IO.File]::ReadAllText($wc) -ceq $changed) 'Save did not apply exactly the candidate.'
    Assert-Throws { Save-WheelConfig $wc $text $changed } 'Concurrent owner edits must not be overwritten.'
    [IO.File]::WriteAllText($wc, $text)
    $lock = [IO.File]::Open($wc, 'Open', 'Read', 'None')
    try { Assert-Throws { Save-WheelConfig $wc $text $changed } 'A denied mapping save must report failure.' }
    finally { $lock.Dispose() }
    Assert ([IO.File]::ReadAllText($wc) -ceq $text) 'A failed mapping save changed the old bindings.'

    # A tiny fixture executable emits recorded input text. It never loads DirectInput.
    $probeFolder = Join-Path $fixture 'dist'
    [IO.Directory]::CreateDirectory($probeFolder) | Out-Null
    $source = Join-Path $fixture 'probe.cs'
    [IO.File]::WriteAllText($source, @'
using System;
using System.IO;
class FixtureProbe {
    static void Main(string[] args) {
        if (args[0] == "DEVICES") { Console.WriteLine("DEV 0006346e DRIVING 1 Fixture wheel"); return; }
        string file = Environment.GetEnvironmentVariable("MILESTONE_UX_FIXTURE");
        int count = File.Exists(file) ? Int32.Parse(File.ReadAllText(file)) : 0;
        File.WriteAllText(file, (count + 1).ToString());
        string pidFile = Environment.GetEnvironmentVariable("MILESTONE_UX_PROBE_PID");
        if (!String.IsNullOrEmpty(pidFile)) File.WriteAllText(pidFile, System.Diagnostics.Process.GetCurrentProcess().Id.ToString());
        string mode = Environment.GetEnvironmentVariable("MILESTONE_UX_PREVIEW");
        if (mode == "hang") { System.Threading.Thread.Sleep(10000); return; }
        if (mode == "disconnect") { Console.WriteLine("ERR fixture device disconnected"); Environment.Exit(2); }
        Console.WriteLine(count % 2 == 0 ? "AX 0 0 65535 0 0 0 0 0" : "AX 0 0 0 0 0 0 0 0");
    }
}
'@)
    $compiler = Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319\csc.exe'
    & $compiler /nologo /target:exe "/out:$probeFolder\wheelprobe.exe" $source
    if ($LASTEXITCODE) { throw 'Fixture input-reader compilation failed.' }
    $env:MILESTONE_UX_FIXTURE = Join-Path $fixture 'sample-count.txt'
    $env:MILESTONE_UX_PROBE_PID = Join-Path $fixture 'probe-pid.txt'
    $env:MILESTONE_UX_PREVIEW = ''
    $env:LOCALAPPDATA = Join-Path $fixture 'local'
    [IO.Directory]::CreateDirectory($env:LOCALAPPDATA) | Out-Null
    foreach ($sub in 'tools','lib\toolkit\powershell','games\gravel') { [IO.Directory]::CreateDirectory((Join-Path $fixture $sub)) | Out-Null }
    Copy-Item (Join-Path $repo 'WheelSetup.ps1') $fixture
    Copy-Item (Join-Path $repo 'Install.ps1') $fixture
    Copy-Item (Join-Path $repo 'Uninstall.ps1') $fixture
    Copy-Item (Join-Path $repo 'tools\SetupUx.psm1') (Join-Path $fixture 'tools')
    Import-Module (Join-Path $repo 'tools\InstallPackage.psm1') -Force
    foreach ($relative in Get-PackageFiles) {
        if ($relative -in @('dist\wheelprobe.exe','lib\toolkit\powershell\DbceWheel.psm1')) { continue }
        $target = Join-Path $fixture $relative
        [IO.Directory]::CreateDirectory((Split-Path $target -Parent)) | Out-Null
        Copy-Item -LiteralPath (Join-Path $repo $relative) -Destination $target
    }
    Copy-Item (Join-Path $repo 'games\gravel\milestone_mod.ini') (Join-Path $fixture 'games\gravel')
    [IO.File]::WriteAllText((Join-Path $fixture 'lib\toolkit\powershell\DbceWheel.psm1'), 'function Get-SteamLibraries { @() }; Export-ModuleMember -Function Get-SteamLibraries')
    [IO.File]::WriteAllText((Join-Path $fixture 'dist\dinput8.dll'), 'milestone_mod fixture - never loaded')
    $fixtureFiles = @(Get-PackageFiles | ForEach-Object { [pscustomobject]@{ Path=$_.Replace('\','/'); SHA256=(Get-FileHash -LiteralPath (Join-Path $fixture $_)).Hash } })
    $fixtureManifest = @{ Product='milestone-wheel-tools'; Version=([IO.File]::ReadAllText((Join-Path $fixture 'VERSION'))).Trim(); SourceCommit=('a'*40); Files=$fixtureFiles }
    [IO.File]::WriteAllText((Join-Path $fixture 'package-manifest.json'), ($fixtureManifest | ConvertTo-Json -Depth 6))
    function Read-Host($Prompt) {
        if (-not $global:MilestoneUxFixtureAnswers.Count) { throw "Fixture prompt was not expected: $Prompt" }
        $value = $global:MilestoneUxFixtureAnswers.Dequeue()
        Write-Host "  $Prompt : $value"
        return $value
    }
    function Get-Process { @() }
    function Run-Setup([string[]]$Choices) {
        $global:MilestoneUxFixtureAnswers = New-Object 'Collections.Generic.Queue[string]'
        foreach ($choice in $Choices) { $global:MilestoneUxFixtureAnswers.Enqueue($choice) }
        & (Join-Path $fixture 'WheelSetup.ps1') -GamePath (Split-Path $wc -Parent) -PreferencesPath $preference
        Assert ($global:MilestoneUxFixtureAnswers.Count -eq 0) 'Fixture interaction ended before all actions.'
    }
    Run-Setup @('2','v','2','r','','v','1','3','4','5','6','x')
    Assert ([IO.File]::ReadAllText($wc) -ceq $text) 'View changes or navigation changed mappings.'
    Assert ((Get-SettingsView $preference) -eq 'Simple') 'Round trip did not retain explicit Simple.'
    Run-Setup @('2','a4','b','','','','s')
    Assert ([IO.File]::ReadAllText($wc) -ceq $text) 'Cancelled handbrake capture changed mappings.'
    Run-Setup @('2','a4','b','','','s','s')
    Assert ((Get-WheelField ([IO.File]::ReadAllText($wc)) '0006346e' 'Wheel_Handbrake') -eq 'Axis3&-1.0&1.0') 'Accepted individual handbrake calibration was not saved.'
    Assert ((Get-WheelField ([IO.File]::ReadAllText($wc)) '0006346e' 'Wheel_RightTrigger') -eq 'Button9') 'Accepted handbrake axis removed its button.'

    $telemetryFolder = Join-Path $fixture 'Gravel\gravel\Binaries\Win64'
    [IO.Directory]::CreateDirectory($telemetryFolder) | Out-Null
    $telemetryFile = Join-Path $telemetryFolder 'milestone_mod.ini'
    $telemetryText = "[proxy]`r`nproduct=0006346e`r`n[telemetry]`r`nenabled=0`r`nhost=192.0.2.25`r`nport=9123`r`nformat=fm7`r`nrate=50`r`nowner_custom=keep this`r`n[ffb]`r`ngain=0.25`r`n"
    [IO.File]::WriteAllText($telemetryFile, $telemetryText)
    [IO.File]::WriteAllText($preference, '{broken again')
    Run-Setup @('v','2','x')
    Assert ((Get-SettingsView $preference) -eq 'Advanced') 'Simple fallback did not recover to explicit Advanced.'
    Assert ([IO.File]::ReadAllText($telemetryFile) -ceq $telemetryText) 'View preference repair changed telemetry.'
    Assert ((Get-WheelField ([IO.File]::ReadAllText($wc)) '0006346e' 'Wheel_Handbrake') -eq 'Axis3&-1.0&1.0') 'View preference repair changed wheel mappings.'
    Assert ((Get-TelemetryState $telemetryText).enabled -eq '0') 'Reading telemetry must preserve saved Off.'
    Assert ((Get-TelemetryConnectionSummary (Get-TelemetryState $telemetryText)) -like 'Custom connection*') 'A custom destination must not be described as defaults.'
    $preset = Set-TelemetryFields $telemetryText @{ host='127.0.0.1'; port='5300'; format='fh4' }
    Assert ((Get-TelemetryConnectionSummary (Get-TelemetryState $preset)) -like '*fresh-installer*') 'Default comparison must name its reference preset.'
    $gravelPreset = Set-TelemetryFields $telemetryText @{ host='127.0.0.1'; port='5300'; format='fm7' }
    Assert ((Get-TelemetryConnectionSummary (Get-TelemetryState $gravelPreset)) -like '*shipped Gravel*') 'Gravel preset must not be conflated with installer preset.'
    foreach ($badAddress in 'localhost','::1','1.2.3','999.0.0.1','127.00.0.1','0.0.0.0','255.255.255.255','224.0.0.1') {
        Assert-Throws { Assert-TelemetryConnection $badAddress '5300' 'fh4' } "Invalid destination accepted: $badAddress"
    }
    foreach ($badPort in '0','65536','-1','5.5','abc','9999999999999') {
        Assert-Throws { Assert-TelemetryConnection '127.0.0.1' $badPort 'fh4' } "Invalid port accepted: $badPort"
    }
    Assert-Throws { Assert-TelemetryConnection '127.0.0.1' '5300' 'unknown' } 'Unknown receiver format must not be silently mapped.'
    Assert-Throws { Set-TelemetryFields $telemetryText @{ port='8000' } } 'Connection fields must apply together.'
    Assert-Throws { Set-TelemetryFields $telemetryText @{ gain='1.0' } } 'Telemetry edit must not alter a tune.'
    Assert-Throws { Get-TelemetryState ($telemetryText + "[telemetry]`r`nenabled=1`r`n") } 'Ambiguous sections must not be edited.'
    Assert-Throws { Get-TelemetryState ($telemetryText.Replace('enabled=0',"enabled=0`r`nenabled=1")) } 'Ambiguous fields must not be edited.'
    $malformed = $telemetryText.Replace('192.0.2.25','localhost')
    Assert ((Get-TelemetryConnectionSummary (Get-TelemetryState $malformed)) -like '*needs review*') 'Malformed connection needs a visible recovery summary.'
    Assert-Throws { Set-TelemetryFields $malformed @{ enabled='1' } } 'Cannot enable an unusable receiver.'
    Assert ((Get-TelemetryState (Set-TelemetryFields $malformed @{ enabled='0' })).host -eq 'localhost') 'Saving Off must preserve an unknown custom destination.'
    $caseText = $telemetryText.Replace('[telemetry]','[Telemetry]').Replace('host=','Host =')
    $caseChanged = Set-TelemetryFields $caseText @{ host='127.0.0.1'; port='5300'; format='fh4' }
    Assert ($caseChanged.Contains('Host =127.0.0.1')) 'Editing must preserve existing key spelling and spacing.'
    Assert ($caseChanged.Contains('owner_custom=keep this')) 'Unknown custom fields must remain.'
    $lock = [IO.File]::Open($telemetryFile, 'Open', 'Read', 'None')
    try { Assert-Throws { Save-TelemetryConfig $telemetryFile $telemetryText $preset } 'Denied telemetry save must report failure.' }
    finally { $lock.Dispose() }
    Assert ([IO.File]::ReadAllText($telemetryFile) -ceq $telemetryText) 'Denied telemetry save changed saved settings.'
    $telemetryBackup = Save-TelemetryConfig $telemetryFile $telemetryText $preset
    Assert ([IO.File]::ReadAllText($telemetryBackup) -ceq $telemetryText) 'Telemetry save must back up the complete previous file.'
    Assert-Throws { Save-TelemetryConfig $telemetryFile $telemetryText $preset } 'Concurrent telemetry edits must not be overwritten.'
    [IO.File]::WriteAllText($telemetryFile, $telemetryText)
    Run-Setup @('5','a','h','192.0.2.100','p','9456','f','1','c','v','1','x')
    Assert ([IO.File]::ReadAllText($telemetryFile) -ceq $telemetryText) 'Cancelling a connection draft changed saved settings.'
    Run-Setup @('5','a','h','localhost','a','c','x')
    Assert ([IO.File]::ReadAllText($telemetryFile) -ceq $telemetryText) 'Invalid Apply changed saved settings.'
    $lock = [IO.File]::Open($telemetryFile, 'Open', 'Read', [IO.FileShare]::ReadWrite)
    try { Run-Setup @('5','a','p','8001','a','c','x') }
    finally { $lock.Dispose() }
    Assert ([IO.File]::ReadAllText($telemetryFile) -ceq $telemetryText) 'Failed atomic Apply changed the previous settings.'
    Assert (@(Get-ChildItem $telemetryFolder -Filter '.telemetry-*.tmp').Count -eq 0) 'Failed Apply left a draft file behind.'
    Run-Setup @('5','a','h','192.0.2.100','p','9456','f','1','a','v','1','x')
    $applied = [IO.File]::ReadAllText($telemetryFile)
    $appliedState = Get-TelemetryState $applied
    Assert ($appliedState.host -eq '192.0.2.100' -and $appliedState.port -eq '9456' -and $appliedState.format -eq 'fh4') 'Atomic connection Apply did not update all requested fields.'
    Assert ($appliedState.enabled -eq '0') 'Connection Apply must retain saved Off.'
    Assert ($applied.Contains('owner_custom=keep this') -and $applied.Contains('gain=0.25') -and $applied.Contains('rate=50')) 'Connection Apply changed unknown settings, tune or rate.'
    Run-Setup @('5','n','x')
    $enabledText = [IO.File]::ReadAllText($telemetryFile)
    Assert ((Get-TelemetryState $enabledText).enabled -eq '1') 'Simple On did not persist for the next launch.'
    Assert ($enabledText.Replace('enabled=1','enabled=0') -ceq $applied) 'Simple On changed connection or custom fields.'
    Run-Setup @('5','o','x')
    Assert ([IO.File]::ReadAllText($telemetryFile) -ceq $applied) 'Simple Off did not restore only its saved preference.'

    $mappingBeforePreview = [IO.File]::ReadAllBytes($wc)
    $telemetryBeforePreview = [IO.File]::ReadAllBytes($telemetryFile)
    $preferenceBeforePreview = [IO.File]::ReadAllBytes($preference)
    $sampleCount = [int]([IO.File]::ReadAllText($env:MILESTONE_UX_FIXTURE))
    $previewOutput = Run-Setup @('2','p','x') 6>&1 | Out-String
    Assert ($previewOutput.Contains('Game-final input, endpoints and deadzones are not verified here.')) 'Preview must distinguish device state from game-final input.'
    Assert ($previewOutput.Contains('Preview finished. No settings were saved.')) 'Normal preview completion must return to setup.'
    Assert ([int]([IO.File]::ReadAllText($env:MILESTONE_UX_FIXTURE)) -eq ($sampleCount + 1)) 'One preview must use exactly one input-reader process.'
    # Stage the opposite handbrake polarity, inspect it, then exit without saving.
    [IO.File]::WriteAllText($env:MILESTONE_UX_FIXTURE, '0')
    $previewOutput = Run-Setup @('2','a4','b','','','i','s','p','x') 6>&1 | Out-String
    Assert ($previewOutput.Contains('Handbrake (axis): Full (100%)')) 'Preview must use staged calibration rather than old saved mapping.'
    $env:MILESTONE_UX_PREVIEW = 'disconnect'
    $previewOutput = Run-Setup @('2','p','x') 6>&1 | Out-String
    Assert ($previewOutput.Contains('Preview stopped. No settings were saved.')) 'Disconnect must return to the settings menu.'
    $disconnectPid = [int]([IO.File]::ReadAllText($env:MILESTONE_UX_PROBE_PID))
    Assert-Throws { [Diagnostics.Process]::GetProcessById($disconnectPid) } 'Disconnected input reader is still running.'
    $env:MILESTONE_UX_PREVIEW = 'hang'
    Remove-Item -LiteralPath $env:MILESTONE_UX_PROBE_PID -ErrorAction SilentlyContinue
    $cancelClock = [Diagnostics.Stopwatch]::StartNew()
    Assert-Throws { Read-WheelInput -Probe (Join-Path $probeFolder 'wheelprobe.exe') -Product '0006346e' -Seconds 10 -CancellationRequested { Test-Path -LiteralPath $env:MILESTONE_UX_PROBE_PID } } 'Cancellation must stop a running input reader.'
    Assert ($cancelClock.Elapsed.TotalSeconds -lt 4) 'Cancellation did not return promptly.'
    $cancelPid = [int]([IO.File]::ReadAllText($env:MILESTONE_UX_PROBE_PID))
    Assert-Throws { [Diagnostics.Process]::GetProcessById($cancelPid) } 'Cancelled input reader is still running.'
    $timeoutClock = [Diagnostics.Stopwatch]::StartNew()
    Assert-Throws { Read-WheelInput -Probe (Join-Path $probeFolder 'wheelprobe.exe') -Product '0006346e' -Seconds 1 -CancellationRequested { $false } } 'A stalled input reader must time out.'
    Assert ($timeoutClock.Elapsed.TotalSeconds -lt 4) 'Input reader timeout did not return promptly.'
    $timeoutPid = [int]([IO.File]::ReadAllText($env:MILESTONE_UX_PROBE_PID))
    Assert-Throws { [Diagnostics.Process]::GetProcessById($timeoutPid) } 'Timed-out input reader is still running.'
    $staleClock = [Diagnostics.Stopwatch]::StartNew()
    Assert-Throws { Read-WheelInput -Probe (Join-Path $probeFolder 'wheelprobe.exe') -Product '0006346e' -Seconds 10 -CancellationRequested { $false } } 'Lost sample heartbeat must not leave a frozen preview.'
    Assert ($staleClock.Elapsed.TotalSeconds -lt 5) 'Stale input did not return promptly.'
    $stalePid = [int]([IO.File]::ReadAllText($env:MILESTONE_UX_PROBE_PID))
    Assert-Throws { [Diagnostics.Process]::GetProcessById($stalePid) } 'Stale input reader is still running.'
    $env:MILESTONE_UX_PREVIEW = ''
    Assert ([Convert]::ToBase64String([IO.File]::ReadAllBytes($wc)) -eq [Convert]::ToBase64String($mappingBeforePreview)) 'Preview changed saved mappings.'
    Assert ([Convert]::ToBase64String([IO.File]::ReadAllBytes($telemetryFile)) -eq [Convert]::ToBase64String($telemetryBeforePreview)) 'Preview changed saved telemetry.'
    Assert ([Convert]::ToBase64String([IO.File]::ReadAllBytes($preference)) -eq [Convert]::ToBase64String($preferenceBeforePreview)) 'Preview changed presentation preference.'

    $game = Join-Path $fixture 'game-binaries'
    [IO.Directory]::CreateDirectory($game) | Out-Null
    [IO.File]::WriteAllText((Join-Path $game 'gravel-Win64-Shipping.exe'), 'inert fixture, never run')
    $config = Join-Path $game 'milestone_mod.ini'
    $custom = [IO.File]::ReadAllText((Join-Path $repo 'games\gravel\milestone_mod.ini'))
    $custom = Set-ModSetting $custom 'telemetry' 'enabled' '0'
    $custom = Set-ModSetting $custom 'telemetry' 'port' '9123'
    $custom = Set-ModSetting $custom 'telemetry' 'host' '192.0.2.25'
    $custom = Set-ModSetting $custom 'ffb' 'gain' '0.25'
    [IO.File]::WriteAllText($config, $custom)
    & (Join-Path $fixture 'Install.ps1') -GamePath $game -SkipWheelConfig
    Assert ([IO.File]::ReadAllText($config) -ceq $custom) 'Ordinary update replaced owner settings.'
    & (Join-Path $fixture 'Install.ps1') -GamePath $game -SkipWheelConfig -Port 9456
    $updated = [IO.File]::ReadAllText($config)
    Assert ((Get-ModSetting $updated 'telemetry' 'port') -eq '9456') 'Explicit connection override was ignored.'
    Assert ((Get-ModSetting $updated 'telemetry' 'enabled') -eq '0') 'Explicit port override enabled telemetry.'
    Assert ((Get-ModSetting $updated 'telemetry' 'host') -eq '192.0.2.25') 'Explicit port override changed host.'
    Assert ((Get-ModSetting $updated 'ffb' 'gain') -eq '0.25') 'Explicit port override changed tune.'
    Assert (@(Get-ChildItem (Join-Path $game 'DBCE-Wheel-Backups') -Directory).Count -eq 2) 'Each install must retain its transaction backup.'
    & (Join-Path $fixture 'Uninstall.ps1') -GamePath $game
    Assert (-not (Test-Path (Join-Path $game 'dinput8.dll'))) 'Uninstall must remove its own proxy.'
    Assert ([IO.File]::ReadAllText($config) -ceq $updated) 'Ordinary uninstall must retain personal settings.'
    Assert (@(Get-ChildItem (Join-Path $game 'DBCE-Wheel-Backups') -Directory).Count -eq 3) 'Ordinary uninstall must retain install backups and add a removal backup.'
    [IO.File]::WriteAllText((Join-Path $game 'dinput8.dll'), 'another proxy')
    Assert-Throws { & (Join-Path $fixture 'Uninstall.ps1') -GamePath $game } 'Unknown receiptless removal must fail closed.'
    Assert ([IO.File]::ReadAllText((Join-Path $game 'dinput8.dll')) -eq 'another proxy') 'Uninstall must preserve another proxy.'
    $freshGame = Join-Path $fixture 'fresh-game'
    [IO.Directory]::CreateDirectory($freshGame) | Out-Null
    [IO.File]::WriteAllText((Join-Path $freshGame 'gravel-Win64-Shipping.exe'), 'inert fixture, never run')
    & (Join-Path $fixture 'Install.ps1') -GamePath $freshGame -SkipWheelConfig -Product '11112222'
    $fresh = [IO.File]::ReadAllText((Join-Path $freshGame 'milestone_mod.ini'))
    Assert ((Get-ModSetting $fresh 'proxy' 'product') -eq '11112222') 'Fresh install must use the chosen wheel.'
    Assert ((Get-ModSetting $fresh 'telemetry' 'port') -eq '5300') 'Fresh install receiver port changed.'
    Assert ((Get-ModSetting $fresh 'telemetry' 'format') -eq 'fh4') 'Fresh install receiver format changed.'
    Write-Host "PASS: $checks offline UX/migration checks. No game or hardware was opened."
} finally {
    $env:LOCALAPPDATA = $oldLocal
    $env:MILESTONE_UX_FIXTURE = $oldFixture
    $env:MILESTONE_UX_PREVIEW = $oldPreviewMode
    $env:MILESTONE_UX_PROBE_PID = $oldProbePid
    Remove-Variable -Name MilestoneUxFixtureAnswers -Scope Global -ErrorAction SilentlyContinue
    # Keep fixture and transcript paths for review; no recursive cleanup targets.
    Write-Host "Fixture: $fixture"
}
