# Kolf checks

One entry point runs everything:

```powershell
.\tests\run-checks.ps1                 # quick tier, about 20 s
.\tests\run-checks.ps1 -Tier full      # quick + online scenarios + UI scripts
.\tests\run-checks.ps1 -Tier full -Only 'online: host-*','ui: editor-*'
```

It loads the Craft environment itself and expects an installed build
(`craft --compile --install --qmerge kolf`), Node.js 22.18+ for the server and
online scenarios, and `-PythonPath` (or `CRAFT_PYTHON`) for Craft. Each check
prints pass or FAIL with its time; a failure prints the end of its output.

**When to run what:** after a change, run the one check that proves it. Run
the quick tier after larger changes and the full tier at the end of a phase or
before a release or deploy.

## Quick tier

| Check | What it covers | Details |
| --- | --- | --- |
| Unit tests (ctest) | Result summaries, server list, invite links, object registry and catalog (up to date with the code), property validation, course play policy, data-object loader and SVG sanitiser | `autotests/` |
| Golden replays | Physics of every shipped course hole: 3 fixed shots each, ball, strokes and moving objects | [replay/README.md](replay/README.md) |
| Ball collisions | A struck ball moves along the contact normal and keeps its momentum (head-on over 50 px); a ball pushed into the cup is holed and the striker plays next | `replay/run-collision.ps1` |
| Save round trip + defaults | Every shipped course saves and re-saves identically; every object type's defaults match its schema and runtime fields | `replay/run-resave.ps1` |
| Server tests | Protocol, lobbies, matches, resume, hardening, course and object catalogs | `server/test/` |

## Full tier

Quick tier, then:

**Showcase courses solvable** (`replay/run-solve.ps1`, about 45 s): the greedy
solver holes every hole of each showcase course within par + 2 shots.

**Online scenarios** (`multiplayer/run-online-native.ts`, real Kolf clients
against a local server; [multiplayer/README.md](multiplayer/README.md)):

| Scenario | Covers |
| --- | --- |
| host-combined | Two matches: reset, undo of a guest shot (balls, objects, scores, turn), go to last/first hole, skip, Results content on both clients, an ordinary rematch with no inherited markers |
| host-undo-dynamics | Undo on a course with every built-in object plus data-defined Ice; floater and windmill runtime restored; guests receive the Ice object |
| host-undo-channels | A plate toggles a gate; undo restores the gate on both clients (plain full-strength shots so the balls always leave the plate) |
| four-player-hazard | Three clients and four players, water drops and rehits, roster order; Host Controls and Allow Voting set in the lobby reach every client |
| custom-course | Uploading a custom course and playing it twice |
| shipped-download | Guests downloading a shipped course they do not have |
| slope-out-of-bounds, slope-in-bounds | Slope physics online with one stroke each |
| host-proposal-reject, -four-player | Guest requests, votes, rejection and approval |
| match-resume, host-resume | Guest reconnecting in the lobby and mid-match; host reconnecting mid-match |
| guest-leaves | Continuing after a guest leaves |
| powerups | Casual power-ups set in the lobby; the owner collects Wobble from a box and uses it on the guest with the next shot; guest power-up state matches the host |

**UI scripts** (`ui/*.json`, [ui/README.md](ui/README.md)): home screen and
menus, new game, settings, local results, the online hub and lobby, server
settings and notices, the HUD hazard prompt, and the course editor (palette,
history, handles, problems and Test Hole, all in one `editor` script).
Steps can take `"within": ms` to retry until they pass instead of sleeping
for the worst case.

### Scenarios kept but not run by default

These still work (`node tests/multiplayer/run-online-native.ts <name>`) and
are useful for narrowing down a failure, but another scenario already makes
each of their assertions:

| Scenario | Covered by |
| --- | --- |
| two-rematch | host-combined (second match is an ordinary rematch; both clients' Results compared) |
| four-player | four-player-hazard (same three-client, four-player roster checks, plus water) |
| host-reset | host-combined (scores cleared, new hole generation, exact guest state) |
| host-undo | host-undo-dynamics and host-undo-channels (the same assertions on richer courses) |
| host-skip | host-combined (partial scores kept, Results skip marker, exact guest state) |
| host-go | host-combined (go last and first, scores kept or cleared, exact guest state) |

Removed in 3j: `host-proposal-approve` (host-proposal-four-player makes the same
approve assertions), `lobby-rules` (in four-player-hazard), `lobby-resume` (in
match-resume), `data-object` (in host-undo-dynamics), and the four `editor-*`
UI scripts (merged into `editor`).
