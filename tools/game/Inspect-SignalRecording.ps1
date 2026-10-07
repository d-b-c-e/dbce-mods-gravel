param([Parameter(Mandatory=$true)][string]$Result)
$ErrorActionPreference='Stop'
$root = Split-Path (Split-Path $PSScriptRoot)
$resultPath = (Resolve-Path -LiteralPath $Result).ProviderPath
$receipt = Get-Content -LiteralPath (Join-Path $resultPath 'request.json') -Raw | ConvertFrom-Json
if ($receipt.schema -ne 'dbce.gravel.signal-request' -or $receipt.id -cnotmatch '^[a-f0-9]{32}$') { throw 'Unknown request receipt.' }
# File/producer validation runs without the game or any device. This is signal
# inspection, not launching playback. The managed reader validates the footer.
$project = Join-Path $root 'tools/Gravel.Diagnostics/Gravel.Diagnostics.csproj'
& dotnet build $project -c Release --nologo --verbosity quiet
if ($LASTEXITCODE) { throw 'Diagnostics build failed.' }
$report = & dotnet (Join-Path $root 'tools/Gravel.Diagnostics/bin/Release/net8.0/Gravel.Diagnostics.dll') inspect (Join-Path $resultPath 'signals.jsonl') $receipt.id
if ($LASTEXITCODE) { throw 'Signal recording validation failed.' }
$parsed = ($report -join "`n") | ConvertFrom-Json
$parsed | Add-Member -NotePropertyName signalsSha256 -NotePropertyValue (Get-FileHash -LiteralPath (Join-Path $resultPath 'signals.jsonl')).Hash
$parsed | ConvertTo-Json -Depth 6
