param([string]$Compiler = 'E:\msys64\mingw64\bin\g++.exe')
$ErrorActionPreference = 'Stop'
$root = Split-Path (Split-Path $PSScriptRoot)
$out = Join-Path $root 'build/ini-settings-tests'
[IO.Directory]::CreateDirectory($out) | Out-Null
& $Compiler -std=c++17 -Wall -Wextra -Werror -static -o (Join-Path $out 'ini.exe') (Join-Path $PSScriptRoot 'NativeIniSettings.cpp')
if ($LASTEXITCODE) { throw 'INI fixture build failed.' }
& (Join-Path $out 'ini.exe') $out
if ($LASTEXITCODE) { throw 'INI regression failed.' }
