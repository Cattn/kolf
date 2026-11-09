# SPDX-License-Identifier: GPL-2.0-or-later
# Checks that every hole of the given shipped courses can be holed within par + 2 shots
# by the greedy solver (see tests/replay/README.md). Courses are solved in parallel.
param(
    [string]$Kolf = 'C:\CraftRoot\bin\kolf.exe',
    [string[]]$Course = @('WeatherReport', 'DangerZone', 'ToyBox')
)
$ErrorActionPreference = 'Continue'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$runs = foreach ($name in $Course) {
    $info = New-Object System.Diagnostics.ProcessStartInfo $Kolf
    $info.UseShellExecute = $false
    $info.RedirectStandardOutput = $true
    $info.RedirectStandardError = $true
    $info.Environment['QT_QPA_PLATFORM'] = 'offscreen'
    $info.Environment['KOLF_SOLVE_COURSE'] = Join-Path $repo "courses\$name"
    $process = [System.Diagnostics.Process]::Start($info)
    [pscustomobject]@{ Name = $name; Process = $process; Output = $process.StandardOutput.ReadToEndAsync(); Errors = $process.StandardError.ReadToEndAsync() }
}
$failed = $false
foreach ($run in $runs) {
    $run.Process.WaitForExit()
    ($run.Output.Result + $run.Errors.Result) -split "`r?`n" | Where-Object { $_ -match '^solve:' } | ForEach-Object { "$($run.Name) $_" }
    if ($run.Process.ExitCode -ne 0) { $failed = $true }
}
if ($failed) { exit 1 }
