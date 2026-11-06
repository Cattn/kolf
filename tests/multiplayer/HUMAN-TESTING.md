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

## Host Go navigation slice

In a two-client match, enable Host Controls and use Next, Previous, First,
Last, Random, and Switch to Hole in settled turns. Cancel a confirmation and
check that the selected hole and both scenes remain unchanged. Accept each
action and compare the hole, scene, scorecard, starter, and host notice on both
clients. On a revisited hole, check that its earlier scores and shot counts
are cleared while other visited rows remain. Go Last must start the last hole
without opening Results. Guests cannot use Go, and the actions are unavailable
while a ball rolls or Host Controls are off. Check Results labels holes left
unfinished and omits them from completed-hole highlights. Leave Online and
verify the offline Go menu and shortcuts still move only the local game.

**Current result:** automated two-client Go Last, Go First, and subsequent
replay **passed** on Windows with `tests/multiplayer/fixtures/static.kolf`
after Craft compile/install/qmerge; run directory
`server/local-session/online-host-go-1790275347446`. Both clients applied
identical states, including cleared scores on the revisited first hole, and
then finished. Visible menu, confirmations, Random, rolling-ball, Results,
offline, and three-member/four-player checks: **not run** on Windows.

## Host action proposals and voting

In a two-client match, confirm **Allow Voting** starts checked but can only
be changed by the owner while Host Controls is on. Both members should see
its current state. With controls enabled, have the guest propose Reset Hole
and vote Yes. Have the owner change a Yes vote to No; confirm the tally still
counts that member once. Reject the proposal and verify no score or scene
change. Propose again, approve after reading the score-effect confirmation,
and compare both clients' hole, scores, ball/object state, turn, and notice.
Check Undo, Skip, Random, and Switch to Hole proposals as bounded target and
wording examples. Turn voting off with a proposal active and confirm it closes.
Check withdrawal, expiry, a busy/rolling-ball attempt, keyboard focus order,
accessible names, and rematch defaults. In a three-member/four-player match,
give the owner two local players and confirm that member has only one vote.

**Current result at source `15e69b8` (Windows, 24 September 2026):** bounded
scripted native approval **passed** on `tests/multiplayer/fixtures/static.kolf`
in `server/local-session/online-host-proposal-approve-1790277661879`; both
clients applied the same cleared reset state and finished. Scripted rejection
**passed** in `online-host-proposal-reject-1790277681644`; both clients
retained the unchanged hole and finished. The three-member/four-player vote
and approval check **passed** in `online-host-proposal-four-player-1790277635392`:
each client saw one vote from each member, including one vote for the owner
with two local players, and all three reconciled to the same reset state.
Visible menu, option, confirmation, focus, accessibility, expiry, withdrawal,
busy phase, and rematch checks: **not run**. These scripted runs are not a
human visible-layout pass.

## Combined host actions and final Results

**Passed (scripted native, Windows, 24 September 2026, source `888aa25`):**
`tests/multiplayer/fixtures/static.kolf`, run directory
`server/local-session/online-host-combined-1790279027013`. In the first
two-client match both players scored, Reset cleared both scores, the owner
undid a settled guest shot, Go Last and Go First kept then cleared the
appropriate score rows, and Skip retained a partially scored first hole.
Both clients applied identical states at each action and rendered identical
final Results data and text: Hole 1 was labelled skipped, both partial scores
remained, and best/worst highlights used only completed Hole 2. Both clients
completed an ordinary rematch with no inherited skip or navigation marker.
The generated `results.json` files in each client's match directory hold the
authoritative result and rendered plain text used for comparison.

**Not run (visible acceptance):** menu/shortcut enabled state, confirmation
wording and cancel behavior, focus and accessibility, rolling-ball attempts,
disconnect recovery, and visual layout. The combined run does not replace
those human observations.

## Focused visible Host Controls pass and follow-up

**Passed (user report, Windows, 24 September 2026, source `c344cf1`):** two
ordinary Kolf clients on **Really Easy**, local endpoint
`ws://127.0.0.1:43877`, run directory `server/local-session/v3-20260924-154935`.
The user confirmed that Reset, Undo, Skip, and Go were unavailable with Host
Controls off, then usable only by the lobby owner with controls on, with no
menu or shortcut exception reported. The Reset and Skip confirmations explained
their score effects; Cancel left both games unchanged, acceptance updated both
clients, and the guest saw a notice. With voting on, the guest could propose
and vote; the owner could see the proposal, tally, expiry, score effect, and
Approve/Reject controls, and those controls were reachable and understandable
with Tab. These answers supersede the older "not run" labels for these specific
visible checks in the historical slice records above.

The user requested a way to suppress those confirmation dialogs and a cleaner
match menu that shows controls appropriate to host and guest. The follow-up
implements the local **Confirm Host Actions** preference and role-specific
**Match controls** panel described in the README. Scripted native runs passed
for owner/guest UI state, Go visibility, proposal decision visibility, match
state, and Results. The new panel's visual polish and the preference's manual
toggle have **not run** a separate human pass; the passed user report above
describes the earlier source. Rolling-ball attempts, disconnect recovery,
visible three-member/four-player owner identity, expiry and withdrawal in a
visible match, and the bounded offline save/reload check also remain **not run**.

### Phase 0 resolution of the open items above (6 November 2026)

The open items were settled by automation, or dropped where Phase 1 replaces
the UI involved. Source: `multi-proto` after `5fc70d3`, Windows, Craft
MSVC 2022, fixture `static.kolf`.

| Item | Resolution | Result |
| --- | --- | --- |
| Rolling-ball attempts | Server test *host actions are unavailable while a shot is pending, synchronizing, or rolling*. Every action family is rejected with "match is busy" and nothing else is delivered | **passed** |
| Expiry, withdrawal, and busy phase | Existing server test *proposal expiry, voting disable, busy state, and stale revision…* with a fake clock | **passed** |
| Random and other Go destinations, final-hole Skip | Server tests (*random Go…*, *owner skip… can finish the last hole*) plus the native `host-go` and `host-skip` runs. The visible Results label is dropped because Phase 1 replaces Results | **passed** |
| Rematch defaults | New `host-combined` assertions: both clients start the rematch with Host Controls off, Allow Voting on, no proposal, and controls never re-enabled | **passed** |
| Three-member/four-player owner identity | Server test with roster `[a, b, a]` plus the native `host-proposal-four-player` run. The visible panel check is dropped because Phase 1 replaces the panel | **passed** |
| Disconnect recovery during host actions | Superseded by Phase 2 reconnect/resume, which has its own native fault tests. Current behaviour (honest interruption) is unchanged | dropped |
| Match-controls panel polish and the preference toggle | Dropped because Phase 1 replaces the panel with the Match menu and request notifications | dropped |
| Offline normal/advanced shot plus save/close/reload | Still needs a person; see *One bounded offline regression* below | **not run** |

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

On 24 September 2026, the user reported that offline functions work. In this
pass, two ordinary shots and turn/score advance were observed on Windows with
Really Easy at source `96810df`. The bounded normal/advanced shot plus
save/close/reload comparison above was **not run**; no saved-game conclusion is
inferred from the shot observation.
