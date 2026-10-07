param([string]$Compiler='E:\msys64\mingw64\bin\g++.exe')
$ErrorActionPreference='Stop'
$root = Split-Path (Split-Path $PSScriptRoot)
$pin = Get-Content -LiteralPath (Join-Path $root 'lib/session/provenance.json') -Raw | ConvertFrom-Json
foreach($file in $pin.files) {
    if ((Get-FileHash -LiteralPath (Join-Path $root $file.path)).Hash -ine $file.sha256) { throw "Session pin changed: $($file.path)" }
}
$out = Join-Path $root 'build/native-capture-tests'
[IO.Directory]::CreateDirectory($out) | Out-Null
& $Compiler -std=c++17 -Wall -Wno-unused-function -static ('-I'+(Join-Path $root 'lib/toolkit/include')) -o (Join-Path $out 'capture.exe') (Join-Path $PSScriptRoot 'NativeCapture.cpp') -ldxguid -luuid -lole32
if ($LASTEXITCODE) { throw 'Capture fake-COM build failed.' }
& (Join-Path $out 'capture.exe')
if ($LASTEXITCODE) { throw 'Capture fake-COM checks failed.' }
& $Compiler -std=c++17 -Wall -Wno-unused-function -static ('-I'+(Join-Path $root 'lib/toolkit/include')) -o (Join-Path $out 'abi.exe') (Join-Path $PSScriptRoot 'NativeAbi.cpp') -ldxguid -luuid -lole32
if ($LASTEXITCODE) { throw 'SDK ABI fixture build failed.' }
& (Join-Path $out 'abi.exe')
if ($LASTEXITCODE) { throw 'SDK ABI checks failed.' }
& $Compiler -std=c++17 -DABI_ANSI -Wall -Wno-unused-function -static ('-I'+(Join-Path $root 'lib/toolkit/include')) -o (Join-Path $out 'abi-ansi.exe') (Join-Path $PSScriptRoot 'NativeAbi.cpp') -ldxguid -luuid -lole32
if ($LASTEXITCODE) { throw 'ANSI SDK ABI fixture build failed.' }
& (Join-Path $out 'abi-ansi.exe')
if ($LASTEXITCODE) { throw 'ANSI SDK ABI checks failed.' }
& $Compiler -std=c++17 -Wall -Wno-unused-function -static ('-I'+(Join-Path $root 'lib/toolkit/include')) -o (Join-Path $out 'recording.exe') (Join-Path $PSScriptRoot 'NativeRecording.cpp')
if ($LASTEXITCODE) { throw 'Native recording fixture build failed.' }
$result = Join-Path ([IO.Path]::GetTempPath()) ('gravel-recording-fixture-'+[guid]::NewGuid().ToString('N'))
foreach($mode in 'none','expired','valid') {
    & (Join-Path $out 'recording.exe') $mode (Join-Path $result $mode)
    if ($LASTEXITCODE) { throw "Recording fixture failed: $mode" }
}
$signals=Join-Path $result 'valid/Dbce/StagePlayback/Gravel/abcdef0123456789abcdef0123456789/signals.jsonl'
& dotnet run --project (Join-Path $root 'tools/Gravel.Diagnostics/Gravel.Diagnostics.csproj') -- inspect $signals 'abcdef0123456789abcdef0123456789'
if ($LASTEXITCODE) { throw 'Actual native producer failed managed inspection.' }
$rows=@(Get-Content -LiteralPath $signals | ForEach-Object { $_ | ConvertFrom-Json } | Where-Object kind -eq 'sample')
if ($rows.Count -ne 3 -or $rows[0].sample.channels.'ffb.observed' -ne 0 -or $rows[0].sample.channels.PSObject.Properties.Name -contains 'ffb.constantSummary') { throw 'Unobserved force was invented.' }
if ($rows[1].sample.channels.'ue.rpm' -ne 0 -or $rows[1].sample.channels.'fmod.rpm' -ne 0 -or [Math]::Abs($rows[1].sample.channels.'ffb.constantSummary'-.61) -gt .00001) { throw 'Known zero/original force observation lost.' }
Write-Host "PASS producer/managed reader; evidence $result"
