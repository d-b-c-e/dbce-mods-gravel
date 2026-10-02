param([string]$PackageRoot = (Split-Path (Split-Path $PSScriptRoot -Parent) -Parent))
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PackageRoot 'tools\SetupUx.psm1') -Force
Import-Module (Join-Path $PackageRoot 'tools\InstallPackage.psm1') -Force
$fixture = Join-Path ([IO.Path]::GetTempPath()) ('milestone-install-' + [guid]::NewGuid().ToString('N'))
[IO.Directory]::CreateDirectory($fixture) | Out-Null
$sourceRoot = $PackageRoot
$PackageRoot = Join-Path $fixture 'package'
[IO.Directory]::CreateDirectory($PackageRoot) | Out-Null
foreach ($relative in Get-PackageFiles) {
    $to = Join-Path $PackageRoot $relative
    [IO.Directory]::CreateDirectory((Split-Path $to -Parent)) | Out-Null
    Copy-Item -LiteralPath (Join-Path $sourceRoot $relative) -Destination $to
}
$manifestPath = Join-Path $PackageRoot 'package-manifest.json'
if (Test-Path -LiteralPath (Join-Path $sourceRoot 'package-manifest.json')) {
    Copy-Item -LiteralPath (Join-Path $sourceRoot 'package-manifest.json') -Destination $manifestPath
} else {
    $files = @(Get-PackageFiles | ForEach-Object { [pscustomobject]@{ Path=$_.Replace('\','/'); SHA256=(Get-FileHash -LiteralPath (Join-Path $PackageRoot $_)).Hash } })
    $metadata = @{ Product='milestone-wheel-tools'; Version=([IO.File]::ReadAllText((Join-Path $PackageRoot 'VERSION'))).Trim(); SourceCommit=('a'*40); Files=$files }
    [IO.File]::WriteAllText($manifestPath, ($metadata | ConvertTo-Json -Depth 6))
}
$checks = 0
function Assert($Condition, [string]$Message) { if (-not $Condition) { throw $Message }; $script:checks++ }
function Reject([scriptblock]$Action, [string]$Message, [string]$Pattern) {
    $failed = $false
    $errorText = ''
    try { & $Action | Out-Null } catch { $failed = $true; $errorText = $_.Exception.Message }
    Assert $failed $Message
    if ($Pattern) { Assert ($errorText -match $Pattern) "Wrong failure for $Message : $errorText" }
}
function Entry([string]$Name, [string]$Text) { [pscustomobject]@{ Path=(Join-Path $fixture $Name); Bytes=[Text.Encoding]::UTF8.GetBytes($Text) } }
function Snapshot([string]$Root) {
    $rootFull=[IO.Path]::GetFullPath($Root)
    $items=@(Get-Item -LiteralPath $rootFull) + @(Get-ChildItem -LiteralPath $rootFull -Recurse -Force)
    @($items | Sort-Object FullName | ForEach-Object {
        # Query timestamp directly rather than cached directory-enumeration data.
        $lastWrite=if($_.PSIsContainer){[IO.Directory]::GetLastWriteTimeUtc($_.FullName).Ticks}else{[IO.File]::GetLastWriteTimeUtc($_.FullName).Ticks}
        [pscustomobject]@{Path=$_.FullName.Substring($rootFull.Length);Directory=$_.PSIsContainer;Attributes=[int]$_.Attributes;LastWriteUtc=$lastWrite;Hash=$(if(-not $_.PSIsContainer){Get-ByteHash ([IO.File]::ReadAllBytes($_.FullName))}else{$null})}
    }) | ConvertTo-Json -Depth 5 -Compress
}
function Get-Process { @() }
try {
    Assert (Test-KnownLegacyProxy 'a9940602daee99f3a7139b56230df2b4b6466be80e78b356e9cd93d8ecb531be') 'Verified v0.1.0 identity missing.'
    Assert (-not (Test-KnownLegacyProxy ('a'*64))) 'Unknown legacy identity accepted.'
    Assert-PackageManifest $PackageRoot
    $manifestText = [IO.File]::ReadAllText($manifestPath)
    Remove-Item -LiteralPath $manifestPath
    Reject { & (Join-Path $PackageRoot 'Install.ps1') -GamePath $fixture } 'Missing manifest was accepted.' 'Package manifest missing'
    [IO.File]::WriteAllText($manifestPath, $manifestText)
    foreach ($relative in 'dist\dinput8.dll','WheelSetup.ps1') {
        $path = Join-Path $PackageRoot $relative
        $originalBytes = [IO.File]::ReadAllBytes($path)
        [IO.File]::AppendAllText($path, 'fixture alteration')
        Reject { & (Join-Path $PackageRoot 'Install.ps1') -GamePath $fixture } "Modified payload was accepted: $relative" 'Package hash mismatch'
        [IO.File]::WriteAllBytes($path, $originalBytes)
    }
    foreach ($change in 'duplicate','missing','unexpected','private-save','generated-game','product','version') {
        $bad = $manifestText | ConvertFrom-Json
        switch ($change) {
            'duplicate' { $bad.Files[1].Path = $bad.Files[0].Path }
            'missing' { $bad.Files = @($bad.Files | Select-Object -Skip 1) }
            'unexpected' { $bad.Files[0].Path = '../escape.txt' }
            'private-save' { $bad.Files[0].Path = 'games/gravel/settings.sav' }
            'generated-game' { $bad.Files[0].Path = 'games/gravel/generated-game.bin' }
            'product' { $bad.Product = 'other-product' }
            'version' { $bad.Version = '999' }
        }
        [IO.File]::WriteAllText($manifestPath, ($bad | ConvertTo-Json -Depth 6))
        Reject { Assert-PackageManifest $PackageRoot } "Invalid $change manifest was accepted."
    }
    [IO.File]::WriteAllText($manifestPath, $manifestText)
    Assert (@(Get-PackageFiles | Where-Object { $_ -match '\.(sav|rom|bin|fzpt)$|milestone_mod\.log$|milestone_install\.json$' }).Count -eq 0) 'Private/generated game assets entered the package allowlist.'
    $old = Join-Path $fixture 'existing.ini'
    $empty = Join-Path $fixture 'empty.ini'
    [IO.File]::WriteAllText($old, 'owner data')
    [IO.File]::WriteAllBytes($empty, [byte[]]@())
    $items = @((Entry 'existing.ini' 'replacement'),(Entry 'new\new.ini' 'new file'),(Entry 'empty.ini' 'new nonempty'))
    Reject { Invoke-PackageInstall $items $fixture (Join-Path $fixture 'rollback') -FailAfterWrite 3 } 'Injected write failure was not reported.'
    Assert ([IO.File]::ReadAllText($old) -ceq 'owner data') 'Rollback lost existing data.'
    Assert (-not (Test-Path -LiteralPath (Join-Path $fixture 'new\new.ini'))) 'Rollback left a newly created file.'
    Assert ((Get-Item -LiteralPath $empty).Length -eq 0) 'Rollback did not preserve an empty file.'
    Assert ([IO.File]::ReadAllText((Join-Path $fixture 'rollback\000-existing.ini')) -ceq 'owner data') 'Backup bytes differ.'
    Assert ((Get-Item -LiteralPath (Join-Path $fixture 'rollback\002-empty.ini')).Length -eq 0) 'Empty-file backup missing.'
    Reject { Invoke-PackageInstall @((Entry '..\escape.ini' 'bad')) $fixture (Join-Path $fixture 'outside') } 'An install path escaped its game folder.'
    Reject { Invoke-PackageInstall @($items[0],$items[0]) $fixture (Join-Path $fixture 'duplicate') } 'Duplicate target was not rejected.'
    Assert ([IO.File]::ReadAllText($old) -ceq 'owner data') 'Preflight failure changed files.'
    $script:closedChecks = 0
    $guard = { $script:closedChecks++; if ($script:closedChecks -eq 3) { throw 'fixture game started' } }
    Reject { Invoke-PackageInstall $items $fixture (Join-Path $fixture 'guarded') -BeforeWrite $guard } 'Running-game guard was ignored.'
    Assert ([IO.File]::ReadAllText($old) -ceq 'owner data') 'Game-start failure did not restore previous file.'
    Assert (-not (Test-Path -LiteralPath (Join-Path $fixture 'new\new.ini'))) 'Guard failed before writing a new file.'
    $script:closedChecks = 0
    $persistentGuard = { $script:closedChecks++; if ($script:closedChecks -ge 3) { throw 'fixture game remains open' } }
    Reject { Invoke-PackageInstall $items $fixture (Join-Path $fixture 'still-running') -BeforeWrite $persistentGuard } 'A still-running game did not block rollback writes.'
    Assert ([IO.File]::ReadAllText($old) -ceq 'replacement') 'Rollback wrote files while the game was reported open.'
    Assert ([IO.File]::ReadAllText((Join-Path $fixture 'still-running\000-existing.ini')) -ceq 'owner data') 'Interrupted rollback has no recovery copy.'
    [IO.File]::WriteAllText($old, 'owner data')
    $script:closedChecks = 0
    $concurrent = { $script:closedChecks++; if ($script:closedChecks -eq 2) { [IO.File]::WriteAllText($old, 'owner changed it') } }
    Reject { Invoke-PackageInstall $items $fixture (Join-Path $fixture 'concurrent') -BeforeWrite $concurrent } 'Concurrent edit was not detected.'
    Assert ([IO.File]::ReadAllText($old) -ceq 'owner changed it') 'Concurrent owner edit was overwritten.'
    [IO.File]::WriteAllText($old, 'owner data')
    $script:closedChecks = 0
    $rollbackEdit = {
        $script:closedChecks++
        if ($script:closedChecks -eq 3) { [IO.File]::WriteAllText($old, 'external rollback edit'); throw 'fixture interrupted' }
    }
    Reject { Invoke-PackageInstall $items $fixture (Join-Path $fixture 'rollback-edit') -BeforeWrite $rollbackEdit } 'Install rollback overwrote an external edit.' 'Rollback needs attention'
    Assert ([IO.File]::ReadAllText($old) -ceq 'external rollback edit') 'Rollback erased external bytes.'
    $raceEntry = Entry 'existing.ini' 'replacement'
    $raceEntry | Add-Member -NotePropertyName ExpectedHash -NotePropertyValue (Get-ByteHash ([Text.Encoding]::UTF8.GetBytes('owner data')))
    Reject { Invoke-PackageInstall @($raceEntry) $fixture (Join-Path $fixture 'ownership-race') } 'Changed ownership snapshot accepted.' 'Ownership changed'
    $raceBefore=Snapshot $fixture
    Reject { Get-PackageInstallPlan @($raceEntry) $fixture } 'Plan accepted ownership race.' 'Ownership changed'
    $raceAfter=Snapshot $fixture
    Assert ($raceAfter -ceq $raceBefore) 'Ownership-race plan changed external bytes or filesystem state.'
    $pathBefore=Snapshot $fixture
    Reject { Get-PackageInstallPlan @((Entry '..\escape.ini' 'bad')) $fixture } 'Plan accepted escaped path.'
    Reject { Get-PackageInstallPlan @($items[0],$items[0]) $fixture } 'Plan accepted duplicate target.'
    Assert ((Snapshot $fixture) -ceq $pathBefore) 'Failed plan path validation wrote files.'
    $directoryTarget=Join-Path $fixture 'directory-file-conflict'
    [IO.Directory]::CreateDirectory($directoryTarget) | Out-Null
    $directoryEntries=@([pscustomobject]@{Path=$directoryTarget;Bytes=[Text.Encoding]::UTF8.GetBytes('new')})
    $before=Snapshot $fixture
    Reject { Get-PackageInstallPlan $directoryEntries $fixture } 'Shared planner accepted directory file target.' 'Directory conflicts'
    Reject { Invoke-PackageInstall $directoryEntries $fixture (Join-Path $fixture 'directory-backup') } 'Transaction accepted directory file target.' 'Directory conflicts'
    Assert ((Snapshot $fixture) -ceq $before) 'Directory conflict changed fixture state.'
    $junctionRoot=Join-Path $fixture 'junction-case'
    $outside=Join-Path $fixture 'outside-allowed-root'
    [IO.Directory]::CreateDirectory($junctionRoot) | Out-Null
    [IO.Directory]::CreateDirectory($outside) | Out-Null
    [IO.File]::WriteAllText((Join-Path $outside 'sentinel.ini'),'synthetic outside bytes')
    $link=Join-Path $junctionRoot 'linked'
    New-Item -ItemType Junction -Path $link -Target $outside -ErrorAction Stop | Out-Null
    $linkEntries=@([pscustomobject]@{Path=(Join-Path $link 'sentinel.ini');Bytes=[Text.Encoding]::UTF8.GetBytes('replacement')})
    $before=Snapshot $junctionRoot; $outsideBefore=Snapshot $outside
    $lock=[IO.File]::Open((Join-Path $outside 'sentinel.ini'),'Open','Read',[IO.FileShare]::None)
    try {
        Reject { Get-PackageInstallPlan $linkEntries $junctionRoot } 'Planner followed junction before rejecting it.' 'Reparse'
        Reject { Invoke-PackageInstall $linkEntries $junctionRoot (Join-Path $junctionRoot 'backup') } 'Transaction followed junction.' 'Reparse'
        Reject { Get-PackageInstallPlan $linkEntries $link } 'Planner accepted reparse allowed root.' 'Reparse'
        $nestedEntries=@([pscustomobject]@{Path=(Join-Path $link 'nested\new.ini');Bytes=[Text.Encoding]::UTF8.GetBytes('new')})
        Reject { Get-PackageInstallPlan $nestedEntries (Join-Path $link 'nested') } 'Planner accepted reparse above allowed root.' 'Reparse'
    } finally { $lock.Dispose() }
    Assert ((Snapshot $junctionRoot) -ceq $before) 'Junction rejection changed target state.'
    Assert ((Snapshot $outside) -ceq $outsideBefore) 'Junction rejection changed outside bytes.'
    $raceLink=Join-Path $junctionRoot 'late-link'
    $raceLinkEntries=@([pscustomobject]@{Path=(Join-Path $raceLink 'new.ini');Bytes=[Text.Encoding]::UTF8.GetBytes('new')})
    $lateLinkGuard={New-Item -ItemType Junction -Path $raceLink -Target $outside -ErrorAction Stop | Out-Null}
    Reject { Invoke-PackageInstall $raceLinkEntries $junctionRoot (Join-Path $junctionRoot 'late-backup') -BeforeWrite $lateLinkGuard } 'Fresh prewrite checks accepted late junction.' 'Reparse'
    Assert (-not (Test-Path -LiteralPath (Join-Path $junctionRoot 'late-backup'))) 'Late junction created backup writes.'
    Assert ((Snapshot $outside) -ceq $outsideBefore) 'Late junction wrote outside allowed root.'
    [IO.File]::WriteAllText($old, 'owner data')
    $result = Invoke-PackageInstall $items $fixture (Join-Path $fixture 'success')
    Assert ($result.ChangedFiles -eq 3) 'Transaction did not install all files.'
    Assert ([IO.File]::ReadAllText($old) -ceq 'replacement') 'Successful install content is wrong.'
    Assert (@(Get-ChildItem $fixture -Recurse -Filter '.dbce-write-*').Count -eq 0) 'Temporary writer files leaked.'

    $template = "[/Wheel.Config/c262046d]`r`nProductName=Template`r`nWheel_Steer=Axis1&1.0&0.0`r`nWheel_RightTrigger=Button1`r`nMaxRotationAngle=900.0`r`n`r`n"
    $profile = Add-WheelProfile $template '0006346e' 'Fixture Wheel'
    Assert ($profile.Contains('ProductName=Fixture Wheel')) 'Fresh profile has no friendly name.'
    Assert ((Get-WheelField $profile '0006346e' 'Wheel_Steer') -eq '') 'Fresh profile guessed an axis.'
    Assert ((Get-WheelField $profile '0006346e' 'Wheel_RightTrigger') -eq '') 'Fresh profile guessed a button.'
    Assert ($profile -notmatch '(?<!\r)\n') 'Profile creation damaged CRLF.'
    Assert ((Add-WheelProfile $profile '0006346e' 'Another name') -ceq $profile) 'Existing profile was changed.'
    Reject { Add-WheelProfile '[unknown]' '0006346e' 'Wheel' } 'Missing verified profile template accepted.'

    # Real packaged scripts, inert exe sentinel and no native input execution.
    $gameRoot = Join-Path $fixture 'Gravel'
    $game = Join-Path $gameRoot 'gravel\Binaries\Win64'
    $wheel = Join-Path $gameRoot 'gravel\Config\WindowsNoEditor\WheelConfig.ini'
    [IO.Directory]::CreateDirectory($game) | Out-Null
    [IO.Directory]::CreateDirectory((Split-Path $wheel -Parent)) | Out-Null
    [IO.File]::WriteAllText((Join-Path $game 'gravel-Win64-Shipping.exe'), 'inert fixture: never execute')
    [IO.File]::WriteAllText($wheel, $profile)
    $ini = Join-Path $game 'milestone_mod.ini'
    $custom = "[proxy]`r`nproduct=0006346e`r`n[telemetry]`r`nenabled=0`r`nhost=192.0.2.25`r`nport=8000`r`nformat=custom-owner`r`n[custom]`r`nkeep=yes`r`n"
    [IO.File]::WriteAllText($ini, $custom, [Text.Encoding]::Unicode)
    $iniHash = Get-ByteHash ([IO.File]::ReadAllBytes($ini))
    $wheelHash = Get-ByteHash ([IO.File]::ReadAllBytes($wheel))
    [IO.File]::WriteAllText((Join-Path $game 'dinput8.dll'), 'another proxy')
    Reject { & (Join-Path $PackageRoot 'Install.ps1') -GamePath $game } 'An unrelated proxy was replaced.'
    Assert ([IO.File]::ReadAllText((Join-Path $game 'dinput8.dll')) -eq 'another proxy') 'Unrelated proxy bytes changed.'
    Assert (-not (Test-Path -LiteralPath (Join-Path $game 'DBCE-Wheel-Backups'))) 'Rejected install mutated backup state.'
    [IO.File]::WriteAllText((Join-Path $game 'dinput8.dll'), 'spoof milestone_mod proxy')
    Reject { & (Join-Path $PackageRoot 'Install.ps1') -GamePath $game } 'Marker spoof was installed.' 'Unknown installed file'
    Reject { & (Join-Path $PackageRoot 'Uninstall.ps1') -GamePath $game } 'Marker spoof was removed.' 'Unknown receiptless'
    Assert ([IO.File]::ReadAllText((Join-Path $game 'dinput8.dll')) -ceq 'spoof milestone_mod proxy') 'Marker spoof bytes changed.'
    $conflictBefore=Snapshot $gameRoot
    Reject { & (Join-Path $PackageRoot 'Install.ps1') -GamePath $game -Check } 'Plan accepted unknown proxy.' 'Unknown installed file'
    Assert ((Snapshot $gameRoot) -ceq $conflictBefore) 'Conflict plan changed target filesystem.'
    Remove-Item -LiteralPath (Join-Path $game 'dinput8.dll')
    $orphanSetup = Join-Path $game 'DBCE-Wheel-Setup\WheelSetup.ps1'
    [IO.Directory]::CreateDirectory((Split-Path $orphanSetup -Parent)) | Out-Null
    [IO.File]::WriteAllText($orphanSetup, 'unreceipted owner setup')
    Reject { & (Join-Path $PackageRoot 'Install.ps1') -GamePath $game } 'Unreceipted setup was replaced.' 'Unknown installed file'
    Assert ([IO.File]::ReadAllText($orphanSetup) -ceq 'unreceipted owner setup') 'Orphan setup changed.'
    Remove-Item -LiteralPath $orphanSetup
    [IO.File]::WriteAllText((Join-Path $gameRoot 'Wheel settings.bat'), 'REM DBCE Milestone wheel settings spoof')
    Reject { & (Join-Path $PackageRoot 'Install.ps1') -GamePath $game } 'Launcher marker spoof was replaced.' 'Unknown installed file'
    Remove-Item -LiteralPath (Join-Path $gameRoot 'Wheel settings.bat')
    foreach($relative in @('dinput8.dll','milestone_install.json','milestone_mod.ini','DBCE-Wheel-Setup\WheelSetup.ps1')) {
        $target=Join-Path $game $relative
        $savedBytes=if([IO.File]::Exists($target)){[IO.File]::ReadAllBytes($target)}else{$null}
        if($null -ne $savedBytes){Remove-Item -LiteralPath $target}
        [IO.Directory]::CreateDirectory($target) | Out-Null
        $before=Snapshot $gameRoot
        Reject { & (Join-Path $PackageRoot 'Install.ps1') -GamePath $game -Check -Product 0006346e } "Full plan accepted directory $relative" 'Directory conflicts'
        Assert ((Snapshot $gameRoot) -ceq $before) "Directory $relative plan changed target."
        [IO.Directory]::Delete($target,$false)
        if($null -ne $savedBytes){[IO.File]::WriteAllBytes($target,$savedBytes)}
    }
    $setupJunction=Join-Path $game 'DBCE-Wheel-Setup'
    # The earlier orphan fixture left an empty directory; remove only that
    # verified-empty synthetic directory, never recursively traverse the link.
    if(Test-Path -LiteralPath $setupJunction){[IO.Directory]::Delete($setupJunction,$false)}
    New-Item -ItemType Junction -Path $setupJunction -Target $outside -ErrorAction Stop | Out-Null
    $before=Snapshot $gameRoot; $outsideBefore=Snapshot $outside
    Reject { & (Join-Path $PackageRoot 'Install.ps1') -GamePath $game -Check -Product 0006346e } 'Full installer plan accepted setup junction.' 'Reparse'
    Assert ((Snapshot $gameRoot) -ceq $before) 'Setup-junction plan changed target.'
    Assert ((Snapshot $outside) -ceq $outsideBefore) 'Setup-junction plan changed outside bytes.'
    [IO.Directory]::Delete($setupJunction,$false)
    # Hardware discovery is replaced only in this disposable package, with its
    # manifest updated. Plans must pass or fail without calling the sentinel.
    $toolkitPath=Join-Path $PackageRoot 'lib\toolkit\powershell\DbceWheel.psm1'
    $toolkitBytes=[IO.File]::ReadAllBytes($toolkitPath)
    [IO.File]::WriteAllText($toolkitPath,"function Get-DirectInputDevices { throw 'forbidden fixture hardware enumeration' }`nfunction Get-SteamLibraries { @() }`nExport-ModuleMember -Function Get-DirectInputDevices,Get-SteamLibraries`n")
    $guardManifest=$manifestText | ConvertFrom-Json
    ($guardManifest.Files | Where-Object Path -CEQ 'lib/toolkit/powershell/DbceWheel.psm1').SHA256=Get-ByteHash ([IO.File]::ReadAllBytes($toolkitPath))
    [IO.File]::WriteAllText($manifestPath,($guardManifest | ConvertTo-Json -Depth 6))
    foreach ($mode in 'Check','DryRun','WhatIf') {
        $before=Snapshot $gameRoot; $packageBefore=Snapshot $PackageRoot
        $arguments=@{GamePath=$game;Product='0006346e'}; $arguments[$mode]=$true
        $planned=& (Join-Path $PackageRoot 'Install.ps1') @arguments
        Assert ($planned.Mode -ceq 'Check' -and $planned.WritesPerformed -eq 0) "$mode did not return no-write plan."
        Assert ($planned.Files.Count -eq (@(Get-SetupPackageFiles).Count+5)) "$mode omitted planned targets."
        Assert (($planned.Files | Where-Object Path -EQ $ini).Action -ceq 'Preserve') "$mode did not report preserved INI."
        Assert (($planned.Files | Where-Object Path -EQ $wheel).Action -ceq 'Preserve') "$mode did not report preserved wheel profile."
        Assert ((Snapshot $gameRoot) -ceq $before) "$mode changed target snapshot."
        Assert ((Snapshot $PackageRoot) -ceq $packageBefore) "$mode changed source package."
    }
    $before=Snapshot $gameRoot
    $planned=& (Join-Path $PackageRoot 'Install.ps1') -GamePath $game -Check -Port 9456
    Assert (($planned.Files | Where-Object Path -EQ $ini).Action -ceq 'Replace') 'Explicit plan override was not reported.'
    Assert ($planned.SettingsOverrides.Count -eq 1 -and $planned.SettingsOverrides[0] -ceq 'Port' -and -not $planned.HardwareEnumerated) 'Plan did not identify explicit override and no hardware enumeration.'
    Assert ((Snapshot $gameRoot) -ceq $before) 'Explicit override plan changed saved settings.'
    $savedIniBytes=[IO.File]::ReadAllBytes($ini)
    [IO.File]::WriteAllText($ini,"[proxy]`r`nproduct=invalid`r`n")
    $before=Snapshot $gameRoot
    Reject { & (Join-Path $PackageRoot 'Install.ps1') -GamePath $game -Check } 'Invalid saved identity plan accepted.' 'saved wheel identity'
    Assert ((Snapshot $gameRoot) -ceq $before) 'Invalid-identity plan changed files.'
    Remove-Item -LiteralPath $ini
    $before=Snapshot $gameRoot
    Reject { & (Join-Path $PackageRoot 'Install.ps1') -GamePath $game -Check } 'Plan without identity accepted.' 'Check mode requires'
    Assert ((Snapshot $gameRoot) -ceq $before) 'Missing-identity plan wrote files.'
    $planned=& (Join-Path $PackageRoot 'Install.ps1') -GamePath $game -Check -Product 0006346e
    Assert (($planned.Files | Where-Object Path -EQ $ini).Action -ceq 'Create') 'Fresh explicit-identity plan did not report new INI.'
    Assert ((Snapshot $gameRoot) -ceq $before) 'Fresh plan wrote files.'
    [IO.File]::WriteAllBytes($ini,$savedIniBytes)
    [IO.File]::WriteAllBytes($toolkitPath,$toolkitBytes)
    [IO.File]::WriteAllText($manifestPath,$manifestText)
    & (Join-Path $PackageRoot 'Install.ps1') -GamePath $game
    Assert ((Get-ByteHash ([IO.File]::ReadAllBytes($ini))) -eq $iniHash) 'Update changed encoding or custom INI bytes.'
    Assert ((Get-ByteHash ([IO.File]::ReadAllBytes($wheel))) -eq $wheelHash) 'Update changed existing wheel profile.'
    $receipt = [IO.File]::ReadAllText((Join-Path $game 'milestone_install.json')) | ConvertFrom-Json
    Assert ($receipt.GameRoot -eq $gameRoot) 'Canonical game layout was resolved incorrectly.'
    foreach ($item in $receipt.OwnedFiles) { Assert ((Get-ByteHash ([IO.File]::ReadAllBytes($item.Path))) -eq $item.Hash) "Installed receipt mismatch: $($item.Path)" }
    $receiptPath = Join-Path $game 'milestone_install.json'
    $receiptBytes = [IO.File]::ReadAllBytes($receiptPath)
    $before=Snapshot $gameRoot
    $planned=& (Join-Path $PackageRoot 'Install.ps1') -GamePath $game -Check
    Assert ((Snapshot $gameRoot) -ceq $before) 'Receipt-backed update plan wrote files.'
    Assert (($planned.Files | Where-Object Path -EQ (Join-Path $game 'dinput8.dll')).Action -ceq 'Preserve') 'Update plan did not preserve known proxy.'
    $global:GravelPlanRaceFixture=@{Checks=0;File=(Join-Path $game 'DBCE-Wheel-Setup\QUICKSTART.md');Root=$gameRoot;Snapshot=$null}
    $raceBytes=[IO.File]::ReadAllBytes($global:GravelPlanRaceFixture.File)
    function Get-Process {
        $global:GravelPlanRaceFixture.Checks++
        if($global:GravelPlanRaceFixture.Checks -eq 2) {
            [IO.File]::AppendAllText($global:GravelPlanRaceFixture.File,'external plan race edit')
            $global:GravelPlanRaceFixture.Snapshot=Snapshot $global:GravelPlanRaceFixture.Root
        }
        @()
    }
    Reject { & (Join-Path $PackageRoot 'Install.ps1') -GamePath $game -Check } 'Plan missed edit after ownership validation.' 'Ownership changed'
    Assert ((Snapshot $gameRoot) -ceq $global:GravelPlanRaceFixture.Snapshot) 'Plan race erased external edit or changed other targets.'
    [IO.File]::WriteAllBytes($global:GravelPlanRaceFixture.File,$raceBytes)
    Remove-Variable -Name GravelPlanRaceFixture -Scope Global
    function Get-Process { @() }
    $global:GravelPlanRunningFixturePath=Join-Path $game 'gravel-Win64-Shipping.exe'
    function Get-Process { [pscustomobject]@{Path=$global:GravelPlanRunningFixturePath} }
    $before=Snapshot $gameRoot
    Reject { & (Join-Path $PackageRoot 'Install.ps1') -GamePath $game -Check } 'Plan ignored running-game guard.' 'Gravel is running'
    Assert ((Snapshot $gameRoot) -ceq $before) 'Running-game refusal changed plan target.'
    function Get-Process { @() }
    Remove-Variable -Name GravelPlanRunningFixturePath -Scope Global
    $setup = Join-Path $game 'DBCE-Wheel-Setup\WheelSetup.ps1'
    $setupBytes = [IO.File]::ReadAllBytes($setup)
    [IO.File]::AppendAllText($setup, 'owner edit')
    $editedHash = Get-ByteHash ([IO.File]::ReadAllBytes($setup))
    Reject { & (Join-Path $PackageRoot 'Install.ps1') -GamePath $game } 'Modified setup was overwritten.' 'Modified installed file'
    Assert ((Get-ByteHash ([IO.File]::ReadAllBytes($setup))) -ceq $editedHash) 'Edited setup changed on refusal.'
    Assert ((Get-ByteHash ([IO.File]::ReadAllBytes($receiptPath))) -ceq (Get-ByteHash $receiptBytes)) 'Refused update changed receipt.'
    $before=Snapshot $gameRoot
    Reject { & (Join-Path $PackageRoot 'Install.ps1') -GamePath $game -Check } 'Plan accepted edited setup.' 'Modified installed file'
    Assert ((Snapshot $gameRoot) -ceq $before) 'Edited-setup plan changed target.'
    [IO.File]::WriteAllBytes($setup, $setupBytes)
    foreach ($mutation in 'json','duplicate','missing','hash','game','version','path') {
        $bad = [Text.Encoding]::UTF8.GetString($receiptBytes) | ConvertFrom-Json
        switch ($mutation) {
            'duplicate' { $bad.OwnedFiles[1] = $bad.OwnedFiles[0] }
            'missing' { $bad.OwnedFiles = @($bad.OwnedFiles | Select-Object -Skip 1) }
            'hash' { $bad.OwnedFiles[0].Hash = 'bad' }
            'game' { $bad.Game = 'MXGP' }
            'version' { $bad.Version = $null }
            'path' { $bad.OwnedFiles[0].Path = Join-Path $fixture 'outside-owned.dll' }
        }
        $badText = if ($mutation -eq 'json') { '{broken' } else { $bad | ConvertTo-Json -Depth 6 }
        [IO.File]::WriteAllText($receiptPath, $badText)
        Reject { & (Join-Path $PackageRoot 'Install.ps1') -GamePath $game } "Malformed $mutation receipt allowed upgrade."
        Reject { & (Join-Path $PackageRoot 'Uninstall.ps1') -GamePath $game } "Malformed $mutation receipt allowed removal."
        $before=Snapshot $gameRoot
        Reject { & (Join-Path $PackageRoot 'Install.ps1') -GamePath $game -Check } "Plan accepted malformed $mutation receipt."
        Assert ((Snapshot $gameRoot) -ceq $before) "Malformed $mutation plan changed target filesystem."
        Assert ((Get-ByteHash ([IO.File]::ReadAllBytes($setup))) -ceq (Get-ByteHash $setupBytes)) "Malformed $mutation receipt changed setup."
    }
    [IO.File]::WriteAllBytes($receiptPath, $receiptBytes)
    # A synthetic older setup with a matching valid prior receipt can upgrade.
    [IO.File]::WriteAllText($setup, 'synthetic prior owned setup')
    $prior = [Text.Encoding]::UTF8.GetString($receiptBytes) | ConvertFrom-Json
    ($prior.OwnedFiles | Where-Object Path -EQ $setup).Hash = Get-ByteHash ([IO.File]::ReadAllBytes($setup))
    [IO.File]::WriteAllText($receiptPath, ($prior | ConvertTo-Json -Depth 6))
    & (Join-Path $PackageRoot 'Install.ps1') -GamePath $game
    Assert ((Get-ByteHash ([IO.File]::ReadAllBytes($setup))) -ceq (Get-ByteHash $setupBytes)) 'Valid prior-receipt upgrade did not restore new payload.'
    $receipt = [IO.File]::ReadAllText($receiptPath) | ConvertFrom-Json
    $launcher = [IO.File]::ReadAllText((Join-Path $gameRoot 'Wheel settings.bat'))
    Assert ($launcher.Contains('-ModConfig') -and $launcher.Contains($ini)) 'Launcher omitted exact game configuration.'
    $lockedFile = Join-Path $game 'DBCE-Wheel-Setup\dist\wheelprobe.exe'
    $lock = [IO.File]::Open($lockedFile, 'Open', 'Read', [IO.FileShare]::Read)
    try { Reject { & (Join-Path $PackageRoot 'Uninstall.ps1') -GamePath $game } 'Locked late removal did not fail visibly.' }
    finally { $lock.Dispose() }
    foreach ($item in $receipt.OwnedFiles) { Assert ((Get-ByteHash ([IO.File]::ReadAllBytes($item.Path))) -eq $item.Hash) "Removal rollback did not restore $($item.Path)" }
    $modified = Join-Path $game 'DBCE-Wheel-Setup\QUICKSTART.md'
    [IO.File]::AppendAllText($modified, 'owner note')
    & (Join-Path $PackageRoot 'Uninstall.ps1') -GamePath $game
    Assert (-not (Test-Path -LiteralPath (Join-Path $game 'dinput8.dll'))) 'Uninstall left the installed proxy.'
    Assert ([IO.File]::ReadAllText($modified).EndsWith('owner note')) 'Uninstall deleted an edited setup file.'
    Assert ((Get-ByteHash ([IO.File]::ReadAllBytes($ini))) -eq $iniHash) 'Uninstall changed owner INI.'
    Assert ((Get-ByteHash ([IO.File]::ReadAllBytes($wheel))) -eq $wheelHash) 'Uninstall changed owner profile.'
    $legacy = Join-Path $fixture 'legacy'
    [IO.Directory]::CreateDirectory($legacy) | Out-Null
    [IO.File]::WriteAllText((Join-Path $legacy 'gravel-Win64-Shipping.exe'), 'inert')
    Copy-Item -LiteralPath (Join-Path $PackageRoot 'dist\dinput8.dll') -Destination (Join-Path $legacy 'dinput8.dll')
    & (Join-Path $PackageRoot 'Uninstall.ps1') -GamePath $legacy
    Assert (-not (Test-Path -LiteralPath (Join-Path $legacy 'dinput8.dll'))) 'Known exact receiptless proxy was retained.'
    Copy-Item -LiteralPath (Join-Path $PackageRoot 'dist\dinput8.dll') -Destination (Join-Path $legacy 'dinput8.dll')
    & (Join-Path $PackageRoot 'Install.ps1') -GamePath $legacy -Product 0006346e -SkipWheelConfig
    Assert (Test-Path -LiteralPath (Join-Path $legacy 'milestone_install.json')) 'Known receiptless upgrade did not create receipt.'
    & (Join-Path $PackageRoot 'Uninstall.ps1') -GamePath $legacy
    [IO.File]::WriteAllText((Join-Path $legacy 'dinput8.dll'), 'unknown milestone_mod')
    [IO.File]::WriteAllText((Join-Path $legacy 'milestone_mod.log'), 'owner log')
    Reject { & (Join-Path $PackageRoot 'Uninstall.ps1') -GamePath $legacy } 'Unknown receiptless removal accepted.' 'Unknown receiptless'
    Assert ([IO.File]::ReadAllText((Join-Path $legacy 'milestone_mod.log')) -ceq 'owner log') 'Unknown proxy removal altered log.'
    $removeA = Join-Path $fixture 'remove-a.txt'
    $removeB = Join-Path $fixture 'remove-b.txt'
    [IO.File]::WriteAllText($removeA, 'original a')
    [IO.File]::WriteAllText($removeB, 'original b')
    $removeItems = @($removeA,$removeB) | ForEach-Object { [pscustomobject]@{ Path=$_; Hash=(Get-ByteHash ([IO.File]::ReadAllBytes($_))) } }
    $script:closedChecks = 0
    $removalEdit = { $script:closedChecks++; if ($script:closedChecks -eq 3) { [IO.File]::WriteAllText($removeA, 'external replacement'); throw 'fixture interrupted' } }
    Reject { Invoke-PackageRemoval $removeItems $fixture (Join-Path $fixture 'removal-edit') -BeforeWrite $removalEdit } 'Removal rollback accepted external replacement.' 'rollback needs attention'
    Assert ([IO.File]::ReadAllText($removeA) -ceq 'external replacement') 'Removal rollback erased external replacement.'
    Assert ([IO.File]::ReadAllText($removeB) -ceq 'original b') 'Removal failure changed later file.'
    $other = Join-Path $fixture 'UnsupportedGame'
    [IO.Directory]::CreateDirectory($other) | Out-Null
    Reject { & (Join-Path $PackageRoot 'Install.ps1') -GamePath $other -Product 0006346e } 'Unsupported game folder accepted.'
    Assert (@(Get-ChildItem -LiteralPath $other).Count -eq 0) 'Unsupported game folder was modified.'
    $incomplete = Join-Path $fixture 'incomplete-package'
    [IO.Directory]::CreateDirectory((Join-Path $incomplete 'tools')) | Out-Null
    [IO.Directory]::CreateDirectory((Join-Path $incomplete 'lib\toolkit\powershell')) | Out-Null
    foreach ($relative in 'Install.ps1','tools\SetupUx.psm1','tools\InstallPackage.psm1','lib\toolkit\powershell\DbceWheel.psm1') {
        Copy-Item -LiteralPath (Join-Path $PackageRoot $relative) -Destination (Join-Path $incomplete $relative)
    }
    Reject { & (Join-Path $incomplete 'Install.ps1') -GamePath $game } 'An incomplete package was installed.'
    Assert (-not (Test-Path -LiteralPath (Join-Path $game 'dinput8.dll'))) 'Incomplete package changed the runtime.'
    Write-Host "PASS: $checks install/package checks. No game or hardware was opened."
} finally { Write-Host "Fixture: $fixture" }
