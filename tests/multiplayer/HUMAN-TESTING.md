# Human testing for Kolf online multiplayer

Build once after source changes, then start a bounded local session from the
repository root:

```powershell
.\tests\multiplayer\play.ps1 -ClientCount 2
```

The launcher prints the endpoint. In each ordinary Kolf window, choose
**Game > Online…** and press **Connect** for the preselected endpoint.
Check that the status bar changes from Connecting/Verifying to Connected once
the server responds, and that failures replace the pending message.

## One bounded online smoke

1. Create a lobby in the first window and join it from the second.
2. Confirm both windows show the same members, player slots, course, and Ready
   state. Edit one locally owned slot and confirm the mutation clears readiness.
3. Ready both members and start. Confirm only the owner of the active slot can
   shoot and both clients show matching scores.
4. Exercise one normal shot, one advanced-putting shot, and one hazard choice.
5. Finish the match and compare Results. Click **Rematch** in one window and
   confirm both status bars name that member and show (1/2). Click **Rematch**
   in the other window and confirm the lobby says Rematch accepted (2/2).
   Once both members are ready, the owner presses **Start** for a fresh match
   identity. The status bar should move on from upload, preparation, and result
   messages when those phases end.
6. Close the clients and confirm the launcher and service shut down.

## Server navigation

With two reachable servers, open Online with a saved server and confirm it
waits for **Connect**. Connect to server A, then use **Change Server** from the
connected entry and from a lobby to reach the same Connect page. Select server B
from recent servers or edit the address, then connect. Verify that the old
lobby closes for its owner or loses the departing member, **Leave Online**
returns to the paused local game, and keyboard navigation reaches the address,
server list, Connect, Change Server, and Leave Online controls. Check Results
and an active match separately; leaving an active match must end its scene and
give the remaining members an interrupted result.

For the representative variable-roster smoke, run with `-ClientCount 3`, add a
second local player to the owner, and play one four-player match. Do not expand
this into every roster or ownership permutation.

## Host reset slice

In a two-client match, verify that **Enable Host Controls** starts unchecked,
the guest cannot change it, and **Hole > Reset** (Ctrl+R) is unavailable to both.
The owner enables controls, then both players take a shot on the same hole.
Confirm that the guest still cannot reset. On the owner, invoke Reset through
the menu and shortcut in separate rounds; cancel once and confirm no state
changes. Accept once and compare both clients' hole, first player's turn,
ball/object placement, zero current-hole scores, and the reset notice. Verify
that a rematch starts with controls off. Repeat with three members and four
players to check owner identity is independent of the active player's slot.
Also check a rolling-ball attempt is unavailable and that offline Reset still
works after leaving Online.

**Current result:** automated two-client reset **passed** on Windows with
`tests/multiplayer/fixtures/static.kolf` after Craft compile/install/qmerge;
run directory `server/local-session/online-host-reset-1790182281562`.
Visible menu, shortcut, confirmation, rolling-ball, three-member/four-player,
and offline checks: **not run**, reset source `4c9fc23`. Automated
rematch regression also **passed** in
`server/local-session/online-two-rematch-1790182072769`.

## Host undo slice

In a two-client match, leave Host Controls off and confirm **Undo Shot** is
unavailable. Enable controls, let the guest take a shot and settle, then have
the owner use **Hole > Undo Shot**. Confirm that both clients show the guest's
pre-shot ball and object positions, score, and active turn, and that the guest
can replay the shot. The guest must not be able to invoke Undo. Check that
Undo is unavailable during rolling, after reset or a hole change, and when no
settled shot exists. Compare final Results counts with the played shots.
Repeat the owner-identity check with three members and four players, then
leave Online and check that offline Undo still works.

**Current result:** automated two-client Undo **passed** on Windows with
`tests/multiplayer/fixtures/static.kolf` after Craft compile/install/qmerge;
run directory `server/local-session/online-host-undo-1790191809909`. The host
undid a settled guest shot; both clients restored the authority's pre-shot
balls, objects, score, and active turn and then finished. Visible menu,
shortcut, rolling-ball, three-member/four-player, final Results comparison,
and offline checks: **not run** on Windows, Undo source `459e67d`.

## Host skip slice

With Host Controls enabled, score on the current hole while another player has
not yet taken a stroke. The owner invokes **Hole > Skip Hole**. Cancel once and
confirm no change; then accept. Confirm the scored player's strokes remain,
the unplayed player remains unscored, and both clients show the same next hole,
turn, and notice. The guest must not be able to skip. On the final hole, accept
a skip and check that Results labels it skipped, keeps partial strokes in the
scorecard, and omits it from completed-hole highlights. Confirm Skip is
unavailable during a rolling ball and with Host Controls off. Repeat the
owner-identity check with three members and four players.

**Current result:** automated two-client Skip **passed** on Windows with
`tests/multiplayer/fixtures/static.kolf` after Craft compile/install/qmerge;
run directory `server/local-session/online-host-skip-1790192417571`. The
owner's partial score remained, the guest stayed unscored, and both clients
applied the same next-hole state and finished. Visible confirmation, final-hole
skip, Results presentation, rolling-ball, and three-member/four-player checks:
**not run** on Windows, Skip source `3d60dd2`.

## One Slope Practise visual pass

Select **Slope Practise** in the lobby. Before starting, turn on **Hole > Show
Info** (Ctrl+I). On the first hole and after a hole transition, confirm the
slope arrows and grade are visible without another toggle and do not cover the
ball, cup, or aim. Toggle the action off and back on with both the menu and
shortcut. During each player's turn, the owner of that ball should see the
local putter; the other client should see only the live remote-aim indicator.
Watch one shot settle on the guest without a stale putter, snapback, or stalled
turn. Compare chosen colors with the lobby, balls, scoreboard, and Results.

## One bounded offline regression

Launch ordinary Kolf through Craft without an online test configuration. Start
a local game, play normal and advanced shots, save, close, reload, and compare
the current hole, turn, and scorecard. This check protects the shared game code;
it is not a broad course matrix.

Record each item as passed, failed, or not run. Include platform, date, course,
source commit, reproduction steps for failures, and the generated run-directory
name. Human input and visible-layout checks are not inferred from scripted runs.
