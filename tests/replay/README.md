# Golden replays

`run-replay.ps1` plays every shipped course offscreen with three fixed shots per
hole and compares the result with `golden/<course>.json`. Later changes to
gameplay code must keep these files unchanged, or explain each difference.

```powershell
.\tests\replay\run-replay.ps1                    # compare with the golden files
.\tests\replay\run-replay.ps1 -Course Hard.kolf  # one course
.\tests\replay\run-replay.ps1 -Runs 3            # also check that repeated runs match
.\tests\replay\run-replay.ps1 -Update -Runs 3    # rewrite the golden files
```

It runs an installed Kolf (`-Kolf`, default `C:\CraftRoot\bin\kolf.exe`) built
with `KOLF_TEST_AUTOMATION`, because Kolf needs its installed theme and data.
On other platforms, set the variables yourself and run `kolf`:

| Variable | Meaning |
| --- | --- |
| `KOLF_REPLAY_COURSES` | A course file, or a folder of courses |
| `KOLF_REPLAY_GOLDEN` | Folder of golden files to compare with |
| `KOLF_REPLAY_UPDATE=1` | Write the golden files instead of comparing |
| `KOLF_REPLAY_OUTPUT` | Folder to also write this run's results to |
| `KOLF_REPLAY_RUNS` | Play each course this many times and require identical results |
| `KOLF_REPLAY_SEED` | Base random seed (default 1); hole *n* uses seed + *n* (advanced putting and random hole only; bumpers are not random) |

Set `QT_QPA_PLATFORM=offscreen`. Each shot aims at the nearest cup: full
direction at 60% power, 25° off at 35%, then straight at 20%. The harness steps
the physics itself instead of using timers, loads a fresh game for every hole, settles the turn through the online
authority path, and drops the ball out of water hazards. A shot records the
ball position and state, strokes, physics steps, and any objects that moved.

## Save round trip and property defaults

`KOLF_RESAVE_COURSES` (a course or a folder) and `KOLF_RESAVE_OUTPUT` (a
folder) make Kolf load every hole of each course, save it as
`<course>.kolf`, save that again as `<course>.roundtrip.kolf`, and fail if the
two differ. The same run checks that a newly created object of every
registered type reports exactly the properties in its schema, with the
schema's default values. When changing how objects load or save, record the
output before the change and compare the files byte for byte afterwards.
