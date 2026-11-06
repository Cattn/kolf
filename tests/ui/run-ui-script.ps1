param(
    [Parameter(Mandatory=$true)][string]$Script,
    [string]$OutputDirectory,
    [string]$CraftRootPath = 'C:\CraftRoot',
    [string]$PythonPath
)
$ErrorActionPreference = 'Stop'
$scriptPath = (Resolve-Path -LiteralPath $Script).Path
$definition = Get-Content -LiteralPath $scriptPath -Raw | ConvertFrom-Json
$output = if ($OutputDirectory) { $OutputDirectory } elseif ($definition.outputDirectory) { $definition.outputDirectory } else { Join-Path (Join-Path (Split-Path -Parent $scriptPath) 'output') ([IO.Path]::GetFileNameWithoutExtension($scriptPath)) }
New-Item -ItemType Directory -Force -Path $output | Out-Null

Get-ChildItem env: | Where-Object { [string]::IsNullOrEmpty($_.Value) } |
    ForEach-Object { Remove-Item "env:$($_.Name)" -ErrorAction SilentlyContinue }
$installer = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer'
$env:PATH = "$installer;" + (($env:PATH -split ';' | Where-Object { $_ -and $_ -notmatch 'scoop\\shims' }) -join ';')
if ($PythonPath) { $env:CRAFT_PYTHON = $PythonPath }
Remove-Item env:KOLF_ONLINE_TEST_CONFIG -ErrorAction SilentlyContinue
$env:KOLF_UI_TEST_SCRIPT = $scriptPath
$env:KOLF_UI_TEST_OUTPUT = (Resolve-Path -LiteralPath $output).Path
Get-ChildItem -LiteralPath $env:LOCALAPPDATA -Filter 'kolf-testrc*' -File -ErrorAction SilentlyContinue | Remove-Item -Force
$env:CRAFT_LOG_FILE = Join-Path $output 'craft-launch.log'

& (Join-Path $CraftRootPath 'craft\craftenv.ps1') | Out-Null
if (-not $? -or -not (Get-Command craft -ErrorAction SilentlyContinue)) { throw 'Craft initialization failed' }
craft --run kolf
$code = $LASTEXITCODE
$log = Join-Path $output 'ui-script.jsonl'
if (Test-Path -LiteralPath $log) { Get-Content -LiteralPath $log | Select-Object -Last 3 }
exit $code
