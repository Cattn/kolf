# Two-client development prototype

One native client owns physics; the other presents authoritative snapshots. A
TypeScript WebSocket relay admits commands for exactly two fixed player slots,
orders transitions, and waits for both clients to apply each committed state.
This is the development milestone before a lobby, persistent service, or release.

For a person playing and checking this build, follow
[HUMAN-TESTING.md](HUMAN-TESTING.md). It gives the exact launch steps, controls,
checks to perform, and a report template. The status and evidence sections below
distinguish scripted runs from play by a person.

## Build and launch on Windows

Requires the existing Craft Kolf environment, Qt Network/WebSockets, and Node
22.18 or newer. Initialize Craft and run the command in the same Windows
PowerShell process:

```powershell
& C:\CraftRoot\craft\craftenv.ps1
craft --compile --install --qmerge kolf
```

To play a shipped course on one PC, build as above, then from the repository
root run `./tests/multiplayer/play.ps1` (defaults to Easy). Use `-Course Medium`,
`-Course Hard`, or `-Course Classic` for the other bundled maps.

For a fixture map or a manual launch, in `server/prototype` run `npm ci` once,
then:

```powershell
$env:KOLF_COURSE = (Resolve-Path ../../tests/multiplayer/fixtures/static.kolf).Path
npm start
```

The relay writes private role credentials and launch settings to
`server/prototype/local-session/{authority,guest}.json`. From the repository root,
run each of these in a separate Windows PowerShell window:

```powershell
./tests/multiplayer/launch.ps1 -Config ./server/prototype/local-session/authority.json
./tests/multiplayer/launch.ps1 -Config ./server/prototype/local-session/guest.json
```

The launcher accepts `-CraftRootPath` and `-PythonPath`; its defaults match the
development machine. Change the Python path on another machine. Without
`KOLF_PROTOTYPE_CONFIG`, normal `craft --run kolf` opens offline Kolf. The prototype
has a separate window, scorecard, resync button, putting options, and hazard choices.
It does not write offline saves or submit the legacy data-server telemetry.

For two machines, use matching source/build identities and byte-identical course
files. Set `KOLF_BIND` to the relay's LAN interface before starting it, then edit
each generated config's endpoint, local course path, and log directory. Transfer
only that player's credential privately. This development relay uses plaintext
WebSockets by default and is intended for a trusted LAN; internet deployment and
authentication accounts are outside this milestone. A disconnect ends the match;
restart the relay for a fresh session. There is no reconnect/migration protocol.

## Protocol and implementation

Messages carry version 1 and match ID `prototype`. Hello compares credentials,
course SHA-256, and the generated native source fingerprint. Course readiness
compares stable object manifests for source group IDs and semantic child IDs.
Shot commands contain a unique command ID, hole generation, turn, owner slot,
putting mode, radians, and canonical launch magnitude. Identical retries are
idempotent; conflicting IDs, stale turns, nonfinite numbers, and wrong owners fail.

Only the authority applies accepted commands through the shared offline/online
shot seam. Hazard resolution suspends for the affected player's drop/rehit choice.
Committed states include scores, turn, phase, hole, and full visual/ball state.
Guests validate snapshots before applying them and do not run game physics,
collision callbacks, or gameplay random choices. Bounded interpolation smooths
compatible motion; discontinuities and committed states snap. Full resync clears
presentation history without replaying a shot. Queues, message sizes, retries,
heartbeats, and outstanding transitions are bounded.

The source fingerprint hashes source bytes: different checkout line endings can
reject an otherwise equivalent build. Snapshot resync restores presentation, not
an authority's hidden physics state; authority migration is not supported.

Each committed state and full resync now has a separate `syncId`. State
acknowledgements, visual frames, input-ready notifications, and hazard choices
must belong to the current round. A same-revision resync clears earlier
acknowledgements and does not reopen input before its full state is applied.

## Repeatable checks

```powershell
cd server/prototype
npm test
npm run check
cd ../..
node tests/multiplayer/run-native.ts static
node tests/multiplayer/run-native.ts water rehit
node tests/multiplayer/run-native.ts water drop
node tests/multiplayer/run-native.ts water-slope drop
node tests/multiplayer/run-native.ts water-bumper drop
node tests/multiplayer/run-native.ts water-cup drop
node tests/multiplayer/run-native.ts water-second-water drop
node tests/multiplayer/run-native.ts water-stationary drop
node tests/multiplayer/run-native.ts teleport
node tests/multiplayer/run-native.ts dynamics
$env:KOLF_DELAY_MS = '100'
node tests/multiplayer/run-native.ts teleport rehit resync
Remove-Item Env:KOLF_DELAY_MS
node tests/multiplayer/run-native.ts teleport rehit disconnect
```

Each native run has a 120-second deadline and launches both clients through Craft.
The runner chooses a free localhost port unless `KOLF_PORT` is set. It verifies
both native processes exit and both scenes are destroyed, then stops only its own
remaining Craft launchers. The delay is applied on each
relay receive/send leg, not an estimate of network RTT. Native golden decoder
fixtures can be run by setting `KOLF_PROTOCOL_TESTS` to the absolute path of
`protocol/shot-fixtures.json`, then initializing Craft and using `craft --run kolf`.
Unset the variable afterward.

Ignored `local-session` directories retain private configs, JSONL traces, PNGs,
and result summaries. Tests compare guest-applied scene state with authoritative
commits and verify zero guest simulation counters. The scripts launch canonical
shots directly; alternating mode fields do not test human power-meter timing.

## Verification on 2026-09-18

Craft compile/install/qmerge passed with MSVC 2022 and Qt 6.11.1. All 12 relay
tests (including 19 shared shot cases) and TypeScript checking passed. The native
golden-fixture Craft launch returned success. Earlier native runs completed the
two-hole static fixture (scores 1,1 per player), water rehit, teleport, and the
dynamic-object fixture. The final bounded pass completed water drop, teleport
with delayed mid-motion resync, and terminal mid-motion disconnect. Both clients
logged scene destruction in these final runs. Sanitized evidence is in `evidence/`.

Remaining acceptance work: interactive mouse/keyboard and advanced-meter checks,
offline gameplay regression, cross-platform/two-machine testing, repeated hazard
and moving-obstacle edge cases, disconnects in every phase, and resource/endurance
measurement. The requested 30-minute endurance run was intentionally skipped.
The dynamics fixture includes every built-in object type but does not prove every
collision interaction. Online hazard placement now checks collisions at the
chosen point and resumes simulation when a slope starts motion. The
`water-slope` fixture exercises this path; other overlaps and full offline
parity still need targeted checks. This is a tested prototype, not a
claim that all original acceptance criteria or the larger multiplayer plan are done.

## Continuation evidence, 2026-09-18

The checks in [evidence/2026-09-18-p0.json](evidence/2026-09-18-p0.json)
were run after the changes above; the earlier evidence file remains unchanged.
Craft compile/install/qmerge passed. All 15 relay tests and TypeScript checking
passed. Native `water-slope drop` and delayed `teleport rehit resync` completed
with matching committed states, zero guest mutation counters, and clean native
scene/process shutdown. The native runner still uses scripted canonical shots;
human mouse/keyboard and advanced-meter coverage remains open. The full lobby,
multi-slot, results/rematch, test-kit and cross-platform goals remain open.

## Sync-round follow-up, 2026-09-18

The relay now coalesces resync requests while a full state is pending and
queues an immediate retry for one second so the requester receives a fresh
round. A newer committed state supersedes a queued or pending resync. The relay
suite has 18 passing tests, including close requests, stale acknowledgements,
a commit during resync, a queued retry, and a late full state. TypeScript
checking passed. Native `water-slope drop` and `teleport rehit resync`
completed through Craft using the installed build; their committed states
matched, the guests performed no gameplay simulation, and both scenes were
destroyed. See [evidence/2026-09-18-sync-followup.json](evidence/2026-09-18-sync-followup.json).

The native runner now reports a Craft launcher failure during the run instead
of waiting for the match timeout. Craft compilation was not needed for these
TypeScript and test-runner changes. This does not close P0: actual mouse and
keyboard normal/advanced putting, offline save/load and multi-hole behavior,
and the remaining hazard overlaps still require checks.

## Hazard-placement follow-up, 2026-09-19

Drop placement now uses the same center-in-puddle predicate as the actual water
collision rule in both offline and online code. This prevents a placement that
looks clear from becoming a delayed water collision when the next shot begins.
The authority also clears a stale legacy `inPlay` flag only when no online shot
is active, and rejected admitted shots now report the concrete engine reason.

Four focused fixtures cover a bumper that restarts motion, immediate cup entry,
two adjacent water hazards, and stationary sand. Their runner assertions check
the first committed continuation, unchanged stroke totals during placement,
the intended motion/holed/stationary outcome, and final clearance of both water
hazards. All four completed with matching authority/guest commits, zero guest
simulation counters, clean scene/process teardown, and the existing slope and
ordinary-water cases still passed. See
[evidence/2026-09-19-placement.json](evidence/2026-09-19-placement.json).

These scripted cases close the automated placement-fixture portion of Stage A.
They do not replace the human normal/advanced input checklist or the offline
multi-hole save/load regression, which remain unverified.

## Protocol v2 lobby foundation, 2026-09-19

The incremental v2 service can now be started from `server/prototype` with:

```powershell
npm run start:v2
```

It loads an allowlist of shipped courses, listens on `KOLF_PORT` (3011 by
default), and accepts runtime-validated Create, Join, profile/course, Ready,
Start, CourseReady, preparation-failure, and Return requests. The pure lobby
domain enforces two remote members, owner permissions, revision-bound readiness,
bounded idempotency, fresh rematch IDs, stale-match rejection, immutable results,
and interrupted results without a fabricated winner. A v1 client receives an
`UnsupportedProtocol` response before the service closes that connection.

`npm test` covers the domain through both in-process protocol clients and real
WebSocket clients. `KOLF_PROTOCOL_V2_TESTS` runs the shared generic-envelope
fixture through the native decoder. Ordinary Kolf now exposes **Game > Online…**
for human Connect/Create/Join/Ready/Start preparation against this service; see
`HUMAN-TESTING.md`. The original `npm start` relay and generated prototype
configs remain the v1 gameplay path until the match-session migration is
complete, so the v2 lobby does not open a playable scene yet.
