# Human testing for Kolf online multiplayer

Build once after source changes, then start a bounded local session from the
repository root:

```powershell
.\tests\multiplayer\play.ps1 -ClientCount 2
```

The launcher prints the endpoint. In each ordinary Kolf window, choose
**Game > Online…** and press **Connect** for the preselected endpoint.

## One bounded online smoke

1. Create a lobby in the first window and join it from the second.
2. Confirm both windows show the same members, player slots, course, and Ready
   state. Edit one locally owned slot and confirm the mutation clears readiness.
3. Ready both members and start. Confirm only the owner of the active slot can
   shoot and both clients show matching scores.
4. Exercise one normal shot, one advanced-putting shot, and one hazard choice.
5. Finish the match, compare Results, return to the lobby, and start one
   rematch with a fresh match identity.
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

## One bounded offline regression

Launch ordinary Kolf through Craft without an online test configuration. Start
a local game, play normal and advanced shots, save, close, reload, and compare
the current hole, turn, and scorecard. This check protects the shared game code;
it is not a broad course matrix.

Record each item as passed, failed, or not run. Include platform, date, course,
source commit, reproduction steps for failures, and the generated run-directory
name. Human input and visible-layout checks are not inferred from scripted runs.
