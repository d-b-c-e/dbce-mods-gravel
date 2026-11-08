param([string]$Compiler = 'E:\msys64\mingw64\bin\g++.exe')
# Pedal resting dead zone (src/pedal_deadzone.h, ISS-0039).
$ErrorActionPreference = 'Stop'
$root = Split-Path (Split-Path $PSScriptRoot)
$out = Join-Path $root 'build/pedal-deadzone-tests'
[IO.Directory]::CreateDirectory($out) | Out-Null
& $Compiler -std=c++17 -Wall -Wextra -Werror -static -o (Join-Path $out 'pedal.exe') (Join-Path $PSScriptRoot 'NativePedalDeadZone.cpp')
if ($LASTEXITCODE) { throw 'Pedal dead zone fixture build failed.' }
& (Join-Path $out 'pedal.exe')
if ($LASTEXITCODE) { throw 'Pedal dead zone regression failed.' }
