$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot '../game/QualificationState.psm1') -Force
$fixture = Join-Path (Split-Path (Split-Path $PSScriptRoot)) ('build/state-tests/' + [guid]::NewGuid().ToString('N'))
$root = Join-Path $fixture 'owner'; [IO.Directory]::CreateDirectory($root) | Out-Null
[IO.File]::WriteAllBytes("$root/a", [byte[]]@(0, 1, 255))
[IO.File]::WriteAllText("$root/b", "kept`r`n")
Save-GravelQualificationState @{ owner = $root } "$fixture/evidence"
[IO.File]::WriteAllText("$root/a", 'changed'); Remove-Item -LiteralPath "$root/b"
[IO.File]::WriteAllText("$root/new", 'new data')
Restore-GravelQualificationState "$fixture/evidence"
if ([Convert]::ToHexString([IO.File]::ReadAllBytes("$root/a")) -ne '0001FF' -or [IO.File]::ReadAllText("$root/b") -cne "kept`r`n" -or (Test-Path "$root/new")) { throw 'Byte/file-set restore failed.' }
if ([IO.File]::ReadAllText("$fixture/evidence/after/owner/new") -cne 'new data') { throw 'New data was not archived.' }
[IO.File]::WriteAllText("$fixture/evidence/before/owner/a", 'tampered')
[IO.File]::WriteAllText("$root/a", 'must remain')
$refused = $false; try { Restore-GravelQualificationState "$fixture/evidence" } catch { $refused = $_ -match 'backup mismatch' }
if (!$refused -or [IO.File]::ReadAllText("$root/a") -ne 'must remain') { throw 'Tampered backup was not refused before mutation.' }
'PASS: changed/deleted/new files restored, post-run data retained, corrupt backup refused before mutation'
