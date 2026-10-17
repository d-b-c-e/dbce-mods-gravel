param([string]$Compiler = 'E:\msys64\mingw64\bin\g++.exe')
$ErrorActionPreference = 'Stop'
$root = Split-Path (Split-Path $PSScriptRoot)
$source = Get-Content -LiteralPath (Join-Path $root 'src/triple.cpp') -Raw
# Compile the actual production decision and thread admission, with inert stand-ins.
# No game DLL is loaded and no window, thread, display or device API is called.
$setup = [regex]::Match($source, '(?s)    bool spanWindow = .*?\r?\n    g_commandLine = line;')
$start = [regex]::Matches($source, '(?m)^    if \(spanWindow\) CreateThread\([^\r\n]+;')
if (-not $setup.Success -or $start.Count -ne 1) { throw 'Triple span production blocks not found uniquely.' }
$template = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'NativeTripleSpan.cpp') -Raw
$fixture = $template.Replace('/* SPAN_SETUP */', $setup.Value).Replace('/* SPAN_START */', $start[0].Value)
$out = Join-Path $root 'build/triple-span-tests'
[IO.Directory]::CreateDirectory($out) | Out-Null
$cpp = Join-Path $out 'span.cpp'
[IO.File]::WriteAllText($cpp, $fixture)
& $Compiler -std=c++17 -Wall -Wextra -Werror -static -o (Join-Path $out 'span.exe') $cpp
if ($LASTEXITCODE) { throw 'Triple span fixture build failed.' }
& (Join-Path $out 'span.exe')
if ($LASTEXITCODE) { throw 'Triple span regression failed.' }
