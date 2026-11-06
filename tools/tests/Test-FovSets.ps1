param(
    [string]$Compiler = 'E:\msys64\mingw64\bin\g++.exe',
    [string]$SourcePath,
    [Parameter(Mandatory)][string]$OutputRoot
)
# Compiles the actual effective-FOV block of src/triple.cpp (rigVerticalFov, tunedFov, eyeFor, setLabelFor) into a
# console fixture: measured start, tune direction, 20..120 range and the notice number sharing one rule.
$ErrorActionPreference = 'Stop'
$root = Split-Path (Split-Path $PSScriptRoot)
if (!$SourcePath) { $SourcePath = Join-Path $root 'src/triple.cpp' }
$source = Get-Content -LiteralPath $SourcePath -Raw
$block = [regex]::Matches($source, '(?s)// --- effective FOV .*?// --- end effective FOV ---')
if ($block.Count -ne 1) { throw 'Actual effective-FOV block not found uniquely' }
$out = [IO.Path]::GetFullPath($OutputRoot)
$allowed = [IO.Path]::GetFullPath((Join-Path $root 'build'))+'\'
if (!$out.StartsWith($allowed,[StringComparison]::OrdinalIgnoreCase) -or (Test-Path -LiteralPath $out)) { throw 'Fresh fixture directory under this worktree build required' }
[void][IO.Directory]::CreateDirectory($out)
$fixture = (Get-Content -LiteralPath (Join-Path $PSScriptRoot 'NativeFovSets.cpp') -Raw).Replace('/* FOV_BLOCK */',$block[0].Value)
$cpp = Join-Path $out 'fov-sets.cpp'; $exe = Join-Path $out 'fov-sets.exe'
[IO.File]::WriteAllText($cpp,$fixture)
& $Compiler -std=c++17 -Wall -Wextra -Werror -static -o $exe $cpp
if ($LASTEXITCODE) { throw 'FOV set fixture compilation failed' }
$lines = @(& $exe); $result = $LASTEXITCODE
$lines | Tee-Object -FilePath (Join-Path $out 'result.log')
[ordered]@{sourceSha256=(Get-FileHash -LiteralPath $SourcePath).Hash;exitCode=$result;nativeRuntime=$false} |
    ConvertTo-Json | Set-Content (Join-Path $out 'result.json')
if ($result) { throw 'FOV set regression failed' }
