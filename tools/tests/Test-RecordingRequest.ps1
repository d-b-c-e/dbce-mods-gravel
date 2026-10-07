$ErrorActionPreference='Stop'
$root=Split-Path (Split-Path $PSScriptRoot)
$fixture=Join-Path ([IO.Path]::GetTempPath()) ('gravel-arm-fixture-'+[guid]::NewGuid().ToString('N'))
$game=Join-Path $fixture 'game'
[IO.Directory]::CreateDirectory($game) | Out-Null
$savedLocal=$env:LOCALAPPDATA
try {
    $env:LOCALAPPDATA=Join-Path $fixture 'local'
    foreach($name in 'gravel-Win64-Shipping.exe','dinput8.dll','milestone_mod.ini') { [IO.File]::WriteAllText((Join-Path $game $name),'inert fixture - never execute') }
    $arm=Join-Path $root 'tools/game/Arm-SignalRecording.ps1'
    $hash=(Get-FileHash -LiteralPath (Join-Path $game 'dinput8.dll')).Hash
    $iniHash=(Get-FileHash -LiteralPath (Join-Path $game 'milestone_mod.ini')).Hash
    $refused=$false; try { & $arm -GameDirectory $game -ExpectedModSha256 ('0'*64) } catch { $refused=$true }
    if(-not $refused -or [IO.File]::Exists((Join-Path $game 'milestone_recording.request.ini'))) { throw 'Wrong candidate armed.' }
    $first=& $arm -GameDirectory $game -ExpectedModSha256 $hash -Seconds 37
    if(-not [IO.File]::Exists($first.Request)) { throw 'Request missing.' }
    $requestHash=(Get-FileHash -LiteralPath $first.Request).Hash
    $refused=$false; try { & $arm -GameDirectory $game -ExpectedModSha256 $hash } catch { $refused=$true }
    if(-not $refused -or (Get-FileHash -LiteralPath $first.Request).Hash -ne $requestHash) { throw 'Existing request was overwritten.' }
    $refused=$false; try { & $arm -GameDirectory $game -Action Cancel -Id ('0'*32) } catch { $refused=$true }
    if(-not $refused -or -not [IO.File]::Exists($first.Request)) { throw 'Foreign request cancelled.' }
    & $arm -GameDirectory $game -Action Cancel -Id $first.Id
    if([IO.File]::Exists($first.Request) -or -not [IO.File]::Exists((Join-Path $first.Result 'cancelled-request.ini'))) { throw 'Cancellation archive failed.' }
    if((Get-FileHash -LiteralPath (Join-Path $game 'milestone_mod.ini')).Hash -ne $iniHash) { throw 'Owner configuration changed.' }
    Write-Host "PASS exact candidate, exclusive request, owned cancel and preserved settings: $fixture"
} finally { $env:LOCALAPPDATA=$savedLocal }
