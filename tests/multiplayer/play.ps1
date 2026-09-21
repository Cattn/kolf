param(
    [ValidateRange(2, 3)][int]$ClientCount = 2,
    [string]$CraftRootPath = 'C:\CraftRoot',
    [string]$PythonPath = 'C:\Users\thecr\AppData\Local\Python\pythoncore-3.14-64\python.exe'
)
$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$serviceRoot = Join-Path $repoRoot 'server\prototype'
if (-not (Test-Path -LiteralPath (Join-Path $serviceRoot 'node_modules'))) {
    throw 'Run npm ci in server\prototype before launching the local service.'
}

$listener = [System.Net.Sockets.TcpListener]::new([System.Net.IPAddress]::Loopback, 0)
$listener.Start()
$port = ([System.Net.IPEndPoint]$listener.LocalEndpoint).Port
$listener.Stop()

$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$runDirectory = Join-Path $serviceRoot "local-session\v3-$stamp"
New-Item -ItemType Directory -Force -Path $runDirectory | Out-Null
$serviceOut = Join-Path $runDirectory 'service.stdout.jsonl'
$serviceError = Join-Path $runDirectory 'service.stderr.log'
$node = (Get-Command node -ErrorAction Stop).Source
$service = $null
$clients = @()
$previousPort = $env:KOLF_PORT
$previousBind = $env:KOLF_BIND
try {
    $env:KOLF_PORT = [string]$port
    $env:KOLF_BIND = '127.0.0.1'
    $service = Start-Process -FilePath $node -ArgumentList (Join-Path $serviceRoot 'relay-v3.ts') -WorkingDirectory $serviceRoot `
        -RedirectStandardOutput $serviceOut -RedirectStandardError $serviceError -WindowStyle Hidden -PassThru
    $env:KOLF_PORT = $previousPort
    $env:KOLF_BIND = $previousBind
    $deadline = (Get-Date).AddSeconds(30)
    while (-not (Test-Path -LiteralPath $serviceOut) -or -not ((Get-Content -LiteralPath $serviceOut -Raw -ErrorAction SilentlyContinue) -match '"event":"listening"')) {
        if ($service.HasExited) { throw "The v3 service exited early. See $serviceError" }
        if ((Get-Date) -gt $deadline) { throw "The v3 service did not become ready. See $serviceError" }
        Start-Sleep -Milliseconds 100
    }
    $endpoint = "ws://127.0.0.1:$port"
    Write-Host "Kolf v3 endpoint: $endpoint"
    Write-Host "Run logs: $runDirectory"
    Write-Host "Open Game > Online in each client and enter the endpoint. Closing every client stops this launcher and its service."
    $launcher = Join-Path $PSScriptRoot 'launch-online.ps1'
    for ($index = 1; $index -le $ClientCount; ++$index) {
        $clientLog = Join-Path $runDirectory "client-$index"
        New-Item -ItemType Directory -Force -Path $clientLog | Out-Null
        $clients += Start-Process -FilePath 'powershell.exe' -ArgumentList @('-NoProfile', '-File', $launcher,
            '-CraftRootPath', $CraftRootPath, '-PythonPath', $PythonPath, '-LogDirectory', $clientLog) `
            -WindowStyle Hidden -PassThru
    }
    while ($clients.Where({ -not $_.HasExited }).Count -gt 0) { Start-Sleep -Milliseconds 500 }
}
finally {
    $env:KOLF_PORT = $previousPort
    $env:KOLF_BIND = $previousBind
    foreach ($client in $clients) {
        if (-not $client.HasExited) { & taskkill.exe /PID $client.Id /T /F 2>$null | Out-Null }
    }
    if ($service -and -not $service.HasExited) { Stop-Process -Id $service.Id -Force -ErrorAction SilentlyContinue }
}
