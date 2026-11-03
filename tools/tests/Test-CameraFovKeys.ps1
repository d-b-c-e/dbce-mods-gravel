param(
    [string]$Compiler = 'E:\msys64\mingw64\bin\g++.exe',
    [string]$SourcePath,
    [Parameter(Mandatory)][string]$OutputRoot,
    [switch]$ExpectFailure
)
$ErrorActionPreference = 'Stop'
$root = Split-Path (Split-Path $PSScriptRoot)
if (!$SourcePath) { $SourcePath = Join-Path $root 'src/triple.cpp' }
$source = Get-Content -LiteralPath $SourcePath -Raw
$state = [regex]::Matches($source, '(?m)^struct CameraOffsets [^\r\n]+')
$steps = [regex]::Matches($source, '(?m)^float g_moveStep = [^\r\n]+')
$dispatch = [regex]::Matches($source, '(?s)enum CamAction \{.*?(?=\r?\nvoid showNotice\(const char \*text\);)')
if ($state.Count -ne 1 -or $steps.Count -ne 1 -or $dispatch.Count -ne 1) { throw 'Actual camera production blocks not found uniquely' }
$out = [IO.Path]::GetFullPath($OutputRoot)
$allowed = [IO.Path]::GetFullPath((Join-Path $root 'build'))+'\'
if (!$out.StartsWith($allowed,[StringComparison]::OrdinalIgnoreCase) -or (Test-Path -LiteralPath $out)) { throw 'Fresh fixture directory under this worktree build required' }
[void][IO.Directory]::CreateDirectory($out)
$fixture = (Get-Content -LiteralPath (Join-Path $PSScriptRoot 'NativeCameraFovKeys.cpp') -Raw).
    Replace('/* CAMERA_STATE */',$state[0].Value).Replace('/* CAMERA_STEPS */',$steps[0].Value).Replace('/* CAMERA_DISPATCH */',$dispatch[0].Value)
$cpp = Join-Path $out 'camera-keys.cpp'; $exe = Join-Path $out 'camera-keys.exe'
[IO.File]::WriteAllText($cpp,$fixture)
& $Compiler -std=c++17 -Wall -Wextra -Werror -static -o $exe $cpp
if ($LASTEXITCODE) { throw 'Camera key fixture compilation failed' }
$lines = @(& $exe); $result = $LASTEXITCODE
$lines | Tee-Object -FilePath (Join-Path $out 'result.log')
[ordered]@{sourceSha256=(Get-FileHash -LiteralPath $SourcePath).Hash;exitCode=$result;expectedFailure=[bool]$ExpectFailure;nativeRuntime=$false} |
    ConvertTo-Json | Set-Content (Join-Path $out 'result.json')
if ($ExpectFailure) { if (!$result) { throw 'Old source unexpectedly passed STD-048 fixture' } }
elseif ($result) { throw 'Camera key STD-048 regression failed' }
