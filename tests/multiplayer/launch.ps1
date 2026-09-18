param(
    [Parameter(Mandatory=$true)][string]$Config,
    [string]$CraftRootPath = 'C:\CraftRoot',
    [string]$PythonPath = 'C:\Users\thecr\AppData\Local\Python\pythoncore-3.14-64\python.exe'
)
$ErrorActionPreference = 'Stop'
$prototypeConfigPath = (Resolve-Path -LiteralPath $Config).Path
$prototypeConfig = Get-Content -LiteralPath $prototypeConfigPath -Raw | ConvertFrom-Json
New-Item -ItemType Directory -Force -Path $prototypeConfig.logDirectory | Out-Null
$env:CRAFT_PYTHON = $PythonPath
$env:CRAFT_LOG_FILE = Join-Path $prototypeConfig.logDirectory 'craft-launch.log'
$env:KOLF_PROTOTYPE_CONFIG = $prototypeConfigPath
# Remove empty variables that Windows PowerShell cannot enumerate/remove twice.
if ([string]::IsNullOrEmpty($env:COLORTERM)) { Remove-Item Env:\COLORTERM -ErrorAction SilentlyContinue }
& (Join-Path $CraftRootPath 'craft\craftenv.ps1')
if (-not $?) { throw 'Craft initialization failed' }
craft --run kolf
exit $LASTEXITCODE
