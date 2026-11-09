---
name: kolf-course
description: >-
  Create or edit a Kolf course file (.kolf) by hand or with a script, using the
  built-in objects, and validate it with Kolf's own harnesses. Use when asked
  to make, generate, lay out, tune, or check a Kolf course or hole.
---

# Making a Kolf course

A course is one KConfig (INI-style) text file. Kolf reads it, so the format
below is exact. Build Kolf first with the `kde-craft` skill; the checks use the
installed `C:\CraftRoot\bin\kolf.exe` (the build-tree binary cannot find its
theme data).

## File layout

```ini
[0-course@-50,-50]
Name=My Course
author=Your Name

[1-hole@-50,-50|0]
par=3
maxstrokes=10
borderWalls=true

[1-ball@60,200]
dummykey=true

[1-cup@340,200|1]
dummykey=true

[1-sand@200,180|2]
width=60
height=40
```

- Groups are `[<hole>-<type>@<x>,<y>|<id>]`. Holes count from 1. `id` is unique
  within a hole (1, 2, 3...). The course header is always `0-course@-50,-50`,
  each hole header `<n>-hole@-50,-50|0`, and the tee `<n>-ball@x,y` (no id).
- Every group needs at least one key. Use `dummykey=true` for objects with no
  properties, or the group is dropped on save.
- Values: numbers, `true`/`false`, points as `x,y`, text as-is.
- `Name`/`author` go in the course header. Hole keys: `par`, `maxstrokes`
  (strokes before the player is picked up), `borderWalls` (walls around the
  field).
- Do not hand-write `formatVersion` or `decorationTypes`; the resave step below
  adds them.

## Coordinates

- The field is 400 x 400 logical units, origin top-left. Keep everything
  inside about 12..388 so it does not overlap the border walls.
- Data-defined objects (ice, mud, lava, wind, ...) are positioned by their
  **centre**; `rotation` turns them and swaps width and height visually.
- A wall is `[<n>-wall@0,0|<id>]` with `startPoint=x,y` and `endPoint=x,y`
  relative to its position (so at `@0,0` they are field coordinates).
- Built-in rectangular items (sign, bridge, sand, puddle, slope) may anchor
  differently; always check a screenshot (below) rather than assume.
- A sign is `[<n>-sign@x,y|<id>]` with `Comment=text`; it has walls unless you
  set `topWallVisible`, `leftWallVisible`, `rightWallVisible`,
  `botWallVisible` to `false`.

## Objects and their keys

The authoritative list of object types and every property (key, type,
default, min/max, enum values) is `protocol/object-catalog.json`. Read it
instead of guessing. Useful types:

| Kind | Types |
| --- | --- |
| Course | `cup` (required, one per hole), `ball` (the tee) |
| Classic | `wall`, `bumper`, `windmill`, `floater`, `bridge`, `sign`, `sand`, `puddle`, `slope`, `blackhole` |
| Surfaces | `ice`, `mud`, `rough`, `shallowwater` |
| Pushers and fields | `speedpad`, `conveyor`, `wind`, `magnet`, `repulsor`, `whirlpool` |
| Hazards | `lava` (back to the last spot, +1), `pit` (back to the tee, +1), `quicksand`, `crumble`, `fence`, `mine` |
| Mechanisms | `plate` + `gate` linked by the same `channel` text |

Each data object's help text in `objects/<type>.kobj.json` says what it does.
Unknown types and out-of-range values fail `--check-course` (below) and online upload.

## Generate, don't hand-edit

For more than a few objects, write a small script (Python is fine) that emits
the groups from a list of holes, as `courses/WeatherReport` was made. Keep the
script out of the repo unless asked.

## Validate (run all of these)

Run from the repo root with the Craft environment loaded (`kde-craft`
prelude). `$out` is any scratch folder.

1. **Valid and playable.** The same validator the online upload uses: unknown
   types, out-of-range values, missing tee or cup, holes out of order, unsafe
   keys. It names the offending group or key:
   ```powershell
   $env:QT_QPA_PLATFORM='offscreen'; C:\CraftRoot\bin\kolf.exe --check-course <path> 2>&1 | % { "$_" }
   # expect "course: valid and playable", exit code 0
   ```
2. **Canonicalise.** Resave rewrites the file in Kolf's own form (adds
   `formatVersion=2`, sorts keys, fills defaults). It does **not** reject bad
   input: unknown types are kept and out-of-range values are clamped, so run
   step 1 first.
   ```powershell
   $env:KOLF_RESAVE_COURSES='<folder with only the course>'; $env:KOLF_RESAVE_OUTPUT=$out
   C:\CraftRoot\bin\kolf.exe 2>&1 | % { "$_" } | Select-String 'resave:'   # expect "<name> round-trips"
   ```
   Copy `$out\<name>.kolf` over your file. Edits made after this (for example
   turning sign walls off) need one more resave to confirm it still
   round-trips identically.
3. **Every hole can be finished.** The greedy solver plays each hole and must
   print `holed in N strokes` for every hole:
   ```powershell
   $env:KOLF_SOLVE_COURSE='<path to course>'; C:\CraftRoot\bin\kolf.exe 2>&1 | % { "$_" } | Select-String 'solve:'
   ```
   It is an upper bound, not a difficulty rating. If it holes in 1 everywhere,
   the holes are probably too open.
4. **Shots settle.** The replay harness plays three fixed shots per hole;
   no shot may report `unsettled`:
   ```powershell
   $env:KOLF_REPLAY_COURSES='<folder>'; $env:KOLF_REPLAY_OUTPUT=$out; $env:KOLF_REPLAY_GOLDEN=''
   C:\CraftRoot\bin\kolf.exe; Get-Content "$out\<name>.json"
   ```
5. **Look at it.** Take one screenshot per hole with a UI script (see
   `tests/ui/README.md`): `open` the course file, `click` `setupStart`, then
   `screenshot` with `"of": "game"` and `action` `nexthole` between holes, run
   with `tests/ui/run-ui-script.ps1`. Check objects sit inside the border, the
   cup and tee are visible, and nothing covers the cup.

Clean up the environment variables between steps (`Remove-Item env:KOLF_*`).

## Shipping a course with the game (only when asked)

Add `<id>|<FileName>` to `courses/manifest.txt` (the server reads it too),
record golden replays with `tests/replay/run-replay.ps1 -Course <FileName>
-Update`, add the id to the course list in
`server/test/websocket-service.test.ts`, and run the quick tier
(`tests/run-checks.ps1`). Showcase courses also go in
`tests/replay/run-solve.ps1`.

## Custom objects

Courses can only use object types the player's Kolf already has. New
data-defined objects (`docs/making-objects.md`) load from installed
`objects/` folders or `KOLF_OBJECT_DIRS`, but cannot yet be bundled with a
course or used online; that is planned for Phase 5 (`.kolfpack`,
`.kolfcourse`). Stick to built-in types for courses meant for others.
