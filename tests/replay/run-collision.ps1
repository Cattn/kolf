# SPDX-License-Identifier: GPL-2.0-or-later
# Ball-to-ball checks: a struck ball moves along the contact normal and keeps its
# momentum, and a ball pushed into the cup is holed without needing a shot.
param([string]$Kolf = 'C:\CraftRoot\bin\kolf.exe')
$ErrorActionPreference = 'Continue'
$fixtures = Join-Path $PSScriptRoot 'fixtures'
$env:QT_QPA_PLATFORM = 'offscreen'
$env:KOLF_COLLISION_PROBE = Join-Path $fixtures 'ball-collision.kolf'
$env:KOLF_COLLISION_POWER = '0.4'
& $Kolf 2>&1 | ForEach-Object { "$_" } | Where-Object { $_ -match '^collision:' }
$code = $LASTEXITCODE
$env:KOLF_COLLISION_POWER = '0.3'
$env:KOLF_COLLISION_CUP_COURSE = Join-Path $fixtures 'ball-into-cup.kolf'
& $Kolf 2>&1 | ForEach-Object { "$_" } | Where-Object { $_ -match '^collision:' }
if ($LASTEXITCODE -ne 0) { $code = $LASTEXITCODE }
Remove-Item env:KOLF_COLLISION_PROBE, env:KOLF_COLLISION_POWER, env:KOLF_COLLISION_CUP_COURSE
exit $code
