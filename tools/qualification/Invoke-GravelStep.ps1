# One observed navigation step at a time until an actual offline-race route has been recorded.
param(
    [Parameter(Mandatory)][ValidateSet('enter','up','down','left','right','back','probe_camera','capture')][string]$Action,
    [int]$ProcessId, [long]$StartedUtcTicks, [long]$Window,
    [string]$GameDirectory,
    [string]$Evidence,
    [string]$LeaseFile = "$env:LOCALAPPDATA/dbce/claude-lease-mevu.json",
    [switch]$DryRun
)
$ErrorActionPreference = 'Stop'
$kit = 'E:/Source/game-modding/_toolkits/dbce-wheel-mod-toolkit'
$play = "$kit/tools/macro/Play-Macro.ps1"
$macro = Join-Path $PSScriptRoot "macros/$Action.macro"
if ($DryRun) {
    if ($Action -eq 'capture') { 'Dry run: capture exact observed game HWND, no focus changes'; return }
    $flags = if ($Action -eq 'probe_camera') { @{ CommandFile = 'dbce-camera-cmd.txt' } } else { @{ RequireForeground = $true; ObserveForegroundWindow = 123 } }
    & $play -Macro $macro -Game gravel -DryRun @flags
    return
}
if (!$ProcessId -or !$StartedUtcTicks -or !$Window -or !$GameDirectory -or !$Evidence) { throw 'Live step needs exact PID, birth ticks, observed HWND, game directory and fresh evidence path.' }
. "$kit/tools/playback/Stage-RigLease.ps1"
. "$kit/tools/playback/Stage-ProcessIdentity.ps1"
$lease = Get-Content -LiteralPath $LeaseFile -Raw | ConvertFrom-Json
Assert-StageRigLease -Path $lease.path -Token $lease.token
$expectedExe = [IO.Path]::GetFullPath((Join-Path $GameDirectory 'gravel-Win64-Shipping.exe'))
$p = Get-Process -Id $ProcessId
if ($p.StartTime.ToUniversalTime().Ticks -ne $StartedUtcTicks -or $p.Path -ine $expectedExe) { throw 'Game identity mismatch; nothing sent.' }
if (Test-Path -LiteralPath $Evidence) { throw 'Use a fresh output path per observed step.' }
[IO.Directory]::CreateDirectory($Evidence) | Out-Null
if ($Action -ne 'capture') {
    $flags = if ($Action -eq 'probe_camera') { @{ CommandFile = (Join-Path $GameDirectory 'dbce-camera-cmd.txt') } } else { @{ RequireForeground = $true; ObserveForegroundWindow = $Window } }
    & pwsh -NoProfile -File $play -Macro $macro -Game gravel -ProcessId $ProcessId -LeasePath $lease.path -LeaseToken $lease.token @flags 2>&1 | Tee-Object -FilePath "$Evidence/macro.txt"
    if ($LASTEXITCODE) { throw 'Step refused or failed; inspect evidence, do not advance.' }
}
$p = Get-Process -Id $ProcessId
if ($p.StartTime.ToUniversalTime().Ticks -ne $StartedUtcTicks -or $p.Path -ine $expectedExe) { throw 'Game identity changed after step.' }
Assert-StageRigLease -Path $lease.path -Token $lease.token
$capture = "$kit/tools/window-capture/bin/Release/net8.0-windows10.0.22621.0/dbce-window-capture.exe"
& $capture $ProcessId --list | Set-Content "$Evidence/windows.json"
if ($LASTEXITCODE) { throw 'Window listing failed.' }
& $capture $ProcessId "$Evidence/frame.png" --hwnd "0x$('{0:X}' -f $Window)" --width 1920
if ($LASTEXITCODE) { throw 'Frame capture failed; do not advance.' }
if ($Action -eq 'probe_camera') { Copy-Item -LiteralPath (Join-Path $GameDirectory 'milestone_mod.log') -Destination "$Evidence/mod-log.txt" }
