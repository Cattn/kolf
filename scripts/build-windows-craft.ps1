param(
    [string]$CraftRoot = 'C:\CraftRoot'
)

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
function Invoke-CraftAction {
    param([string[]]$Arguments, [string[]]$ExpectedActions)
    $output = & craft @Arguments 2>&1 | Out-String
    Write-Output $output
    if ($LASTEXITCODE -ne 0) { throw "Craft failed: $($Arguments -join ' ')" }
    foreach ($action in $ExpectedActions) {
        if ($output -notmatch "(?m)^\*\*\* Craft $action succeeded:") {
            throw "Craft did not confirm $action success: $($Arguments -join ' ')"
        }
    }
}

$craftEnvironment = Join-Path $CraftRoot 'craft\craftenv.ps1'
if (-not (Test-Path -LiteralPath $craftEnvironment)) {
    throw "Craft environment not found at $craftEnvironment"
}

# Craft's binary Kolf blueprint does not yet declare Qt WebSockets.
& $craftEnvironment
Invoke-CraftAction -Arguments @('-i', 'libs/qt6/qtwebsockets') -ExpectedActions @('all')

# libkdegames' OpenAL global static is destroyed during DLL_PROCESS_DETACH on
# Windows. OpenAL can block there while waiting for its worker thread. Keep its
# lifetime tied to QApplication, so shutdown happens before the loader lock.
$libraryWork = Join-Path $CraftRoot 'build\kde\kdegames\libkdegames\work'
$source = @(Get-ChildItem -LiteralPath $libraryWork -Directory -Filter 'libkdegames-*' -ErrorAction SilentlyContinue |
    Sort-Object Name -Descending |
    ForEach-Object { Join-Path $_.FullName 'src\audio\kgameaudioscene-openal.cpp' } |
    Where-Object { Test-Path -LiteralPath $_ }) | Select-Object -First 1
if (-not $source) {
    Invoke-CraftAction -Arguments @('--fetch', '--unpack', 'libkdegames') -ExpectedActions @('fetch', 'unpack')
    $source = @(Get-ChildItem -LiteralPath $libraryWork -Directory -Filter 'libkdegames-*' |
        Sort-Object Name -Descending |
        ForEach-Object { Join-Path $_.FullName 'src\audio\kgameaudioscene-openal.cpp' } |
        Where-Object { Test-Path -LiteralPath $_ }) | Select-Object -First 1
}
if (-not $source) { throw 'Could not locate libkdegames OpenAL source' }
$content = [IO.File]::ReadAllText($source)
if ($content.Contains('Q_GLOBAL_STATIC(KGameOpenALRuntime, g_runtime)')) {
    $content = $content.Replace('#include "kgameaudioscene.h"', '#include "kgameaudioscene.h"' + "`n#include <QApplicationStatic>")
    $content = $content.Replace('Q_GLOBAL_STATIC(KGameOpenALRuntime, g_runtime)', 'Q_APPLICATION_STATIC(KGameOpenALRuntime, g_runtime)')
    [IO.File]::WriteAllText($source, $content)
} elseif (-not $content.Contains('Q_APPLICATION_STATIC(KGameOpenALRuntime, g_runtime)')) {
    throw 'Unknown libkdegames OpenAL runtime declaration; review the dependency before building'
}
Invoke-CraftAction -Arguments @('--compile', '--install', '--qmerge', 'libkdegames') -ExpectedActions @('compile', 'install', 'qmerge')
Invoke-CraftAction -Arguments @('--compile', '--install', '--qmerge', 'kolf') -ExpectedActions @('compile', 'install', 'qmerge')
