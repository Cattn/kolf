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
online window or test-only UI path.

The bounded scripted scenarios are:

```powershell
cd server
npm run test:native:rematch
npm run test:native:four-player
npm run test:native:hazard
```

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
[evidence/2026-09-22-stage1-main-window.json](evidence/2026-09-22-stage1-main-window.json). Generated
`local-session` directories contain development logs and are ignored by Git.
