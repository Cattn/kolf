# Human testing for Kolf online multiplayer

The current product path is protocol v3 from ordinary Kolf through
**Game > Online…**. It supports multiple rooms, 2–8 frozen player slots, and
multiple locally owned slots without generated role configs. The older v1
source-checkout prototype remains below only as a historical regression path.

## Activate and test the ordinary in-game lobby

Build once after source changes. The simplest local launch from the repository
root is:

```powershell
.\tests\multiplayer\play.ps1 -ClientCount 2
```

It selects a free port, starts the real v3 service, launches two ordinary Kolf
clients through Craft, and prints the endpoint and log directory. Use
`-ClientCount 3` for a three-member check. Keep the launcher open.

In each Kolf window:

1. Choose **Game > Online…**. Confirm the Connect page appears from normal app
   startup and that cancelling or closing it leaves offline Kolf usable.
2. Connect both windows to the printed endpoint. In the first window choose a
   display name, `#RRGGBBAA` color, and shipped course, then select **Create
   Lobby**. Copy the visible join code.
3. In the second window enter a different name/color and the join code, then
   select **Join Lobby**. Confirm both windows show separate member and player
   lists with the same order, course, revision, and readiness state.
4. In one window choose **Add local player**, then edit that slot. Confirm only
   its owner can edit/remove it, at least one local slot remains, and each
   mutation clears every Ready state. Use four slots total for the representative
   three-client run; do not launch eight clients.
5. Change the course as the owner and confirm the catalog came from the service.
   Ready every member; only the owner should have an enabled **Start** button.
6. Start and play the match. Confirm the same client automatically controls each
   of its owned slots, remote turns cannot be controlled locally, and score rows
   use player names. Exercise one hazard choice for a locally owned player.
7. Finish the match. Confirm every client shows the same ordered Results, Return
   to the lobby, ready again, and start one rematch with a different match ID.
8. Disconnect a non-owner during an active match and confirm only that room is
   interrupted. The remaining members must reach Results and Return without
   waiting for the departed member. Owner departure closes only that room.

Mark each step passed, failed, or unverified. Stop after this bounded smoke; do
not expand it into every player-count or ownership permutation.

Scripted ordinary-client coverage is available from `server/prototype` as
`npm run test:native:v3:rematch`, `npm run test:native:v3:four-player`, and
`npm run test:native:v3:hazard`. These validate protocol/gameplay state and
cleanup, but do not replace the mouse/keyboard and visible UI checks above.

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

The v3 core/build checkpoint is recorded in
[2026-09-21-v3-core.json](evidence/2026-09-21-v3-core.json). Earlier v2 and
legacy native checkpoints remain in
[2026-09-19-stage1.json](evidence/2026-09-19-stage1.json) and
[2026-09-19-native-lobby-ui.json](evidence/2026-09-19-native-lobby-ui.json).
The latest historical v1 gameplay results are in
[2026-09-18-sync-followup.json](evidence/2026-09-18-sync-followup.json), with
earlier Windows prototype results in
[2026-09-18-p0.json](evidence/2026-09-18-p0.json). Human in-game lobby
variable-roster mouse/keyboard play, advanced-meter timing, offline save/load,
two-machine play, the representative native hazard case, and a complete native
v3 Results/Return/rematch flow remain unverified until the bounded smoke above
is recorded.
