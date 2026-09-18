# Play and test the current two-client prototype

This guide applies to the source-checkout prototype. It currently has two
fixed players and one match per relay process. Ordinary Kolf still opens the
offline game; the planned Online/Create/Join lobby UI is not built yet. A
person can play this prototype, but needs the launcher and generated local
role configs described below. Use it on one Windows PC or a trusted LAN.

## Start a match on one Windows PC

Prerequisites: a working KDE Craft Kolf environment, Windows PowerShell, and
Node 22.18 or newer. Build once after source changes. Craft initialization and
the command must share the same PowerShell process:

```powershell
& C:\CraftRoot\craft\craftenv.ps1
craft --compile --install --qmerge kolf
```

From the repository root, start a two-client match on a shipped course:

```powershell
./tests/multiplayer/play.ps1
```

That opens the relay plus two Kolf windows on `courses/Easy.kolf`. Other shipped
courses:

```powershell
./tests/multiplayer/play.ps1 -Course Medium
./tests/multiplayer/play.ps1 -Course Hard
./tests/multiplayer/play.ps1 -Course Classic
```

Keep the relay window open during play. Close both Kolf windows when finished,
then stop the relay. Restart `play.ps1` for a new match. Do not edit the
generated credentials in `server/prototype/local-session/`.

For short test maps instead of a shipped course:

```powershell
./tests/multiplayer/play.ps1 -CoursePath ./tests/multiplayer/fixtures/static.kolf
./tests/multiplayer/play.ps1 -CoursePath ./tests/multiplayer/fixtures/water.kolf
./tests/multiplayer/play.ps1 -CoursePath ./tests/multiplayer/fixtures/teleport.kolf
```

The launcher defaults to this development PC's Craft and Python paths; pass
`-CraftRootPath` and `-PythonPath` to `launch.ps1` if yours differ.

## Controls and human checks

Only the player shown as the active slot can take a shot. The status line shows
phase, turn, revision, and whether input is ready. Click inside the course to
give it keyboard focus.

1. With **Mouse aiming** checked and **Advanced putting** unchecked, move the
   pointer to aim. Press and release the left mouse button to set power and
   shoot. Check that the other window cannot shoot, that one shot is accepted,
   and that both scorecards agree after the ball settles.
2. Uncheck **Mouse aiming**. Use Left/Right to aim, then press and release
   Space or Down to putt. Shift and Ctrl change the aim step. Press Escape
   during a power stroke to cancel; check that no shot or score is added.
3. Check **Advanced putting** on an owned turn. Use three deliberate left
   clicks, or three separate Space/Down taps: start the meter, choose power,
   then choose precision. Try both a well timed and an imperfect stroke.
   Escape should cancel before the last tap. Record whether the accepted-shot
   UI, direction, power, score, and next owner behave as expected.
4. On `water.kolf`, have each player choose **Drop outside hazard** and
   **Rehit** on separate attempts. Only the affected player's window should
   enable those buttons. Check the penalty, continued motion, next turn, and
   equal scorecards. On `teleport.kolf`, use **Resync** during motion and verify
   the game continues with one accepted shot and matching views.
5. Finish a match. Check both final scorecards, close both windows, and note
   whether either process remains open. A disconnect currently interrupts the
   match; restart the relay for another match.

These are manual checks, not claims of a pass. The scripted `run-native.ts`
runner sends canonical shot values directly and cannot verify the timing or
cancellation steps above.

## Offline regression on the same build

Open ordinary Kolf in a PowerShell window without `KOLF_PROTOTYPE_CONFIG`:

```powershell
& C:\CraftRoot\craft\craftenv.ps1
craft --run kolf
```

Use Game > New Game for a multi-hole course. Play normal and advanced shots;
check water and teleport behavior and score changes. Use Game > Save Game,
close Kolf, then Game > Load Game and compare the hole, turn, and scorecard
with what was saved. Record any failure and whether it also occurs without
the multiplayer changes. This offline path has not yet been reverified for P0.

## What to send back

For each check, mark **passed**, **failed**, or **not run**. Include the course,
platform, date, which client owned the turn, reproduction steps, expected and
actual behavior, and a screenshot if it helps. Include the run directory name
from `server/prototype/local-session/` and the source commit (`git rev-parse
HEAD`). The `authority/session.jsonl` and `guest/session.jsonl` files contain
useful event and score details. Keep generated `authority.json` and
`guest.json` private: they contain role credentials. Review logs and images
for private information before sharing them; sanitized report export is part
of the later test-kit stage.

## Current evidence and limits

The latest automated results are in
[2026-09-18-sync-followup.json](evidence/2026-09-18-sync-followup.json): 18 relay
tests and TypeScript checking passed; scripted water/slope and teleport/resync
native runs completed with matching committed states, zero guest gameplay
simulation, and clean scene shutdown. Earlier Windows prototype results are
in [2026-09-18-p0.json](evidence/2026-09-18-p0.json). Human mouse/keyboard
play, advanced-meter timing, offline save/load, two-machine play, and the
planned lobby/rematch flow remain unverified or unimplemented.
