param([switch]$NativeBindingsOnly, [switch]$HelpOnly, [switch]$CameraStepsOnly, [switch]$AxisInvertOnly)
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
$oldBrowserOut = $env:MILESTONE_UX_BROWSER_OUT
try {
    if ($AxisInvertOnly) {
        foreach ($axis in 1..8) {
            $normal = "Axis$axis&1.0&0.0"; $inverted = "Axis$axis&-1.0&1.0"
            Assert ((Get-InvertedAxisMapping $normal) -ceq $inverted) 'Normal axis did not invert.'
            Assert ((Get-InvertedAxisMapping $inverted) -ceq $normal) 'Inverted axis did not restore.'
            Assert ((Get-DeviceInputPreview $inverted @(0,0,0,0,0,0,0,0)).Value -eq 1) 'Inversion did not reverse released/full preview.'
        }
        Assert ((Get-InvertedAxisMapping 'Axis3&1&0') -ceq 'Axis3&-1.0&1.0') 'Existing integer-form transform was refused.'
        foreach ($bad in '', 'Button1', 'Axis0&1.0&0.0', 'Axis9&1.0&0.0', 'Axis3&0.5&0.2', 'Axis1', "Axis1&1.0&0.0`nunsafe=1", "Axis1&1.0&0.0`n") {
            Assert-Throws { Get-InvertedAxisMapping $bad } 'Unknown assignment was inverted.'
        }

        $tokens=$null; $parseErrors=$null
        $ast=[Management.Automation.Language.Parser]::ParseFile((Join-Path $repo 'WheelSetup.ps1'),[ref]$tokens,[ref]$parseErrors)
        Assert (!$parseErrors.Count) 'Setup script failed to parse.'
        foreach ($name in 'Edit-Axis','Get-Field','Set-Field','Assignment') {
            $definition=@($ast.FindAll({param($node) $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq $name},$true))
            Assert ($definition.Count -eq 1) 'Production binding editor function was not unique.'
            Invoke-Expression $definition[0].Extent.Text
        }
        function Say($m) {} function Head($m) {} function Ok($m) {} function Warn($m) { $script:invertWarnings += [string]$m }
        function Sample { throw 'Invert must never acquire or sample a device.' }
        function Read-Host { param($Prompt) if (!$script:invertAnswers.Count) { throw 'Invert fixture answers exhausted.' }; $script:invertAnswers.Dequeue() }
        $dev=[pscustomobject]@{Key='fixture'}; $View='Simple'
        $original="[/Wheel.Config/fixture]`r`nWheel_Steer=Axis1&1.0&0.0`r`nWheel_Generic_Axis2=Axis6&-1.0&1.0`r`nForceFeedback=0.0`r`nOwnerCustom=keep`r`n`r`n[/Wheel.Config/other]`r`nWheel_Steer=Axis2&1.0&0.0`r`n"
        $AXES=Get-WheelAxisFields @{Throttle='Wheel_Generic_Axis2'; Brake=@('Wheel_Brake','Wheel_Generic_Axis1')}
        $wc=Join-Path $fixture 'WheelConfig.ini'
        foreach ($case in @(
            @{Name='Steering';Answers=@('i','s');Field='Wheel_Steer';Value='Axis1&-1.0&1.0'},
            @{Name='Steering';Answers=@('i','');Field='Wheel_Steer';Value='Axis1&1.0&0.0'},
            @{Name='Throttle';Answers=@('i','s');Field='Wheel_Generic_Axis2';Value='Axis6&1.0&0.0'},
            @{Name='Brake';Answers=@();Field='Wheel_Steer';Value='Axis1&1.0&0.0'},
            @{Name='Clutch';Answers=@();Field='Wheel_Steer';Value='Axis1&1.0&0.0'},
            @{Name='Steering';Answers=@('i');Custom=$true;Field='Wheel_Steer';Value='Axis1&0.5&0.2'},
            @{Name='Steering';Answers=@();Duplicate=$true;Field='Wheel_Steer';Value='Axis1&1.0&0.0'}
        )) {
            $before=$original
            if($case.Custom){$before=$before.Replace('Axis1&1.0&0.0','Axis1&0.5&0.2')}
            if($case.Duplicate){$before=$before.Replace('Wheel_Steer=Axis1&1.0&0.0',"Wheel_Steer=Axis1&1.0&0.0`r`nWheel_Steer=Axis8&1.0&0.0")}
            [IO.File]::WriteAllText($wc,$before); $script:raw=$before; $script:invertWarnings=@()
            $script:invertAnswers=New-Object 'Collections.Generic.Queue[string]'
            foreach($answer in $case.Answers){$script:invertAnswers.Enqueue($answer)}
            Edit-Axis $case.Name
            Assert ($script:invertAnswers.Count -eq 0) 'Editor did not consume the expected interaction.'
            Assert ((Get-WheelField $script:raw 'fixture' $case.Field) -ceq $case.Value) 'Editor changed the wrong slot or failed to preserve a refused assignment.'
            Assert ([IO.File]::ReadAllText($wc) -ceq $before) 'An inversion wrote to disk before final Save.'
            $expected=if($case.Value -eq 'Axis1&-1.0&1.0'){$before.Replace('Wheel_Steer=Axis1&1.0&0.0', 'Wheel_Steer=Axis1&-1.0&1.0')}
                elseif($case.Value -eq 'Axis6&1.0&0.0'){$before.Replace('Wheel_Generic_Axis2=Axis6&-1.0&1.0', 'Wheel_Generic_Axis2=Axis6&1.0&0.0')}else{$before}
            Assert ($script:raw -ceq $expected) 'Invert changed unrelated bindings, force, owner fields or CRLF.'
            if($case.Custom -or $case.Duplicate -or !$case.Answers.Count){Assert ($script:invertWarnings.Count -eq 1) 'Refusal did not explain why the assignment was kept.'}
            if($script:raw -cne $before){
                $backup=Save-WheelConfig $wc $before $script:raw
                Assert ([IO.File]::ReadAllText($backup) -ceq $before) 'Final Save lost the original backup.'
                Assert ([IO.File]::ReadAllText($wc) -ceq $expected) 'Final Save did not persist the staged inversion.'
                Assert-Throws { Save-WheelConfig $wc $before $script:raw } 'Stale mapping save was accepted.'
            }
        }
        Write-Host "PASS: $checks axis inversion checks. Synthetic mappings and production editor only; no device/game."
        return
    }
    if ($CameraStepsOnly) {
        # Only INI helpers and the actual draft editor, with fake console/process boundaries.
        # No input-reader process, device, game, browser or visible console host is opened.
        $original = [IO.File]::ReadAllText((Join-Path $repo 'games/gravel/milestone_mod.ini'))
        $change = @{move_step_m='0.05';tilt_step_deg='3';fov_step_deg='4'}
        $updated = Set-CameraSteps $original $change
        $state = Get-CameraSteps $updated
        Assert ($state.move_step_m -eq '0.05' -and $state.tilt_step_deg -eq '3' -and $state.fov_step_deg -eq '4') 'All camera steps must apply together.'
        $restored = Set-CameraSteps $updated @{move_step_m='0.02';tilt_step_deg='1';fov_step_deg='2'}
        Assert ($restored -ceq $original) 'Unrelated geometry, offsets, telemetry, comments and line endings changed.'
        Assert ((Set-CameraSteps $original @{move_step_m='0.020';tilt_step_deg='1.0';fov_step_deg='2.00'}) -ceq $original) 'Numerically unchanged steps must keep exact text.'
        foreach ($bad in '0','-1','NaN','Infinity','1.1','0,05',"0.05`nunsafe=1") {
            Assert-Throws { Set-CameraSteps $original @{move_step_m=$bad;tilt_step_deg='1';fov_step_deg='2'} } 'Invalid move step accepted.'
        }
        Assert-Throws { Set-CameraSteps $original @{move_step_m='0.02';tilt_step_deg='46';fov_step_deg='2'} } 'Oversized tilt step accepted.'
        Assert-Throws { Set-CameraSteps $original @{move_step_m='0.02';tilt_step_deg='1';fov_step_deg='0'} } 'Zero FOV step accepted.'
        Assert-Throws { Set-CameraSteps $original @{move_step_m='0.02'} } 'Partial camera draft accepted.'
        Assert-Throws { Set-CameraSteps $original @{move_step_m='0.02';tilt_step_deg='1';gain='2'} } 'Unrelated setting accepted.'
        Assert-Throws { Get-CameraSteps ($original + "`r`n[camera]`r`nmove_step_m=0.1`r`n") } 'Duplicate camera sections accepted.'
        Assert-Throws { Get-CameraSteps ($original.Replace('move_step_m=0.02',"move_step_m=0.02`r`nMOVE_STEP_M=0.1")) } 'Duplicate camera keys accepted.'
        Assert-Throws { Get-CameraSteps '[proxy]' } 'Missing camera section accepted.'
        $telemetryPath = Join-Path $fixture 'milestone_mod.ini'
        $innerGameFolder = Join-Path $fixture 'game'
        [IO.File]::WriteAllText($telemetryPath, $original)
        $backup = Save-CameraSteps $telemetryPath $original $change
        Assert ([IO.File]::ReadAllText($backup) -ceq $original) 'Camera backup did not preserve original text.'
        Assert ([IO.File]::ReadAllText($telemetryPath) -ceq $updated) 'Camera transaction did not write the draft.'
        Assert-Throws { Save-CameraSteps $telemetryPath $original $change } 'Stale camera draft overwrote later settings.'
        Assert ($null -eq (Save-CameraSteps $telemetryPath $updated $change)) 'No-op camera Apply created a backup.'
        # The existing telemetry entry uses the same atomic writer after extraction.
        $off = Set-TelemetryFields $updated @{enabled='0'}
        $telemetryBackup = Save-TelemetryConfig $telemetryPath $updated $off
        Assert ([IO.File]::ReadAllText($telemetryBackup) -ceq $updated) 'Shared telemetry save lost its original backup.'
        Assert ((Get-CameraSteps ([IO.File]::ReadAllText($telemetryPath))).move_step_m -eq '0.05') 'Telemetry save altered camera steps.'
        foreach ($encoding in @((New-Object Text.UTF8Encoding($true)),(New-Object Text.UnicodeEncoding($false,$true)),(New-Object Text.UnicodeEncoding($true,$true)))) {
            [IO.File]::WriteAllText($telemetryPath,$original,$encoding)
            $before=[IO.File]::ReadAllBytes($telemetryPath)
            $encodingBackup=Save-CameraSteps $telemetryPath $original $change
            Assert ([Convert]::ToBase64String([IO.File]::ReadAllBytes($encodingBackup)) -ceq [Convert]::ToBase64String($before)) 'Encoded camera backup was not byte exact.'
            Assert ([Convert]::ToBase64String([IO.File]::ReadAllBytes($telemetryPath)) -ceq [Convert]::ToBase64String($encoding.GetPreamble()+$encoding.GetBytes($updated))) 'Camera save changed the installed Unicode encoding/BOM.'
        }

        $tokens=$null; $parseErrors=$null
        $ast=[Management.Automation.Language.Parser]::ParseFile((Join-Path $repo 'WheelSetup.ps1'),[ref]$tokens,[ref]$parseErrors)
        Assert (!$parseErrors.Count) 'Setup script failed to parse.'
        foreach ($name in 'Edit-CameraSteps','Assert-ModGameClosed') {
            $definition=@($ast.FindAll({param($node) $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq $name},$true))
            Assert ($definition.Count -eq 1) 'Production camera editor function was not unique.'
            Invoke-Expression $definition[0].Extent.Text
        }
        function Say($m) {} function Head($m) {} function Ok($m) {} function Warn($m) { $script:cameraWarnings += [string]$m }
        function Get-Process { param($EA) if ($script:cameraGameRunning) { [pscustomobject]@{Path=(Join-Path $innerGameFolder 'gravel.exe')} } }
        function Read-Host { param($Prompt) if (!$script:cameraAnswers.Count) { throw 'Camera fixture answers exhausted.' }; $script:cameraAnswers.Dequeue() }
        foreach ($case in @(
            @{Answers=@('1','0.08','c');Running=$false;Applied=$false},
            @{Answers=@('1','0.08','a');Running=$false;Applied=$true},
            @{Answers=@('d','a');Running=$true;Applied=$false},
            @{Answers=@('1','-1','a','c');Running=$false;Applied=$false}
        )) {
            [IO.File]::WriteAllText($telemetryPath,$original)
            $script:cameraAnswers=New-Object 'Collections.Generic.Queue[string]'
            foreach($answer in $case.Answers){$script:cameraAnswers.Enqueue($answer)}
            if($case.Running){$script:cameraAnswers.Enqueue('c')}
            $script:cameraGameRunning=$case.Running; $script:cameraWarnings=@()
            Edit-CameraSteps
            $actual=[IO.File]::ReadAllText($telemetryPath)
            Assert (($actual -cne $original) -eq $case.Applied) 'Production Apply/Cancel/closed-game admission differed.'
            if($case.Applied){Assert ((Get-CameraSteps $actual).move_step_m -eq '0.08') 'Production editor did not save its actual draft.'}
            if($case.Running){Assert (($script:cameraWarnings -join ' ') -match 'Close the game') 'Running-game refusal was not shown.'}
        }
        Assert (@(Get-ChildItem -LiteralPath $fixture -Filter '*.tmp' -File).Count -eq 0) 'Temporary INI files remain.'
        Write-Host "PASS: $checks camera step/settings checks. Synthetic INI and production editor with fake console/process boundaries only."
        return
    }
    if ($NativeBindingsOnly) {
        # Existing setup helpers, synthetic GVAS bytes and INI only. No reader,
        # device, game, installer or production save is invoked in this mode.
        function BindingSave($Records) {
            $stream = New-Object IO.MemoryStream
            $writer = New-Object IO.BinaryWriter($stream)
            function WriteString([string]$Value) {
                $bytes = [Text.Encoding]::ASCII.GetBytes($Value)
                $writer.Write([int]($bytes.Length + 1)); $writer.Write($bytes); $writer.Write([byte]0)
            }
            try {
                $writer.Write([Text.Encoding]::ASCII.GetBytes('GVAS')); $writer.Write((New-Object byte[] 16))
                WriteString 'ignitioninput'; $writer.Write((New-Object byte[] 17))
                foreach ($record in $Records) {
                    foreach ($pair in @(@('ActionName', $record[0]), @('KeyName', $record[1]))) {
                        WriteString $pair[0]; WriteString 'NameProperty'
                        $writer.Write([long]($pair[1].Length + 5)); $writer.Write([byte]0); WriteString $pair[1]
                    }
                    WriteString 'None'
                }
                $writer.Flush(); return ,$stream.ToArray()
            } finally { $writer.Dispose(); $stream.Dispose() }
        }
        $records = @(@('GearReverse','Wheel_GearReverse'), @('Gear1','Wheel_Gear1'), @('Gear2','Wheel_Gear2'),
            @('Gear3','Wheel_Gear3'), @('Gear4','Wheel_Gear4'), @('Gear5','Wheel_Gear5'), @('Gear6','Wheel_Gear6'),
            @('Gear7','Wheel_Gear7'), @('LookBackCamera','Wheel_FaceButton_Bottom'), @('Gear1','KeyboardOne'),
            @('Gear1','Wheel_Gear1'))
        $save = BindingSave $records
        $saveBefore = [Convert]::ToBase64String($save)
        $saved = Read-SavedWheelActions $save
        Assert ($saved.Count -eq 9) 'The saved native actions were not read.'
        Assert (@($saved.Gear1).Count -eq 1) 'Duplicate identical wheel slots must not create false ambiguity.'
        Assert ([Convert]::ToBase64String($save) -ceq $saveBefore) 'Reading bindings changed save bytes.'
        $actions = Get-WheelButtonActions $saved
        Assert ($actions.Contains('Reverse') -and $actions.Contains('Gear 7') -and $actions.Contains('Look back')) 'Saved native actions are missing from standalone setup.'
        Assert (!$actions.Contains('Gear 8')) 'A gear absent from the save must not be invented.'
        Assert (@((Get-WheelButtonActions @{}).Keys).Count -eq 10) 'Missing saves must not invent H-pattern or look-back defaults.'
        $text = "[/Wheel.Config/fixture]`r`nWheel_Clutch=Axis7&1.0&0.0`r`nOwnerCustom=keep`r`n"
        foreach ($slot in @('Wheel_GearReverse','Wheel_Gear1','Wheel_Gear2','Wheel_Gear3','Wheel_Gear4','Wheel_Gear5','Wheel_Gear6','Wheel_Gear7','Wheel_FaceButton_Bottom')) { $text += "$slot=`r`n" }
        $original = $text
        $button = 1
        foreach ($label in @('Reverse','Gear 1','Gear 2','Gear 3','Gear 4','Gear 5','Gear 6','Gear 7','Look back')) {
            $slot = Resolve-WheelActionSlot $actions[$label] $saved -GravelDefaults
            Assert ($slot -and $slot -like 'Wheel_*') "No native saved slot for $label."
            $text = Set-WheelField $text 'fixture' $slot "Button$button"
            Assert ((Get-WheelField $text 'fixture' $slot) -ceq "Button$button") "$label did not use the saved destination."
            $button++
        }
        Assert ((Get-WheelField $text 'fixture' 'Wheel_Clutch') -ceq 'Axis7&1.0&0.0') 'New button bindings changed native clutch mapping.'
        Assert ($text.Contains('OwnerCustom=keep') -and $text -notmatch '(?<!\r)\n') 'Native binding writes changed owner fields or CRLF.'
        $ambiguous = Read-SavedWheelActions (BindingSave @(@('Gear1','Wheel_Gear1'), @('Gear1','Wheel_Gear2')))
        Assert (!(Resolve-WheelActionSlot $actions['Gear 1'] $ambiguous -GravelDefaults)) 'Ambiguous gear slots must not select the first or guess a fallback.'
        Assert (!(Resolve-WheelActionSlot @('GearUp','Wheel_RightShoulder') @{GearUp=@('Wheel_GearUp','Wheel_RightShoulder')} -GravelDefaults)) 'Ambiguous existing actions must not fall back to a default.'
        Assert ((Read-SavedWheelActions ([byte[]]@(1,2,3))).Count -eq 0) 'A non-GVAS save must not create native mappings.'
        Assert ((Resolve-WheelActionSlot @('GearUp','Wheel_RightShoulder') @{} -GravelDefaults) -ceq 'Wheel_RightShoulder') 'Existing verified Gravel defaults changed.'
        Assert (!(Resolve-WheelActionSlot @('GearUp','Wheel_RightShoulder') @{})) 'Other games must not inherit Gravel defaults.'
        Assert-Throws { Set-WheelField $original 'fixture' 'Wheel_Gear8' 'Button8' } 'An absent wheel field must never be created by native gear binding.'
        $devices = @([pscustomobject]@{Key='0006346e';Name='Fixture wheel'}, [pscustomobject]@{Key='b660044f';Name='Fixture shifter'})
        $paired = Get-ButtonCaptureSource 'b660044f_Button8' '0006346e' $devices
        Assert ($paired.Product -ceq 'b660044f' -and $paired.Name -ceq 'Fixture shifter') 'An existing paired gear must sample its named device, not the wheel.'
        $updated = Set-WheelField $original 'fixture' 'Wheel_GearReverse' ($paired.Prefix + 'Button7')
        Assert ((Get-WheelField $updated 'fixture' 'Wheel_GearReverse') -ceq 'b660044f_Button7') 'Rebinding an existing paired shifter lost its native product prefix.'
        $primary = Get-ButtonCaptureSource 'Button8' '0006346e' $devices
        Assert ($primary.Product -ceq '0006346e' -and $primary.Prefix -ceq '') 'An ordinary wheel button must not gain a pairing.'
        Assert ((Format-WheelAssignment 'b660044f_Button8') -ceq 'Button 8 (device b660044f)') 'The paired device must be visible in the assignment.'
        Assert-Throws { Get-ButtonCaptureSource 'b660044f_Button8' '0006346e' @($devices[0]) } 'A missing paired device must not silently redirect capture to the wheel.'
        Assert-Throws { Get-ButtonCaptureSource 'b660044f_Button8' '0006346e' @($devices[0],$devices[1],$devices[1]) } 'Duplicate paired products must not choose a device arbitrarily.'
        Assert-Throws { Get-ButtonCaptureSource 'b660044f_Button129' '0006346e' $devices } 'An invalid native button range must not start capture.'
        $axisSlots = Read-SavedWheelActions (BindingSave @(@('Clutch','Wheel_Generic_Axis1'), @('Brake','Wheel_Brake'), @('Handbrake','Wheel_RightTrigger')))
        $axes = Get-WheelAxisFields $axisSlots
        Assert ($axes.Clutch -ceq 'Wheel_Generic_Axis1') 'Clutch must use its saved action slot instead of an unused default field.'
        Assert ($axes.'Handbrake (axis)' -ceq 'Wheel_Handbrake') 'The saved handbrake button must not replace the independent axis destination.'
        $clutchText = $original + "Wheel_Generic_Axis1=`r`n"
        $clutchUpdated = Set-WheelField $clutchText 'fixture' $axes.Clutch 'Axis8&-1.0&1.0'
        Assert ((Get-WheelField $clutchUpdated 'fixture' 'Wheel_Generic_Axis1') -ceq 'Axis8&-1.0&1.0') 'Native clutch destination did not receive the staged axis.'
        Assert ((Get-WheelField $clutchUpdated 'fixture' 'Wheel_Clutch') -ceq 'Axis7&1.0&0.0') 'An unused retained clutch field was changed.'
        $ambiguousAxes = Get-WheelAxisFields @{Clutch=@('Wheel_Clutch','Wheel_Generic_Axis1')}
        Assert (!$ambiguousAxes.Clutch) 'Ambiguous saved clutch slots must not fall back to a default.'
        Assert ((Get-WheelAxisFields @{}).Clutch -ceq 'Wheel_Clutch') 'The verified native clutch fallback changed.'
        Write-Host "PASS: $checks native binding checks. Synthetic save/INI only; no hardware or game."
        return
    }
    $preference = Join-Path $fixture 'settings-view.json'
    $text = "[/Wheel.Config/0006346e]`r`nProductName=Fixture wheel`r`nWheel_Steer=Axis1&1.0&0.0`r`nWheel_Accelerator=Axis3&1.0&0.0`r`nWheel_Brake=Axis6&1.0&0.0`r`nWheel_Handbrake=Axis7&1.0&0.0`r`nWheel_Clutch=`r`nWheel_RightTrigger=Button9`r`nWheel_Special_Left=Button8`r`nForceFeedback=0.0`r`nMaxRotationAngle=540.0`r`nCustomOwnerValue=42`r`n`r`n[/Wheel.Config/second]`r`nWheel_Handbrake=Axis2&1.0&0.0`r`n"
    $wc = Join-Path $fixture 'Gravel\gravel\Config\WindowsNoEditor\WheelConfig.ini'
    [IO.Directory]::CreateDirectory((Split-Path $wc -Parent)) | Out-Null
    [IO.File]::WriteAllText($wc, $text)
    if (!$HelpOnly) {
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
    Assert ($neutralPreview.Label -eq 'Center') 'Steering neutral should be readable as Center.'
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

    }

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
    foreach ($sub in 'tools','games\gravel') { [IO.Directory]::CreateDirectory((Join-Path $fixture $sub)) | Out-Null }
    Copy-Item (Join-Path $repo 'WheelSetup.ps1') $fixture
    Copy-Item (Join-Path $repo 'Install.ps1') $fixture
    Copy-Item (Join-Path $repo 'Uninstall.ps1') $fixture
    Copy-Item (Join-Path $repo 'tools\SetupUx.psm1') (Join-Path $fixture 'tools')
    Import-Module (Join-Path $repo 'tools\InstallPackage.psm1') -Force
    foreach ($relative in Get-PackageFiles) {
        if ($relative -in @('dist\wheelprobe.exe','dist\dinput8.dll','lib\vendor-toolkit\DbceWheel.psm1')) { continue }
        $target = Join-Path $fixture $relative
        [IO.Directory]::CreateDirectory((Split-Path $target -Parent)) | Out-Null
        Copy-Item -LiteralPath (Join-Path $repo $relative) -Destination $target -ErrorAction SilentlyContinue
    }
    Copy-Item (Join-Path $repo 'games\gravel\milestone_mod.ini') (Join-Path $fixture 'games\gravel')
    # Stub DbceWheel.psm1 at the expected path so WheelSetup.ps1 can import it in the fixture.
    $stubPath = Join-Path $fixture 'lib\vendor-toolkit'
    [IO.Directory]::CreateDirectory($stubPath) | Out-Null
    [IO.File]::WriteAllText((Join-Path $stubPath 'DbceWheel.psm1'), 'function Get-SteamLibraries { @() }; Export-ModuleMember -Function Get-SteamLibraries')
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
    if (!$HelpOnly) {
    Run-Setup @('2','v','2','r','','v','1','3','4','5','6','x')
    Assert ([IO.File]::ReadAllText($wc) -ceq $text) 'View changes or navigation changed mappings.'
    Assert ((Get-SettingsView $preference) -eq 'Simple') 'Round trip did not retain explicit Simple.'
    Run-Setup @('2','a4','b','','','','s')
    Assert ([IO.File]::ReadAllText($wc) -ceq $text) 'Cancelled handbrake capture changed mappings.'
    Run-Setup @('2','a4','b','','','s','s')
    Assert ((Get-WheelField ([IO.File]::ReadAllText($wc)) '0006346e' 'Wheel_Handbrake') -eq 'Axis3&-1.0&1.0') 'Accepted individual handbrake calibration was not saved.'
    Assert ((Get-WheelField ([IO.File]::ReadAllText($wc)) '0006346e' 'Wheel_RightTrigger') -eq 'Button9') 'Accepted handbrake axis removed its button.'

    }

    $telemetryFolder = Join-Path $fixture 'Gravel\gravel\Binaries\Win64'
    [IO.Directory]::CreateDirectory($telemetryFolder) | Out-Null
    $telemetryFile = Join-Path $telemetryFolder 'milestone_mod.ini'
    $telemetryText = "[proxy]`r`nproduct=0006346e`r`n[telemetry]`r`nenabled=0`r`nhost=192.0.2.25`r`nport=9123`r`nformat=fm7`r`nrate=50`r`nowner_custom=keep this`r`n[ffb]`r`ngain=0.25`r`n"
    [IO.File]::WriteAllText($telemetryFile, $telemetryText)
    if (!$HelpOnly) {
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

    } else {
        $mappingBeforePreview = [IO.File]::ReadAllBytes($wc)
        $telemetryBeforePreview = [IO.File]::ReadAllBytes($telemetryFile)
    }

    $links = Get-ModHelpLinks
    Assert ($links.ModUrl -ceq 'https://github.com/d-b-c-e/dbce-mods-gravel' -and $links.WheelkitUrl -ceq 'https://github.com/d-b-c-e/dbce-wheelkit') 'Help links must name the mod and Wheelkit GitHub repositories.'
    # Labels follow the canonical core's documented credential set; Core's C# fixture owns redaction coverage.
    $redacted = Format-SupportRedactedText "Path C:\Users\Alice\AppData\Local\Gravel mail alice@example.com password=secret123 ghp_1234567890abcdefghij1234567890"
    Assert ($redacted -notmatch 'Alice|alice@example\.com|secret123|ghp_' -and $redacted.Contains('%USERPROFILE%') -and $redacted.Contains('[redacted email]') -and $redacted.Contains('[redacted]')) 'Redaction must strip user paths, emails and credentials.'
    Assert-Throws { Read-SupportLogTail '\\server\share\milestone_mod.log' } 'UNC log paths must be refused.'
    $binaryLog = Join-Path $fixture 'binary.log'
    [IO.File]::WriteAllBytes($binaryLog, [byte[]]@(0x4D, 0x5A, 0x00, 0x01))
    Assert-Throws { Read-SupportLogTail $binaryLog } 'Binary files must not be included as logs.'
    $invalidLog = Join-Path $fixture 'invalid-utf8.log'
    [IO.File]::WriteAllBytes($invalidLog, [byte[]]@(0x61, 0xC3, 0x28))
    Assert-Throws { New-PreparedSupportReport -ProductName 'Gravel' -ProductVersion '1' -Repository 'd-b-c-e/dbce-mods-gravel' -Title 'Invalid log' -Description 'Steps' -LogPaths @($invalidLog) } 'An invalid selected log must fail preparation rather than silently omit evidence.'
    $modLog = Join-Path $telemetryFolder 'milestone_mod.log'
    $longLogLines = (1..30 | ForEach-Object { "line $_ from C:\Users\OwnerName\Gravel password=abc$_" }) -join "`r`n"
    [IO.File]::WriteAllText($modLog, $longLogLines)
    $tail = Read-SupportLogTail $modLog 300
    Assert ($tail.Included -and $tail.Truncated -and $tail.Content -notmatch 'OwnerName|abc' -and $tail.Content.Contains('[Last 300 bytes only; partial first line omitted]')) 'Log tail must truncate cleanly on a line boundary and redact paths/secrets.'
    [IO.File]::WriteAllText($modLog, 'Latest log from C:\Users\OwnerName\Gravel password=secret-value')
    $prepared = New-PreparedSupportReport -ProductName 'Gravel' -ProductVersion '0.1.0' -Repository 'd-b-c-e/dbce-mods-gravel' -EntryPoint 'WheelSetup.ps1' -Title 'Wheel setup test' -Description 'Pedal axis test for C:\Users\OwnerName\Gravel' -EnvironmentSummary @{ OS='Windows' } -SelectedSummary @{ Game='Gravel' } -LogPaths @($modLog)
    Assert ($prepared.Preview.Contains('## Selected mod logs (redacted)') -and $prepared.Preview -notmatch 'OwnerName') 'Prepared report must include redacted log preview and require review.'
    $reviewedSnapshot = $prepared.Preview
    [IO.File]::WriteAllText($modLog, 'Changed after review')
    Assert ($prepared.Preview -ceq $reviewedSnapshot) 'Preparing a report must freeze its selected log snapshot.'
    Assert-Throws { $prepared.Body = 'Changed body' } 'A prepared report must reject body mutation.'
    Assert-Throws { New-SupportBrowserSubmission $prepared } 'Browser draft URL must refuse unreviewed reports.'
    Assert-Throws { Save-SupportReviewFile $prepared (Join-Path $fixture 'unreviewed.txt') } 'Report file export must refuse unreviewed reports.'
    Assert-Throws { Save-SupportReviewFile $prepared (Join-Path $fixture 'report.md') -UserReviewed } 'Report file export must require a .txt extension.'
    $submission = New-SupportBrowserSubmission $prepared -UserReviewed
    Assert ($submission.Url.StartsWith('https://github.com/d-b-c-e/dbce-mods-gravel/issues/new?title=') -and $submission.BodyIncludedInUrl) 'Reviewed browser submission must target the Gravel repository.'
    $large = New-PreparedSupportReport -ProductName 'Other game' -ProductVersion '2' -Repository 'owner/another-game' -Title 'Long report' -Description ('Useful reproduction. ' + ('d' * 9000))
    $compactSubmission = New-SupportBrowserSubmission $large -UserReviewed
    Assert (-not $compactSubmission.BodyIncludedInUrl -and $compactSubmission.Url.Length -le 4096 -and $compactSubmission.IssueBody.Contains('Other game') -and $compactSubmission.IssueBody.Contains('Useful reproduction.')) 'Long reports must fall back to a compact browser URL.'
    $exportTxt = Join-Path $fixture 'reviewed-report.txt'
    Save-SupportReviewFile $prepared $exportTxt -UserReviewed
    Assert ((Test-Path -LiteralPath $exportTxt) -and ([IO.File]::ReadAllText($exportTxt)).Contains('# Wheel setup test')) 'Reviewed report must be saved as UTF-8 text.'
    Assert ([IO.File]::ReadAllText($exportTxt) -ceq $reviewedSnapshot) 'Export must use the exact reviewed snapshot.'
    Assert-Throws { Save-SupportReviewFile $prepared $exportTxt -UserReviewed } 'Report export must never overwrite an existing file.'

    $browserCapture = Join-Path $fixture 'browser-urls.txt'
    $env:MILESTONE_UX_BROWSER_OUT = $browserCapture
    $interactiveExport = Join-Path $fixture 'interactive-issue.txt'
    $helpCancelOutput = Run-Setup @('6','i','Test cancel','','x') 6>&1 | Out-String
    Assert ($helpCancelOutput.Contains('Mod on GitHub: https://github.com/d-b-c-e/dbce-mods-gravel') -and $helpCancelOutput.Contains('Wheelkit on GitHub: https://github.com/d-b-c-e/dbce-wheelkit')) 'Help page must display unobtrusive Mod and Wheelkit GitHub links.'
    Assert ($helpCancelOutput.Contains('[I] Submit issue') -and $helpCancelOutput.Contains('Canceled. No report was created.')) 'Help page must keep Submit issue separate and allow canceling.'
    Assert (-not (Test-Path -LiteralPath $browserCapture)) 'Viewing Help or canceling Submit issue must not open browser links.'
    Run-Setup @('6','m','w','i','Brake inversion question','Steps from C:\Users\TestDriver\Gravel','y','e',$interactiveExport,'i','Second draft','Needs browser draft','n','o','x')
    Assert ((Test-Path -LiteralPath $interactiveExport) -and ([IO.File]::ReadAllText($interactiveExport)) -notmatch 'TestDriver' -and ([IO.File]::ReadAllText($interactiveExport)).Contains('--- milestone_mod.log ---')) 'Interactive Submit issue export must include opted-in redacted log.'
    $openedUrls = @([IO.File]::ReadAllLines($browserCapture) | Where-Object { $_ })
    Assert ($openedUrls.Count -eq 3 -and $openedUrls[0] -ceq 'https://github.com/d-b-c-e/dbce-mods-gravel' -and $openedUrls[1] -ceq 'https://github.com/d-b-c-e/dbce-wheelkit' -and $openedUrls[2].StartsWith('https://github.com/d-b-c-e/dbce-mods-gravel/issues/new?title=')) 'Help actions must open Mod, Wheelkit and reviewed issue draft URLs only when explicitly selected.'
    $longDescription = 'Long reproduction details. ' + ('d' * 9000)
    $longFlowOutput = Run-Setup @('6','i','Long browser draft',$longDescription,'n','o',$fixture,'x') 6>&1 | Out-String
    $attachment = Join-Path $fixture 'support-report.txt'
    Assert ((Test-Path -LiteralPath $attachment) -and ([IO.File]::ReadAllText($attachment)).Contains($longDescription)) 'Long interactive drafts must save the complete reviewed attachment before opening.'
    $openedUrls = @([IO.File]::ReadAllLines($browserCapture) | Where-Object { $_ })
    Assert ($openedUrls.Count -eq 4 -and $openedUrls[3].Length -le 4096 -and $longFlowOutput.Contains('Saved this reviewed snapshot:')) 'Long interactive drafts must open only the bounded summary after saving the attachment.'
    Assert ([Convert]::ToBase64String([IO.File]::ReadAllBytes($wc)) -eq [Convert]::ToBase64String($mappingBeforePreview)) 'Help and Submit issue flows must not change saved mappings.'
    Assert ([Convert]::ToBase64String([IO.File]::ReadAllBytes($telemetryFile)) -eq [Convert]::ToBase64String($telemetryBeforePreview)) 'Help and Submit issue flows must not change saved telemetry.'

    if ($HelpOnly) { Write-Host "PASS: $checks affected Help/setup checks. Inert compiler/host only; no visible host, game, browser, network or hardware."; return }

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
    $env:MILESTONE_UX_BROWSER_OUT = $oldBrowserOut
    Remove-Variable -Name MilestoneUxFixtureAnswers -Scope Global -ErrorAction SilentlyContinue
    # Keep fixture and transcript paths for review; no recursive cleanup targets.
    Write-Host "Fixture: $fixture"
}
