# SPDX-License-Identifier: GPL-2.0-or-later
param(
    [string]$Kolf = 'C:\CraftRoot\bin\kolf.exe',
    [string[]]$Course = @(),
    [int]$Runs = 1,
    [switch]$Update,
    [string]$Output
)
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$golden = Join-Path $PSScriptRoot 'golden'
$courses = if ($Course.Count) { $Course | ForEach-Object { Join-Path $repo "courses\$_" } } else { @(Join-Path $repo 'courses') }
$env:QT_QPA_PLATFORM = 'offscreen'
$env:KOLF_REPLAY_GOLDEN = $golden
$env:KOLF_REPLAY_RUNS = "$Runs"
$env:KOLF_REPLAY_UPDATE = if ($Update) { '1' } else { '' }
$env:KOLF_REPLAY_OUTPUT = $Output
$failed = $false
foreach ($path in $courses) {
    $env:KOLF_REPLAY_COURSES = $path
    $ErrorActionPreference = 'Continue'
    & $Kolf 2>&1 | ForEach-Object { "$_" }
    $code = $LASTEXITCODE
    $ErrorActionPreference = 'Stop'
    if ($code -ne 0) { $failed = $true }
}
if ($failed) { exit 1 }
