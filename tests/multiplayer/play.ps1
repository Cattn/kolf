param(
    [string]$Course = 'Easy',
    [string]$CoursePath = ''
)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$relayDir = Join-Path $root 'server\prototype'
$sessionDir = Join-Path $relayDir 'local-session'
$courseFile = if ($CoursePath) {
    (Resolve-Path -LiteralPath $CoursePath).Path
} else {
    (Resolve-Path -LiteralPath (Join-Path $root "courses\$Course.kolf")).Path
}
$authority = Join-Path $sessionDir 'authority.json'
$guest = Join-Path $sessionDir 'guest.json'
Remove-Item -Force -ErrorAction SilentlyContinue $authority, $guest
if (-not (Test-Path (Join-Path $relayDir 'node_modules'))) {
    Push-Location $relayDir
    try { npm ci } finally { Pop-Location }
}
Start-Process powershell -ArgumentList @(
    '-NoExit',
    '-Command',
    "`$env:KOLF_COURSE = '$courseFile'; Set-Location '$relayDir'; npm start"
)
$deadline = (Get-Date).AddSeconds(30)
while (-not ((Test-Path $authority) -and (Test-Path $guest))) {
    if ((Get-Date) -gt $deadline) { throw 'Relay did not write client configs. Check the relay window.' }
    Start-Sleep -Milliseconds 200
}
$launch = Join-Path $PSScriptRoot 'launch.ps1'
Start-Process powershell -ArgumentList @('-NoExit', '-File', $launch, '-Config', $authority)
Start-Process powershell -ArgumentList @('-NoExit', '-File', $launch, '-Config', $guest)
Write-Host "Course: $courseFile"
Write-Host 'Relay and two clients are starting. Close both Kolf windows when done, then stop the relay window.'
