Set-StrictMode -Version Latest

function Assert-SourceValidationWorkflow([string]$Text) {
    # Closed workflow grammar: any additional job, command, permission, secret,
    # upload or action requires a separate policy review. No YAML dependency.
    $expected = @'
name: Gravel source validation

on:
  workflow_dispatch:

permissions:
  contents: read

jobs:
  synthetic-validation:
    runs-on: windows-2022
    timeout-minutes: 15
    strategy:
      fail-fast: false
      matrix:
        engine: [powershell, pwsh]
    steps:
      - name: Check out source
        uses: actions/checkout@11bd71901bbe5b1630ceea73d27597364c9af683 # v4.2.2
        with:
          persist-credentials: false
      - name: Validate synthetic source fixtures
        shell: pwsh
        run: |
          $ErrorActionPreference = 'Stop'
          & .\tools\Invoke-SourceValidation.ps1 -Engine '${{ matrix.engine }}'
          if (-not $?) { exit 1 }
'@
    if ($Text.Replace("`r`n","`n").TrimEnd("`n") -cne $expected.Replace("`r`n","`n").TrimEnd("`n")) {
        throw 'Workflow differs from the reviewed read-only synthetic-validation policy.'
    }
}

function Assert-SourcePackageSafety([string[]]$Paths) {
    if (@($Paths | Where-Object {
        $_ -match '(?i)\.(sav|rom|iso|bin|fzpt|inp|log)$|milestone_install\.json$|(?:^|[\\/])recordings(?:[\\/]|$)'
    }).Count) { throw 'Private/generated game assets must not enter source-validation packages.' }
}

Export-ModuleMember -Function Assert-SourceValidationWorkflow,Assert-SourcePackageSafety
