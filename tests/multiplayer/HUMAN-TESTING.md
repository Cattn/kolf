# Human testing for Kolf online multiplayer

The current build has two incremental paths. The new protocol-v2 lobby is
activated from ordinary Kolf with **Game > Online…** and needs no generated
role config or `KOLF_PROTOTYPE_CONFIG`. It currently verifies the human
Connect/Create/Join/Ready/Start preparation flow; authoritative gameplay is not
wired to that lobby yet. The older source-checkout prototype remains below for
manual shot, hazard, and synchronization checks until gameplay moves to v2.

## Activate and test the ordinary in-game lobby

Build once after source changes. From `server/prototype`, start the v2 service:

```powershell
npm run start:v2
```

It prints the local endpoint and shipped-course catalog. The default endpoint
is `ws://127.0.0.1:3011`. Keep this PowerShell window open.

Open two more Windows PowerShell windows. In each one, initialize Craft and
launch an ordinary Kolf process; do not set `KOLF_PROTOTYPE_CONFIG`:

```powershell
& C:\CraftRoot\craft\craftenv.ps1
craft --run kolf
```

In each Kolf window:

1. Choose **Game > Online…**. Confirm the Connect page appears from normal app
   startup and that cancelling or closing it leaves offline Kolf usable.
2. Connect both windows to the printed endpoint. In the first window choose a
   display name, `#RRGGBBAA` color, and shipped course, then select **Create
   Lobby**. Copy the visible join code.
3. In the second window enter a different name/color and the join code, then
   select **Join Lobby**. Confirm both windows show the same two names, course,
   revision, and readiness state. Neither window should expose a generated
   credential or require JSON editing.
4. Change the course as the owner and confirm both Ready states clear. Confirm
   the non-owner cannot change it. Ready both players on the displayed
   revision; only the owner should have an enabled **Start** button.
5. Select **Start** once. Both clients should verify the installed course and
   show that they are waiting for the authoritative initial state. Repeated
   Start clicks must not create a second match. This is the current v2 native
   checkpoint; a playable scene and Results/Return/rematch UI become testable
   when the match-session migration lands.
6. Disconnect one window. The other should receive a readable lobby-closed
   message and remain connected far enough to create a new lobby. Close and
   reopen **Game > Online…** and check that the nonsecret endpoint, display
   name, and color were remembered without persisting a join code.

Mark each step passed, failed, or not run. Do not count the preparation-only
checkpoint as a completed multiplayer match.

## Play the legacy v1 two-client prototype

This path currently has two fixed players and one match per relay process. Use
it on one Windows PC or a trusted LAN for the shot/hazard checks below.

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

The v2 protocol/lobby/WebSocket checkpoint is recorded in
[2026-09-19-stage1.json](evidence/2026-09-19-stage1.json). The latest legacy
native-UI build checkpoint is recorded in
[2026-09-19-native-lobby-ui.json](evidence/2026-09-19-native-lobby-ui.json). The latest legacy
gameplay results are in
[2026-09-18-sync-followup.json](evidence/2026-09-18-sync-followup.json), with
earlier Windows prototype results in
[2026-09-18-p0.json](evidence/2026-09-18-p0.json). Human in-game lobby
activation, mouse/keyboard play, advanced-meter timing, offline save/load,
two-machine play, and a complete v2 Results/Return/rematch flow remain
unverified until a person records them.
