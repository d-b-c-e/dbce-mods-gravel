# File-only state preservation for Gravel diagnostic runs. Caller holds the rig lease for any live mutation.
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
function Get-GravelTree([string]$Root) {
    $result = @{}
    if (Test-Path -LiteralPath $Root) {
        foreach ($item in Get-ChildItem -LiteralPath $Root -Force -Recurse) {
            if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Linked state is unsupported: $($item.FullName)" }
            if (!$item.PSIsContainer) { $result[$item.FullName.Substring($Root.Length + 1)] = (Get-FileHash -LiteralPath $item.FullName).Hash }
        }
    }
    return ,$result
}
function Save-GravelQualificationState([System.Collections.IDictionary]$Roots, [string]$Evidence) {
    if (Test-Path -LiteralPath $Evidence) { throw 'Use a fresh evidence folder.' }
    [IO.Directory]::CreateDirectory($Evidence) | Out-Null
    $manifest = [ordered]@{}
    foreach ($name in $Roots.Keys) {
        if ($name -notmatch '^[a-z0-9_-]+$') { throw 'Invalid state label.' }
        $root = [IO.Path]::GetFullPath($Roots[$name]).TrimEnd('\')
        if ((Test-Path -LiteralPath $root) -and ((Get-Item -LiteralPath $root).Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw 'State root is linked.' }
        if ($Evidence.StartsWith($root + '\', [StringComparison]::OrdinalIgnoreCase)) { throw 'Evidence must be outside the state root.' }
        $files = Get-GravelTree $root
        foreach ($relative in $files.Keys) {
            $to = Join-Path "$Evidence/before/$name" $relative
            [IO.Directory]::CreateDirectory((Split-Path $to)) | Out-Null
            Copy-Item -LiteralPath (Join-Path $root $relative) -Destination $to
            if ((Get-FileHash -LiteralPath $to).Hash -ne $files[$relative]) { throw 'Snapshot copy did not verify.' }
        }
        $check = Get-GravelTree $root
        if ($check.Count -ne $files.Count) { throw 'State changed while snapshotting.' }
        foreach ($relative in $files.Keys) { if (!$check.ContainsKey($relative) -or $check[$relative] -ne $files[$relative]) { throw 'State changed while snapshotting.' } }
        $manifest[$name] = @{ root = $root; files = $files }
    }
    $manifest | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath "$Evidence/state.json" -Encoding utf8
}
function Restore-GravelQualificationState([string]$Evidence) {
    if (Get-Process -Name 'gravel-Win64-Shipping' -ErrorAction SilentlyContinue) { throw 'Close Gravel gracefully before restoring.' }
    $manifest = Get-Content -LiteralPath "$Evidence/state.json" -Raw | ConvertFrom-Json -AsHashtable
    $after = if (Test-Path -LiteralPath "$Evidence/after") { "after-$([guid]::NewGuid().ToString('N'))" } else { 'after' }
    # Verify every retained backup before touching any owner file.
    foreach ($name in $manifest.Keys) {
        foreach ($relative in $manifest[$name].files.Keys) {
            $backup = Join-Path "$Evidence/before/$name" $relative
            if ((Get-FileHash -LiteralPath $backup).Hash -ne $manifest[$name].files[$relative]) { throw 'Retained backup mismatch; nothing restored.' }
        }
    }
    foreach ($name in $manifest.Keys) {
        $entry = $manifest[$name]; $root = [IO.Path]::GetFullPath($entry.root).TrimEnd('\')
        if ((Test-Path -LiteralPath $root) -and ((Get-Item -LiteralPath $root).Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw 'State root became linked.' }
        $now = Get-GravelTree $root
        foreach ($relative in $now.Keys) {
            $from = Join-Path $root $relative
            $archive = Join-Path "$Evidence/$after/$name" $relative
            [IO.Directory]::CreateDirectory((Split-Path $archive)) | Out-Null
            Copy-Item -LiteralPath $from -Destination $archive -Force
            if ((Get-FileHash -LiteralPath $archive).Hash -ne $now[$relative]) { throw 'Post-run archive did not verify.' }
            if (!$entry.files.ContainsKey($relative)) {
                $resolved = [IO.Path]::GetFullPath($from)
                if (!$resolved.StartsWith($root + '\', [StringComparison]::OrdinalIgnoreCase)) { throw 'Removal escaped the explicit state root.' }
                Remove-Item -LiteralPath $resolved
            }
        }
        foreach ($relative in $entry.files.Keys) {
            $to = Join-Path $root $relative
            [IO.Directory]::CreateDirectory((Split-Path $to)) | Out-Null
            Copy-Item -LiteralPath (Join-Path "$Evidence/before/$name" $relative) -Destination $to -Force
        }
        $restored = Get-GravelTree $root
        if ($restored.Count -ne $entry.files.Count) { throw 'Restored file set differs; retain custody.' }
        foreach ($relative in $entry.files.Keys) {
            if (!$restored.ContainsKey($relative) -or $restored[$relative] -ne $entry.files[$relative]) { throw 'Restored bytes differ; retain custody.' }
        }
    }
    'PASS: every original file restored byte-exact; new files archived and removed'
}
Export-ModuleMember -Function Save-GravelQualificationState, Restore-GravelQualificationState
