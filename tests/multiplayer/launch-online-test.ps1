param(
    [Parameter(Mandatory=$true)][string]$Config,
    [Parameter(Mandatory=$true)][string]$Course,
    [string]$CraftRootPath = 'C:\CraftRoot',
    [string]$PythonPath
)
$ErrorActionPreference = 'Stop'
$onlineConfigPath = (Resolve-Path -LiteralPath $Config).Path
$coursePath = (Resolve-Path -LiteralPath $Course).Path
$onlineConfig = Get-Content -LiteralPath $onlineConfigPath -Raw | ConvertFrom-Json
New-Item -ItemType Directory -Force -Path $onlineConfig.logDirectory | Out-Null
if ($PythonPath) { $env:CRAFT_PYTHON = $PythonPath }
$env:CRAFT_LOG_FILE = Join-Path $onlineConfig.logDirectory 'craft-launch.log'
$env:KOLF_ONLINE_TEST_CONFIG = $onlineConfigPath
$env:KOLF_ONLINE_TEST_COURSE = $coursePath
if ($onlineConfig.forceCourseDownload) { $env:KOLF_ONLINE_TEST_FORCE_DOWNLOAD = '1' }
if ([string]::IsNullOrEmpty($env:COLORTERM)) { Remove-Item Env:\COLORTERM -ErrorAction SilentlyContinue }
& (Join-Path $CraftRootPath 'craft\craftenv.ps1')
if (-not $?) { throw 'Craft initialization failed' }
craft --run kolf
exit $LASTEXITCODE
