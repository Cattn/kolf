# SPDX-License-Identifier: GPL-2.0-or-later
# Registers kolf:// invite links for the current Windows user, pointing at a Kolf build.
# Installers register the scheme themselves; use this for development builds.
param(
    [string]$KolfExe = 'C:\CraftRoot\bin\kolf.exe',
    [switch]$Remove
)
$ErrorActionPreference = 'Stop'
$key = 'HKCU:\Software\Classes\kolf'
if ($Remove) {
    if (Test-Path $key) { Remove-Item $key -Recurse -Force }
    'Removed the kolf:// link handler.'
    return
}
if (-not (Test-Path $KolfExe)) { throw "Kolf not found at $KolfExe" }
New-Item -Path "$key\shell\open\command" -Force | Out-Null
Set-ItemProperty -Path $key -Name '(Default)' -Value 'URL:Kolf invite'
Set-ItemProperty -Path $key -Name 'URL Protocol' -Value ''
Set-ItemProperty -Path "$key\shell\open\command" -Name '(Default)' -Value "`"$KolfExe`" `"%1`""
"kolf:// links now open $KolfExe"
