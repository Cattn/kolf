# SPDX-License-Identifier: GPL-2.0-or-later
# Runs Kolf's checks. -Tier quick: unit tests, golden replays, save round trip and
# server tests. -Tier full: quick plus the native online scenarios and the UI scripts.
# See tests/README.md for what each check covers.
param(
    [ValidateSet('quick', 'full')][string]$Tier = 'quick',
    [string]$Kolf = 'C:\CraftRoot\bin\kolf.exe',
    [string]$CraftRoot = 'C:\CraftRoot',
    [string]$PythonPath = $env:CRAFT_PYTHON,
    [string]$BuildDirectory,
    [string[]]$Only = @()
)
$ErrorActionPreference = 'Stop'
$Only = @($Only | ForEach-Object { $_ -split ',' } | ForEach-Object { $_.Trim() } | Where-Object { $_ })
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
if (-not $BuildDirectory) { $BuildDirectory = Join-Path $repo '..\build' }

$nativeScenarios = @(
    'host-combined', 'host-undo-dynamics', 'host-undo-channels', 'four-player-hazard',
    'custom-course', 'shipped-download', 'slope-out-of-bounds', 'slope-in-bounds',
    'host-proposal-reject', 'host-proposal-four-player',
    'match-resume', 'host-resume', 'guest-leaves'
)

Get-ChildItem env: | Where-Object { [string]::IsNullOrEmpty($_.Value) } | ForEach-Object { Remove-Item "env:$($_.Name)" -ErrorAction SilentlyContinue }
$env:PATH = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer;" + (($env:PATH -split ';' | Where-Object { $_ -and $_ -notmatch 'scoop\\shims' }) -join ';')
if ($PythonPath) { $env:CRAFT_PYTHON = $PythonPath }
$ErrorActionPreference = 'Continue'
& (Join-Path $CraftRoot 'craft\craftenv.ps1') 2>$null | Out-Null
Set-Location $repo
$env:QT_QPA_FONTDIR = "$env:WINDIR\Fonts"

$results = [System.Collections.Generic.List[object]]::new()
function Invoke-Check([string]$Name, [scriptblock]$Command) {
    if ($Only.Count -and -not ($Only | Where-Object { $Name -like $_ })) { return }
    $watch = [Diagnostics.Stopwatch]::StartNew()
    $output = & $Command 2>&1 | Out-String
    $passed = $LASTEXITCODE -eq 0
    $watch.Stop()
    $results.Add([pscustomobject]@{ Check = $Name; Result = $(if ($passed) { 'pass' } else { 'FAIL' }); Seconds = [math]::Round($watch.Elapsed.TotalSeconds, 1) })
    "{0,-34} {1,-5} {2,6:N1}s" -f $Name, $(if ($passed) { 'pass' } else { 'FAIL' }), $watch.Elapsed.TotalSeconds
    if (-not $passed) { $output.TrimEnd() -split "`n" | Select-Object -Last 15 | ForEach-Object { "    $_" } }
}
function Stop-TestRelay {
    $listener = Get-NetTCPConnection -LocalPort 13111 -State Listen -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($listener) { Stop-Process -Id $listener.OwningProcess -Force -ErrorAction SilentlyContinue }
}

Invoke-Check 'unit tests (ctest)' { ctest --test-dir $BuildDirectory --output-on-failure }
Invoke-Check 'golden replays' { powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $repo 'tests\replay\run-replay.ps1') -Kolf $Kolf }
Invoke-Check 'ball collisions' { powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $repo 'tests\replay\run-collision.ps1') -Kolf $Kolf }
Invoke-Check 'save round trip + defaults' { powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $repo 'tests\replay\run-resave.ps1') -Kolf $Kolf }
Invoke-Check 'server tests' { Push-Location (Join-Path $repo 'server'); try { npm test } finally { Pop-Location } }

if ($Tier -eq 'full') {
    foreach ($scenario in $nativeScenarios) {
        Stop-TestRelay
        Invoke-Check "online: $scenario" { node (Join-Path $repo 'tests\multiplayer\run-online-native.ts') $scenario }
    }
    foreach ($uiScript in Get-ChildItem (Join-Path $repo 'tests\ui') -Filter *.json | Sort-Object Name) {
        $needsServer = (Get-Content $uiScript.FullName -Raw | ConvertFrom-Json).PSObject.Properties.Name -contains 'server'
        $server = $null
        if ($needsServer) {
            Stop-TestRelay
            $server = Start-Process node -ArgumentList @((Join-Path $repo 'tests\ui\ui-server.ts'), $uiScript.FullName) -PassThru -WindowStyle Hidden
            for ($i = 0; $i -lt 40 -and -not (Get-NetTCPConnection -LocalPort 13111 -State Listen -ErrorAction SilentlyContinue); ++$i) { Start-Sleep -Milliseconds 250 }
        }
        try {
            Invoke-Check "ui: $($uiScript.BaseName)" {
                powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $repo 'tests\ui\run-ui-script.ps1') -Script $uiScript.FullName -PythonPath $env:CRAFT_PYTHON
            }
        } finally {
            if ($server) { Stop-Process -Id $server.Id -Force -ErrorAction SilentlyContinue }
        }
    }
}

$failed = @($results | Where-Object Result -eq 'FAIL')
$total = ($results | Measure-Object Seconds -Sum).Sum
""
"{0} checks, {1} failed, {2:N0} s" -f $results.Count, $failed.Count, $total
if ($failed.Count) { exit 1 }
