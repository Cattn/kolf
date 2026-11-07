# SPDX-License-Identifier: GPL-2.0-or-later
param(
    [string]$Kolf = 'C:\CraftRoot\bin\kolf.exe',
    [string]$Output = (Join-Path ([System.IO.Path]::GetTempPath()) 'kolf-resave')
)
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
Remove-Item $Output -Recurse -Force -ErrorAction SilentlyContinue
$env:QT_QPA_PLATFORM = 'offscreen'
$env:KOLF_RESAVE_COURSES = Join-Path $repo 'courses'
$env:KOLF_RESAVE_OUTPUT = $Output
$ErrorActionPreference = 'Continue'
& $Kolf 2>&1 | ForEach-Object { "$_" } | Where-Object { $_ -match '^(resave|defaults):' }
$code = $LASTEXITCODE
Remove-Item env:KOLF_RESAVE_COURSES, env:KOLF_RESAVE_OUTPUT
exit $code
