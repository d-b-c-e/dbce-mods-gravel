param([string]$Toolchain = 'E:\msys64\mingw64\bin', [switch]$UpdateDist)
$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
Import-Module (Join-Path $root 'tools\BinaryProvenance.psm1') -Force
$compiler = Join-Path $Toolchain 'g++.exe'
$inspect = Join-Path $Toolchain 'objdump.exe'
if (-not (Test-Path -LiteralPath $compiler)) { throw 'MinGW-w64 g++ was not found. Pass -Toolchain with its bin folder.' }
$sourceCommit = (& git -C $root rev-parse HEAD).Trim()
if ($LASTEXITCODE) { throw 'Build source commit could not be read.' }
$sourceState = @(& git -C $root status --porcelain --untracked-files=all)
if ($LASTEXITCODE) { throw 'Build source state could not be read.' }
$inputPaths = @(& git -C $root ls-files -- src tools/wheelprobe lib/toolkit/include build.ps1 tools/BinaryProvenance.psm1)
if ($LASTEXITCODE) { throw 'Build inputs could not be enumerated.' }
$inputHashes = @($inputPaths | ForEach-Object { [pscustomobject]@{Path=$_;SHA256=(Get-ProvenanceHash (Join-Path $root $_))} })
$compilerVersion = (& $compiler --version | Out-String).Trim()
if ($LASTEXITCODE) { throw 'Compiler version could not be read.' }
$inspectVersion = (& $inspect --version | Out-String).Trim()
if ($LASTEXITCODE) { throw 'Inspector version could not be read.' }
$compilerIdentity = @{Version=$compilerVersion;SHA256=(Get-ProvenanceHash $compiler)}
$inspectorIdentity = @{Version=$inspectVersion;SHA256=(Get-ProvenanceHash $inspect)}
$build = Join-Path $root 'build'
[IO.Directory]::CreateDirectory($build) | Out-Null
$buildReceiptPath = Join-Path $build 'build-receipt.json'
if ([IO.File]::Exists($buildReceiptPath)) { Remove-Item -LiteralPath $buildReceiptPath }
$previousPath = $env:PATH
$env:PATH = "$Toolchain;$previousPath"
Push-Location $root
try {
    & $compiler -std=c++17 -O2 -s -shared -static -static-libgcc -static-libstdc++ -Wall -Wno-unused-function -Wno-stringop-truncation -Ilib/toolkit/include -o build/dinput8.dll src/proxy.cpp src/fmod_tap.cpp src/ue4.cpp src/telemetry.cpp src/triple.cpp src/dinput8.def -ldxguid -luuid -lole32 -lws2_32
    if ($LASTEXITCODE) { throw 'Proxy build failed.' }
    & $compiler -std=c++17 -O2 -s -static -static-libgcc -static-libstdc++ -Wall -o build/wheelprobe.exe tools/wheelprobe/wheelprobe.cpp -ldinput8 -ldxguid
    if ($LASTEXITCODE) { throw 'Input helper build failed.' }
    $exports = (& $inspect -p build/dinput8.dll | Out-String)
    if ($LASTEXITCODE) { throw 'Proxy inspection failed.' }
    foreach ($name in 'DirectInput8Create','DllCanUnloadNow','DllGetClassObject','DllRegisterServer','DllUnregisterServer','GetdfDIJoystick') {
        if ($exports -notmatch ('(?m)^\s*\[.*\]\s+(?:[0-9a-fA-F]+\s+)?' + $name + '\s*$')) { throw "Missing native export: $name" }
    }
    foreach ($binary in 'dinput8.dll','wheelprobe.exe') {
        $inspection = (& $inspect -p (Join-Path 'build' $binary) | Out-String)
        if ($LASTEXITCODE) { throw "$binary inspection failed." }
        if ($inspection -notmatch 'file format pei-x86-64') { throw "$binary is not x64." }
        if ($inspection -match '(?i)DLL Name:\s+(libgcc|libstdc|libwinpthread)') { throw "$binary has an unbundled compiler runtime dependency." }
    }
    if ((Get-ProvenanceHash $compiler) -cne $compilerIdentity.SHA256 -or (Get-ProvenanceHash $inspect) -cne $inspectorIdentity.SHA256) { throw 'Build tools changed during compilation; no receipt written.' }
    $finalSourceCommit = (& git -C $root rev-parse HEAD).Trim()
    if ($LASTEXITCODE -or $finalSourceCommit -cne $sourceCommit) { throw 'Build source commit changed; no receipt written.' }
    $finalSourceState = @(& git -C $root status --porcelain --untracked-files=all)
    if ($LASTEXITCODE -or ($finalSourceState -join "`n") -cne ($sourceState -join "`n")) { throw 'Build source state changed; no receipt written.' }
    Write-ValidatedBuildReceipt -Root $root -SourceCommit $sourceCommit -SourceDirty ($sourceState.Count -gt 0) -Compiler $compilerIdentity -Inspector $inspectorIdentity -InputHashes $inputHashes
    if ($UpdateDist) {
        foreach ($binary in 'dinput8.dll','wheelprobe.exe') { Copy-Item -LiteralPath (Join-Path $build $binary) -Destination (Join-Path $root "dist\$binary") -Force }
    }
    Get-FileHash (Join-Path $build 'dinput8.dll'),(Join-Path $build 'wheelprobe.exe')
} finally { Pop-Location; $env:PATH = $previousPath }
