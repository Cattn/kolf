# Kolf online development

Kolf currently uses one online protocol and one multi-room service. A room has
2–8 player slots owned by 2–8 connected members. One client runs the
authoritative simulation; the others render and acknowledge its state.

For human checks, use [HUMAN-TESTING.md](HUMAN-TESTING.md). Keep validation
bounded to the scenarios listed there.

## Server checks

From `server`:

```powershell
npm test
npm run check
```

Start the development service with `npm start`. It binds to `KOLF_BIND`
(default `0.0.0.0`) and `KOLF_PORT` (default `3011`). `KOLF_COURSE_ROOT` can
override the shipped-course directory.

## Windows build and local play

Build Kolf through Craft:

```powershell
& C:\CraftRoot\craft\craftenv.ps1
craft --compile --install --qmerge kolf
```

Then launch a two-client local session from the repository root:

```powershell
.\tests\multiplayer\play.ps1 -ClientCount 2
```

Use `-ClientCount 3` for the representative three-member/four-player check.
The launcher reserves a local port, starts the service, launches ordinary Kolf
clients through Craft, and prints the endpoint and log directory. Online pages
are embedded in each ordinary `KolfWindow`; the launcher does not use a separate
online window or test-only UI path. During a match, the main window owns the
normal game area, `ScoreBoard`, settings actions, status bar, and contextual
hazard controls; the online match controller contains no widgets.

The bounded scripted scenarios are:

```powershell
cd server
npm run test:native:rematch
npm run test:native:four-player
npm run test:native:hazard
node ../tests/multiplayer/run-online-native.ts host-reset
node ../tests/multiplayer/run-online-native.ts host-undo
node ../tests/multiplayer/run-online-native.ts host-skip
node ../tests/multiplayer/run-online-native.ts host-go
node ../tests/multiplayer/run-online-native.ts slope-out-of-bounds
node ../tests/multiplayer/run-online-native.ts slope-in-bounds
```

The slope scenarios use the shipped Slope Practice first-hole geometry with
border walls disabled. They limit each player to one stroke so both owner and
guest outcomes can be checked in a short native match. The out-of-bounds case
asserts position reset, one stroke each, turn advance, and completion; the
in-bounds case asserts forward motion and settlement without snapback.
The host-reset scenario lets both players score before the owner enables
Host Controls and resets the hole. It checks the new turn/generation, cleared
scores, exact guest reconciliation, and completion after the reset.
The host-undo scenario has the guest take a shot, then the owner undoes it.
It checks pre-shot balls, objects, scores, and active turn on both clients,
exact reconciliation, and completion after the guest retries.
The host-skip scenario has the owner score once, skips with the guest still
unscored, then checks retained scores, next-hole turn order, exact guest
reconciliation, and completion.
The host-go scenario has the owner score once, uses Go Last to start the last
hole without finishing the match, then Go First to restart the earlier hole.
It checks retained source score, cleared revisited score, starter, exact guest
reconciliation, and eventual completion.

The native envelope fixture can be run by setting
`KOLF_ONLINE_PROTOCOL_TESTS` to the absolute path of
`protocol/online-envelope-fixtures.json`, then launching Kolf through Craft.

## Current protocol boundary

Every envelope carries `protocolVersion: 3`. Non-current clients receive an
`UnsupportedProtocol` response; no compatibility decoder or fallback exists.
Messages are bounded to 512 KiB. Request, lobby, and match identities scope
mutating commands. Frozen roster ownership determines who may submit each shot
or hazard choice. State barriers wait for each current member once, and guest
clients do not simulate gameplay.

The current checkpoint is
[evidence/2026-09-22-stage2-shared-game-surface.json](evidence/2026-09-22-stage2-shared-game-surface.json). Generated
`local-session` directories contain development logs and are ignored by Git.
