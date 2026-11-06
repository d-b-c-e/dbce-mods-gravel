param(
    [string]$Compiler = 'E:\msys64\mingw64\bin\g++.exe',
    [string]$SourcePath,
    [Parameter(Mandatory)][string]$OutputRoot
)
# Compiles the actual camera-probe block of src/ue4.cpp into a console fixture over synthetic reflection memory:
# controller/manager/target pointer walk, exact-one typed and bounded members, weak serial handling, nonfinite values,
# output caps and one terminal status line on every path. No game process or native runtime is involved.
$ErrorActionPreference = 'Stop'
$root = Split-Path (Split-Path $PSScriptRoot)
if (!$SourcePath) { $SourcePath = Join-Path $root 'src/ue4.cpp' }
$source = Get-Content -LiteralPath $SourcePath -Raw
$block = [regex]::Matches($source, '(?s)// --- camera probe ---.*?// --- end camera probe ---')
if ($block.Count -ne 1) { throw 'Actual camera-probe block not found uniquely' }
$out = [IO.Path]::GetFullPath($OutputRoot)
$allowed = [IO.Path]::GetFullPath((Join-Path $root 'build'))+'\'
if (!$out.StartsWith($allowed,[StringComparison]::OrdinalIgnoreCase) -or (Test-Path -LiteralPath $out)) { throw 'Fresh fixture directory under this worktree build required' }
[void][IO.Directory]::CreateDirectory($out)
$fixture = (Get-Content -LiteralPath (Join-Path $PSScriptRoot 'NativeCameraProbe.cpp') -Raw).Replace('/* PROBE_BLOCK */',$block[0].Value)
$cpp = Join-Path $out 'camera-probe.cpp'; $exe = Join-Path $out 'camera-probe.exe'
[IO.File]::WriteAllText($cpp,$fixture)
& $Compiler -std=c++17 -Wall -Wextra -Werror -static -o $exe $cpp
if ($LASTEXITCODE) { throw 'Camera probe fixture compilation failed' }
$lines = @(& $exe); $result = $LASTEXITCODE
$lines | Tee-Object -FilePath (Join-Path $out 'result.log')
[ordered]@{sourceSha256=(Get-FileHash -LiteralPath $SourcePath).Hash;exitCode=$result;nativeRuntime=$false} |
    ConvertTo-Json | Set-Content (Join-Path $out 'result.json')
if ($result) { throw 'Camera probe regression failed' }
