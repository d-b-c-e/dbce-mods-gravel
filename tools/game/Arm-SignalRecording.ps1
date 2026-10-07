<#
.SYNOPSIS
Arms one developer signal capture without launching, installing or changing settings.
#>
param(
    [Parameter(Mandatory=$true)][string]$GameDirectory,
    [ValidateSet('Arm','Cancel')][string]$Action = 'Arm',
    [ValidateRange(1,600)][int]$Seconds = 60,
    [string]$ExpectedModSha256,
    [string]$Id
)
$ErrorActionPreference = 'Stop'
$game = (Resolve-Path -LiteralPath $GameDirectory).ProviderPath
foreach ($name in 'gravel-Win64-Shipping.exe','dinput8.dll') {
    if (-not [IO.File]::Exists((Join-Path $game $name))) { throw "Missing $name in game directory." }
}
if (Get-Process -Name 'gravel-Win64-Shipping' -ErrorAction SilentlyContinue) { throw 'Close Gravel before arming or cancelling a recording.' }
$request = Join-Path $game 'milestone_recording.request.ini'
$captureRoot = Join-Path $env:LOCALAPPDATA 'Dbce\StagePlayback\Gravel'
if ($Action -eq 'Cancel') {
    if ($Id -cnotmatch '^[a-f0-9]{32}$') { throw 'Cancel requires the exact capture Id.' }
    if (-not [IO.File]::Exists($request)) { throw 'No pending request. A claimed capture remains muted until the game exits.' }
    $content = [IO.File]::ReadAllText($request)
    if ($content -cnotmatch ('(?m)^id=' + $Id + '\r?$')) { throw 'Request belongs to a different capture; not changed.' }
    $cancelled = Join-Path (Join-Path $captureRoot $Id) 'cancelled-request.ini'
    [IO.File]::Copy($request, $cancelled, $false)
    [IO.File]::Delete($request)
    Write-Output "Cancelled $Id; request archived in $cancelled"
    return
}
if ($ExpectedModSha256 -notmatch '^[a-fA-F0-9]{64}$') { throw 'Specify the SHA256 of the reviewed candidate DLL with -ExpectedModSha256.' }
$modHash = (Get-FileHash -LiteralPath (Join-Path $game 'dinput8.dll') -Algorithm SHA256).Hash
if ($modHash -ine $ExpectedModSha256) { throw 'Installed DLL differs from the requested candidate; nothing armed.' }
if ([IO.File]::Exists($request)) { throw 'A request already exists. Inspect/cancel its exact Id before arming another.' }
$captureId = [guid]::NewGuid().ToString('N')
$result = Join-Path $captureRoot $captureId
[IO.Directory]::CreateDirectory($result) | Out-Null
$expires = [DateTime]::UtcNow.AddMinutes(5)
$text = "[session]`nid=$captureId`nexpiresFileTimeUtc=$($expires.ToFileTimeUtc())`nseconds=$Seconds`n"
$receipt = [ordered]@{
    schema='dbce.gravel.signal-request'; version=1; id=$captureId; utc=[DateTime]::UtcNow.ToString('o'); expiresUtc=$expires.ToString('o')
    gameDirectory=$game; exeSha256=(Get-FileHash -LiteralPath (Join-Path $game 'gravel-Win64-Shipping.exe')).Hash
    modSha256=$modHash; seconds=$Seconds; kind='sampled-signals'; trajectoryPlayback=$false; physicalForceQualification=$false
}
$ini = Join-Path $game 'milestone_mod.ini'
if ([IO.File]::Exists($ini)) {
    $receipt['configSha256']=(Get-FileHash -LiteralPath $ini).Hash
    [IO.File]::Copy($ini,(Join-Path $result 'owner-milestone_mod.ini'),$false)
}
[IO.File]::WriteAllText((Join-Path $result 'request.json'),($receipt | ConvertTo-Json -Depth 5),[Text.UTF8Encoding]::new($false))
# Publish only a complete file, using an atomic same-directory move. Existing
# requests are never replaced, even if another agent arms between the checks.
$pending = $request + '.' + $captureId + '.tmp'
$stream = [IO.File]::Open($pending,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
try { $bytes=[Text.Encoding]::ASCII.GetBytes($text); $stream.Write($bytes,0,$bytes.Length); $stream.Flush() } finally { $stream.Dispose() }
[IO.File]::Move($pending,$request)
[pscustomobject]@{ Id=$captureId; Result=$result; Request=$request; ExpiresUtc=$expires.ToString('o'); Notice='One launch, outputs muted until exit; normal game close required. No route playback.' }
