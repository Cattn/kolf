param(
    [Parameter(Mandatory=$true)][string]$LogDirectory,
    [string]$CraftRootPath = 'C:\CraftRoot',
    [string]$PythonPath
)
$ErrorActionPreference = 'Stop'
$resolvedLogDirectory = (Resolve-Path -LiteralPath $LogDirectory).Path
if ($PythonPath) { $env:CRAFT_PYTHON = $PythonPath }
$env:CRAFT_LOG_FILE = Join-Path $resolvedLogDirectory 'craft-launch.log'
if ([string]::IsNullOrEmpty($env:COLORTERM)) { Remove-Item Env:\COLORTERM -ErrorAction SilentlyContinue }
& (Join-Path $CraftRootPath 'craft\craftenv.ps1')
if (-not $?) { throw 'Craft initialization failed' }
craft --run kolf
exit $LASTEXITCODE
