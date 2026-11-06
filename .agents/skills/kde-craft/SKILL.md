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

Set `CRAFT_PYTHON` to a real `python.exe` **before** running `craftenv.ps1`, in the same PowerShell invocation as the Craft command. Verified interpreters (the user profile differs per machine):

| Machine user | Interpreter |
| --- | --- |
| `thecr` | `C:\Users\thecr\AppData\Local\Python\pythoncore-3.14-64\python.exe` |
| `cattn` | `C:\Users\cattn\AppData\Local\Programs\Python\Python312\python.exe` (`py -0p` also lists a 3.13 path that does not exist; ignore it) |

```powershell
$craftPython = 'C:\Users\thecr\AppData\Local\Python\pythoncore-3.14-64\python.exe'
if (-not (Test-Path -LiteralPath $craftPython -PathType Leaf)) { throw 'Find the installed Python executable and update $craftPython.' }
& $craftPython --version
$env:CRAFT_PYTHON = $craftPython
& C:\CraftRoot\craft\craftenv.ps1
craft --compile --install --qmerge kolf
```

If that path is absent, locate an installed Python interpreter (for example with `py -0p` or `Get-Command python -All`) and use its full executable path. A WindowsApps app execution alias is not a Python installation. Keep the override in the same shell; setting it in one agent command does not carry into the next. If initialization still reports an error, stop and inspect that error before running Craft.

### Agent-shell environment fixes (process-only; never edit Craft settings for these)

Agent shells inherit an environment that differs from an interactive Craft shell. Three failures have been seen. All are fixed by adjusting variables **in the same PowerShell invocation** before `craftenv.ps1`:

1. **`JSONDecodeError: Expecting value: line 1 column 1` at `craftenv.ps1:131`.** Python is usually *not* the cause. Craft runs `vcvarsall.bat` and parses its output as JSON, with stderr merged into it. When `vswhere.exe` is not on PATH, `vcvarsall` prints `'vswhere.exe' is not recognized…` before the JSON, and parsing fails. Fix: prepend the Visual Studio Installer folder to PATH.
2. **`Found "sh" in your PATH: …\scoop\shims`.** Craft warns that scoop's `sh` breaks its configuration. Drop `scoop\shims` from PATH for the process.
3. **The Kolf launcher scripts (`tests/multiplayer/launch-online*.ps1`) fail in `craftenv.ps1` with `Remove-Item … Env:\GIT_ASKPASS … ItemNotFoundException`.** The agent sets some variables (for example `GIT_ASKPASS`) to an empty string. Windows PowerShell 5 cannot remove them under `$ErrorActionPreference = 'Stop'`. Remove empty variables before starting the native test runner, which passes its environment to those launchers.

Prelude that covers all three (adjust `$craftPython` per the table above):

```powershell
Get-ChildItem env: | Where-Object { [string]::IsNullOrEmpty($_.Value) } | ForEach-Object { Remove-Item "env:$($_.Name)" -ErrorAction SilentlyContinue }
$env:PATH = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer;" + (($env:PATH -split ';' | Where-Object { $_ -and $_ -notmatch 'scoop\\shims' }) -join ';')
$env:CRAFT_PYTHON = 'C:\Users\cattn\AppData\Local\Programs\Python\Python312\python.exe'
& C:\CraftRoot\craft\craftenv.ps1
craft --compile --install --qmerge kolf
```

When debugging environment problems, do **not** print the whole environment (`dumpenv.py`, `Get-ChildItem env:`, `set`). The user's environment holds API keys. Print only the variables you need.

## Native online tests

`tests/multiplayer/run-online-native.ts <scenario>` starts a local server and launches each client through `launch-online-test.ps1` → `craftenv.ps1` → `craft --run kolf`. Run it from the repository root, in the same PowerShell invocation as the prelude above (minus the compile line), after a successful `--compile --install --qmerge`:

```powershell
node tests/multiplayer/run-online-native.ts host-combined
```

The runner prints one JSON line with `"outcome":"passed"` and the run directory under `server/local-session/`. Scenarios include `two-rematch`, `four-player`, `four-player-hazard`, `custom-course`, `host-combined`, `host-proposal-approve`, and `host-proposal-reject`. Each can take a few minutes; run long batches in the background.

## Scripted UI checks and screenshots

Prefer these over asking the user to look. After a build:

```powershell
.\tests\ui\run-ui-script.ps1 -Script tests\ui\home-flow.json -PythonPath $craftPython
```

The runner applies the environment fixes above itself. Screenshots and `ui-script.jsonl` are written to `tests/ui/output/<script>/`; view the PNGs with the Read tool. Step syntax is in `tests/ui/README.md`. Exit code 0 means every step and expectation passed.

### `scripts/build-windows-craft.ps1`

That helper is for first-time setup of a Craft tree: it runs `craft -i libs/qt6/qtwebsockets` and patches and rebuilds `libkdegames`. Installing is otherwise forbidden by this skill, so run it **only** when the user approves (for example on a new or reset Craft root). For everyday builds, use `craft --compile --install --qmerge kolf`.

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
