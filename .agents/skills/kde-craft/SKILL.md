---
name: kde-craft
description: >-
  Build and run kolf on Windows with Craft. Use when the user invokes
  /kde-craft, or when compiling, running, or packaging kolf via Craft
  (craftenv.ps1, craft --compile --install --qmerge, craft --run).
disable-model-invocation: true
---

# KDE Craft (Windows)

Use Craft only to compile, run, and (when asked) package kolf on Windows. Do not invoke CMake, Ninja, Make, or the app executable directly unless Craft itself printed that as the way to proceed.

Default Craft root: `C:\CraftRoot`. If that path is missing, look for `craft\craftenv.ps1` or ask which Craft root to use.

## Environment (non-negotiable)

Craft commands only work in a PowerShell session that has already run the init script. Each agent shell is a **new process**, so **initialize and run Craft in the same invocation**.

```powershell
& C:\CraftRoot\craft\craftenv.ps1
craft <args>
```

- Use Windows PowerShell. Do not use cmd.exe or PowerShell ISE.
- `craftenv.ps1` loads `C:\CraftRoot\etc\CraftSettings.ini` (paths, ABI, compiler).
- If init prints errors, stop. Craft will not work until that is fixed.
- Successful init looks like:

```
Craft               : C:\CraftRoot
ABI                 : windows-cl-msvc2022-x86_64
Download directory  : C:\CraftRoot\download
Cache repository    : https://files.kde.org/craft/Qt6/...
```

Do not assume `craft`, `cs`, or `cb` exist until this script has run in that same shell.

### If Craft cannot find Python

Set `CRAFT_PYTHON` to a real `python.exe` **before** running `craftenv.ps1`, in the same PowerShell invocation as the Craft command. On this workstation, the verified interpreter is `C:\Users\thecr\AppData\Local\Python\pythoncore-3.14-64\python.exe`:

```powershell
$craftPython = 'C:\Users\thecr\AppData\Local\Python\pythoncore-3.14-64\python.exe'
if (-not (Test-Path -LiteralPath $craftPython -PathType Leaf)) { throw 'Find the installed Python executable and update $craftPython.' }
& $craftPython --version
$env:CRAFT_PYTHON = $craftPython
& C:\CraftRoot\craft\craftenv.ps1
craft --compile --install --qmerge kolf
```

If that path is absent, locate an installed Python interpreter (for example with `py -0p` or `Get-Command python -All`) and use its full executable path. A WindowsApps app execution alias is not a Python installation. Keep the override in the same shell; setting it in one agent command does not carry into the next. If initialization still reports an error, stop and inspect that error before running Craft.

## Allowed commands

Only these Craft commands are allowed. Do not use any other `craft ...` invocation unless the user explicitly names that exact command.

After editing kolf source:

```powershell
& C:\CraftRoot\craft\craftenv.ps1
craft --compile --install --qmerge kolf
```

To run the built app:

```powershell
& C:\CraftRoot\craft\craftenv.ps1
craft --run kolf
```

Typical loop: change source → `--compile --install --qmerge` → `--run`. Do not skip qmerge after a local compile if the next step is running the app.

Packaging is allowed **only when the user says kolf is ready to package** (installer / redistributable). Compile+install+qmerge first if source changed:

```powershell
& C:\CraftRoot\craft\craftenv.ps1
craft --package kolf
```

To jump to kolf source or build dirs (after init):

```powershell
cs kolf
cb kolf
```

## Do not use

Do not search, install, uninstall, update, rebuild from cache, pin versions, or otherwise mutate the Craft tree. That includes, but is not limited to:

- `craft packagename` (install)
- `craft --search`
- `craft --unmerge`
- `craft -i` / `craft --upgrade`
- `craft -i craft` / `craft -i craft-blueprints-kde`
- `craft --no-cache`
- `craft --set version=...`

If a build seems to need one of those, stop and ask. Do not "fix" Craft by installing deps or upgrading packages.

## Troubleshooting

- If Craft looks stalled, send Enter in that PowerShell session.
- Build logs: `%USERPROFILE%\.craft`. Search that log and the terminal output for `error`.
- Antivirus scanning `C:\CraftRoot` can make builds fail (e.g. `kdoctools`). Exclude the Craft root if extracts/builds hang or fail oddly.
- QtWebEngine packages need MSVC, not MinGW. Craft Qt6 expects Visual Studio 2022.
