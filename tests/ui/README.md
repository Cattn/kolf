# Scripted UI checks

`Kolf::Testing::UiScript` (`src/testing/uiscript.*`, built with
`KOLF_TEST_AUTOMATION`) runs a JSON list of steps against an ordinary Kolf
window and saves screenshots. It replaces most visible human checks: run a
script, then look at the PNGs.

```powershell
.\tests\ui\run-ui-script.ps1 -Script tests\ui\home-flow.json -PythonPath <python.exe for Craft>
```

Output goes to `tests/ui/output/<script>/` (ignored by git), or to
`-OutputDirectory`. The folder holds the screenshots and `ui-script.jsonl`,
which records each step and the focused widget. The process exits with 0 on
success, or 3 with a `failed` entry naming the step. Each run uses a fresh
`kolf-testrc` config, so your own settings are never touched.

Each step is an object. The keys run in this order, and `wait` (ms, default
150) sets the delay before the next step:

| Key | Effect |
| --- | --- |
| `writeFile` + `content` | Write a file into the output folder |
| `setConfig` `{group, key, value}` | Write to the test config |
| `action` | Trigger a KXmlGui action by name (for example `game_new`, `mainmenu`) |
| `click` | Click a button by object name |
| `clickText` (+ `in`) | Click a visible button by its label, in `modal` (default) or `window` |
| `key` (+ `to`) | Send a key sequence such as `Escape` to the focused widget, or to `to` (`game` = the active course view; use it, since desktop focus is unreliable) |
| `resize` `[w, h]` | Resize the main window |
| `expectVisible` / `expectHidden` | Fail unless the named widget is visible or hidden |
| `expectTitle` | Fail unless the window title contains the text |
| `screenshot` (+ `of`) | Save a PNG of `window` (default), `modal`, `active`, or a named widget |
| `quit` | Finish successfully |

Tokens `{output}` and `{courses}` in `writeFile` content and `setConfig`
values expand to the output folder and the installed course folder.
Actions, clicks, and keys are delivered on the next event-loop turn, so a step
that opens a modal dialog does not block the script.
