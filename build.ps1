param([string]$Toolchain = 'E:\msys64\mingw64\bin')
# Builds the x64 proxy and the input helper into build\. tools\Package.ps1 packages them from there.
$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$compiler = Join-Path $Toolchain 'g++.exe'
$inspect = Join-Path $Toolchain 'objdump.exe'
if (-not (Test-Path -LiteralPath $compiler)) { throw 'MinGW-w64 g++ was not found. Pass -Toolchain with its bin folder.' }
[IO.Directory]::CreateDirectory((Join-Path $root 'build')) | Out-Null
$previousPath = $env:PATH
$env:PATH = "$Toolchain;$previousPath"
Push-Location $root
try {
    & $compiler -std=c++17 -O2 -s -shared -static -static-libgcc -static-libstdc++ -Wall -Wno-unused-function -Wno-stringop-truncation -Ilib/toolkit/include -o build/dinput8.dll src/proxy.cpp src/fmod_tap.cpp src/ue4.cpp src/telemetry.cpp src/recording.cpp src/triple.cpp src/dinput8.def -ldxguid -luuid -lole32 -lws2_32 -lgdi32
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
    Get-FileHash (Join-Path $root 'build\dinput8.dll'),(Join-Path $root 'build\wheelprobe.exe')
} finally { Pop-Location; $env:PATH = $previousPath }
