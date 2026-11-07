# SPDX-License-Identifier: GPL-2.0-or-later
# Builds and packages Kolf for Windows with Craft, then writes the installer and its SHA-256
# into -OutputDirectory as kolf-<version>-windows-x86_64.<ext>.
# Craft builds from its own work folder, which must be this checkout at the commit to release.
param(
    [string]$OutputDirectory = (Join-Path $PSScriptRoot '..\release'),
    [string]$CraftRoot = 'C:\CraftRoot',
    [string]$PythonPath = $env:CRAFT_PYTHON,
    [string]$ExpectedVersion
)
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path

$cmake = Get-Content -Raw (Join-Path $repo 'CMakeLists.txt')
$base = [regex]::Match($cmake, 'set\(KOLF_BASE_VERSION "([^"]+)"\)').Groups[1].Value
$fork = [regex]::Match($cmake, 'set\(KOLF_FORK_RELEASE "([^"]+)"\)').Groups[1].Value
if (-not $base -or -not $fork) { throw 'KOLF_BASE_VERSION or KOLF_FORK_RELEASE is missing from CMakeLists.txt' }
$version = "$base-cattn.$fork"
if ($ExpectedVersion -and $ExpectedVersion -ne $version) { throw "CMakeLists.txt says $version, but $ExpectedVersion was requested" }
if (git -C $repo status --porcelain --untracked-files=no) { throw 'The checkout has uncommitted changes; commit or stash them first' }
"Releasing Kolf $version from $(git -C $repo rev-parse --short HEAD)"

Get-ChildItem env: | Where-Object { [string]::IsNullOrEmpty($_.Value) } | ForEach-Object { Remove-Item "env:$($_.Name)" -ErrorAction SilentlyContinue }
$env:PATH = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer;" + (($env:PATH -split ';' | Where-Object { $_ -and $_ -notmatch 'scoop\\shims' }) -join ';')
if ($PythonPath) { $env:CRAFT_PYTHON = $PythonPath }
$ErrorActionPreference = 'Continue'
& (Join-Path $CraftRoot 'craft\craftenv.ps1') 2>$null | Out-Null
$ErrorActionPreference = 'Stop'
if (-not (Get-Command craft -ErrorAction SilentlyContinue)) { throw 'Craft initialization failed' }

$started = Get-Date
craft --compile --install --qmerge kolf
if ($LASTEXITCODE -ne 0) { throw "craft build failed ($LASTEXITCODE)" }
craft --package kolf
if ($LASTEXITCODE -ne 0) { throw "craft --package failed ($LASTEXITCODE)" }

$packages = Get-ChildItem (Join-Path $CraftRoot 'tmp') -File -ErrorAction SilentlyContinue |
    Where-Object { $_.Name -like 'kolf-*' -and $_.Extension -in '.exe', '.7z', '.appx', '.msix' -and $_.LastWriteTime -ge $started }
if (-not $packages) { throw "Craft produced no Kolf package in $CraftRoot\tmp" }

$archive = $packages | Where-Object Extension -eq '.7z' | Select-Object -First 1
if (-not $archive) { throw 'Craft produced no portable archive to verify' }
$listing = & (Join-Path $CraftRoot 'dev-utils\bin\7za.exe') l -slt $archive.FullName
if ($LASTEXITCODE -ne 0) { throw 'Could not inspect the portable archive' }
if (-not ($listing -match '^Path = bin[\\/]Qt6WebSockets\.dll$')) { throw 'The Windows package is missing Qt6WebSockets.dll' }

New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$results = foreach ($package in $packages) {
    $target = Join-Path $OutputDirectory "kolf-$version-windows-x86_64$($package.Extension)"
    Copy-Item $package.FullName $target -Force
    $hash = (Get-FileHash $target -Algorithm SHA256).Hash.ToLowerInvariant()
    Set-Content -Path "$target.sha256" -Value "$hash  $(Split-Path $target -Leaf)" -NoNewline -Encoding ascii
    $target; "$target.sha256"
}
$results
