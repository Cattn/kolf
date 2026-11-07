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
| Save round trip + defaults | Every shipped course saves and re-saves identically; every object type's defaults match its schema and runtime fields | `replay/run-resave.ps1` |
| Server tests | Protocol, lobbies, matches, resume, hardening, course and object catalogs | `server/test/` |

## Full tier

Quick tier, then:

**Online scenarios** (`multiplayer/run-online-native.ts`, real Kolf clients
against a local server; [multiplayer/README.md](multiplayer/README.md)):

| Scenario | Covers |
| --- | --- |
| host-combined | Two matches: reset, undo of a guest shot (balls, objects, scores, turn), go to last/first hole, skip, Results content on both clients, an ordinary rematch with no inherited markers |
| host-undo-dynamics | Undo on a course with every built-in object; floater and windmill runtime restored |
| host-undo-channels | A plate toggles a gate; undo restores the gate on both clients |
| four-player-hazard | Three clients and four players, water drops and rehits, roster order |
| lobby-rules | Host Controls and Allow Voting set in the lobby reach every client; three members with four players |
| custom-course | Uploading a custom course and playing it twice |
| shipped-download | Guests downloading a shipped course they do not have |
| data-object | A course with a data-defined object (Ice) online |
| slope-out-of-bounds, slope-in-bounds | Slope physics online with one stroke each |
| host-proposal-approve, -reject, -four-player | Guest requests and votes |
| lobby-resume, match-resume, host-resume | Reconnecting in the lobby and mid-match, guest and host |
| guest-leaves | Continuing after a guest leaves |

**UI scripts** (`ui/*.json`, [ui/README.md](ui/README.md)): home screen and
menus, new game, settings, local results, the online hub and lobby, server
settings and notices, the HUD hazard prompt, and the course editor (palette,
history, handles, problems and Test Hole).

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
