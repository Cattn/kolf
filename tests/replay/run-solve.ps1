# SPDX-License-Identifier: GPL-2.0-or-later
# Checks that every hole of the given shipped courses can be holed within par + 2 shots
# by the greedy solver (see tests/replay/README.md).
param(
    [string]$Kolf = 'C:\CraftRoot\bin\kolf.exe',
    [string[]]$Course = @('WeatherReport', 'DangerZone', 'ToyBox')
)
$ErrorActionPreference = 'Continue'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$env:QT_QPA_PLATFORM = 'offscreen'
$failed = $false
foreach ($name in $Course) {
    $env:KOLF_SOLVE_COURSE = Join-Path $repo "courses\$name"
    & $Kolf 2>&1 | ForEach-Object { "$_" } | Where-Object { $_ -match '^solve:' } | ForEach-Object { "$name $_" }
    if ($LASTEXITCODE -ne 0) { $failed = $true }
}
Remove-Item env:KOLF_SOLVE_COURSE -ErrorAction SilentlyContinue
if ($failed) { exit 1 }
