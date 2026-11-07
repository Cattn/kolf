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

The same port serves WebSocket upgrades on `/` and `/ws`, plus plain HTTP:
`/healthz` (`ok`), `/status` (name, version, protocol, lobby and connection
counts, uptime), and `/join/CODE` (an invite page that looks the same whether
or not the lobby exists). Optional environment variables:

| Variable | Effect |
| --- | --- |
| `KOLF_SERVER_NAME` | Name shown in clients and `/status` (default `Kolf server`) |
| `KOLF_MOTD` | Message of the day in `ServiceHello`, plain text, at most 280 characters |
| `KOLF_PUBLIC_URL` | Public `https://` address used for invite links |
| `KOLF_LATEST_CLIENT_VERSION`, `KOLF_LATEST_CLIENT_URL` | Advertise a client update (both required) |
| `KOLF_MAX_LOBBIES`, `KOLF_MAX_CONNECTIONS` | Override the limits (defaults 32 and 64) |
| `KOLF_TRUST_PROXY` | Comma-separated proxy addresses or CIDR ranges whose `X-Forwarded-For` is trusted (for example `127.0.0.1` behind nginx) |

| `KOLF_DRAIN_SECONDS` | How long running matches may continue after SIGTERM (default 600); a second SIGTERM or SIGINT stops at once |
| `KOLF_LOG_DEBUG` | `1` adds client addresses and client-written reasons to logs (off by default) |

The service logs JSON lines to stdout: `service.started/draining/stopped`,
`lobby.created/closed` (with server reasons), `match.started/finished/interrupted`
(with duration and player count), `request.rejected` (request type and code),
`connection.rejected/closed`, and `error`. Logs contain random lobby and match
IDs only: no display names, join codes, or addresses unless `KOLF_LOG_DEBUG=1`.

Public-server limits: 8 connections per client address, a message rate of
40 per second with bursts of 80 (flooding disconnects), 10 wrong join codes per
address per minute, Open lobbies close after 30 minutes without activity,
matches are interrupted after 3 hours, and display names may not contain
control, format, or separator characters.

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

Scripted runs are driven by `Kolf::Testing::OnlineAutomation`
(`src/testing/onlineautomation.*`). When `KOLF_ONLINE_TEST_CONFIG` names a
config file, it creates or joins the lobby, adds players, readies, starts,
plays the scripted shots and host actions, and writes `results.json` and
`ui-state.jsonl`. It drives only the public slots and signals of
`OnlineCoordinator`, `OnlineWidget`, and `SessionController`; production
classes contain no scripted behaviour. The driver is built when the CMake
option `KOLF_TEST_AUTOMATION` is `ON` (the default). Release builds can turn it
off, which also removes the `KOLF_ONLINE_TEST_CONFIG` entry point.

Agent shells may need the environment prelude in `.agents/skills/kde-craft`
before these runs.

The bounded scripted scenarios are:

```powershell
cd server
npm run test:native:rematch
npm run test:native:four-player
npm run test:native:hazard
node ../tests/multiplayer/run-online-native.ts host-reset
node ../tests/multiplayer/run-online-native.ts host-undo
node ../tests/multiplayer/run-online-native.ts host-undo-dynamics
node ../tests/multiplayer/run-online-native.ts host-skip
node ../tests/multiplayer/run-online-native.ts host-go
node ../tests/multiplayer/run-online-native.ts host-proposal-approve
node ../tests/multiplayer/run-online-native.ts host-proposal-reject
node ../tests/multiplayer/run-online-native.ts host-proposal-four-player
node ../tests/multiplayer/run-online-native.ts host-combined
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
host-undo-dynamics runs the same checks on `fixtures/dynamics.kolf`, which has
one of every built-in object, and requires the checkpoint to carry floater and
windmill runtime state.
The host-skip scenario has the owner score once, skips with the guest still
unscored, then checks retained scores, next-hole turn order, exact guest
reconciliation, and completion.
The host-go scenario has the owner score once, uses Go Last to start the last
hole without finishing the match, then Go First to restart the earlier hole.
It checks retained source score, cleared revisited score, starter, exact guest
reconciliation, and eventual completion.
The proposal scenarios let a guest propose Reset Hole. Two-client runs check
that owner approval clears scores on both clients and owner rejection leaves
the hole unchanged. The three-member/four-player run gives the owner two
player slots, verifies one vote per member, then checks the approved reset
and exact guest reconciliation. They exercise the normal native clients
through Craft; they do not replace a visible menu and accessibility pass.
The host-combined scenario runs two matches. In the first, both players score,
the owner resets, undoes a settled guest shot, goes to the last and first holes,
then skips a partially scored hole. It checks both clients at every action,
compares their final Results content and completed-hole highlights, and finishes
an ordinary rematch with no inherited unfinished-hole marker. This is scripted
native evidence; it does not verify visual layout or keyboard focus. The
scripted clients also write `ui-state.jsonl` in each match directory. The
combined and proposal scenarios check that the owner sees host settings and
Go, while guests see status and proposal controls without owner-only actions.

During an online match, **Match controls** shows the owner **Enable Host
Controls**, **Allow Voting**, and **Confirm Host Actions**. The guest sees the
current host-control and voting status. When voting is available, a proposal
composer appears until a proposal is active; then its summary and voting row
appear. Only the owner sees Approve and Reject; only the proposer sees Withdraw.
Owner-only Hole actions and the Go menu are hidden for guests. **Confirm Host
Actions** starts on, is saved as a local preference, and can be turned off to
skip the Reset, Skip, Go, and proposal-approval confirmation dialogs. It does
not change the server's authority checks or other leave/change-server prompts.

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
