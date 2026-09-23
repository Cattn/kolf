# Kolf online protocol

Protocol v3 is the private-alpha room and variable-roster protocol. Every JSON
text message uses this envelope:

```json
{
  "protocolVersion": 3,
  "type": "SetReady",
  "requestId": "request_opaque",
  "lobbyId": "lobby_opaque",
  "matchId": "match_opaque_when_needed",
  "payload": {}
}
```

The service sends `ServiceHello` immediately after connection. Its payload is
the authoritative shipped-course catalog and the room/member/player/message
limits for that service. Clients use each catalog entry's stable `courseId` and
`resourceName`; they do not keep a separate course list.
The shared `courses/manifest.txt` controls both installation and the server
catalog. Each descriptor includes a source, name, author, hole count, total
par, SHA-256, and byte size. Only shipped descriptors have a resource name;
no client path appears in lobby or frozen match state.

In an open lobby, the owner can send `CourseUploadBegin` with an upload ID,
raw-byte SHA-256 and byte size, followed by ordered `CourseUploadChunk` messages
with base64 data and `CourseUploadFinish`. The service acknowledges the next
chunk index, validates the Kolf text and hash, then publishes the descriptor
in `LobbyState` and selects it. Failed uploads leave the previous selection.
The limit is 4 MiB, with 48 KiB chunks, one active upload, a 30-second idle
timeout, and three completed custom courses per lobby.

During match preparation, members request `GetCourseChunk` by the frozen
match ID, hash, and index. `CourseChunkData` supplies bounded bytes. Each
member verifies the raw hash and course groups, caches under a hash-only
filename, and sends `CourseReady` with that exact hash. The match proceeds
after every connected member confirms it. A failed transfer sends
`PreparationFailed` and returns everyone to the open lobby.

`connectionId`, `memberId`, `lobbyId`, `playerId`, `matchId`, and `requestId`
are independent opaque identities. One connection owns one lobby member. Each
member owns one or more entries in the separate `players` collection, and the
server derives ownership from the authenticated connection. A lobby freezes
the ordered 2–8-player roster at Start and assigns contiguous `engineIndex`
values only at that boundary.

Player requests use `colorMode: "auto" | "custom"` and include `customColor`
only for manual preset or custom choices. Colors use canonical `#RRGGBBAA`
channels. The server assigns each player's `resolvedColor` in roster order,
avoiding manual choices where possible. Lobby state publishes that resolved
color and Start freezes it in the match roster and Results. Duplicate manual
colors remain valid; clients warn players without rejecting their names.

Lobby mutations are `CreateLobby`, `JoinLobby`, `LeaveLobby`, `UpdateMember`,
`AddPlayer`, `UpdatePlayer`, `RemovePlayer`, `ReorderPlayers`, `SetCourse`,
`SetReady`, and `StartMatch`. Roster and course changes advance the lobby
revision and clear readiness. A member display-name change advances the revision
while retaining readiness because the frozen player roster is unchanged.
Runtime codecs bound identifiers, strings,
arrays, numeric fields, snapshots, and message bytes.

The selected authority member alone publishes authoritative states and frames.
Shots carry `playerId` and are accepted only from its owner when that player is
active. Hazard choices are authorized from the frozen owner of `choiceSlot`.
Preparation and state barriers acknowledge each connected member once,
regardless of how many player slots it owns. Frames are fanned out to every
non-authority member.

During an open shot turn, its owner may send bounded `AimUpdate` messages
(`playerId`, `stateRevision`, `syncId`, `holeGeneration`, `turnId`,
`directionRadians`, and normalized `strength`). The server validates ownership
and turn identity, limits relays to one every 50 ms, and sends ephemeral
`AimPreview` messages to other members. `AimClear` clears the overlay when a
shot is admitted or synchronization restarts. These visual messages do not
change the match state, physics, or command sequence.

On a shot that leaves the course board, the authority restores the pre-shot
position and resting ball state, charges the accepted stroke once, and advances
the turn. Guests receive the resulting committed state; their local presentation
does not decide whether a shot was out of bounds.

Host Controls currently begin with **Reset Hole**. They start off for every
new match and rematch. Only the frozen lobby owner may send `SetHostControls`
with a command ID, current revision/sync ID, and desired enabled state. The
service broadcasts `HostControlsChanged`; guests can see the state but cannot
toggle it. While enabled and input-ready, that owner may send `HostAction`
with action `resetHole`, the current revision/sync ID, and hole generation.
The server rejects guest, stale, duplicate-conflicting, disabled, or busy
requests with `HostControlRejected` and a readable reason. A valid reset
broadcasts `HostActionPending`, asks the authority to reload the current hole,
then publishes one new `AwaitingShot` revision behind the usual all-member
apply barrier. Its turn and generation both advance, all current-hole scores
return to zero, the first player starts, and the current hole's accepted-shot
and hazard-choice counts are removed from Results. A committed reset broadcasts
`HostActionNotice`. Exact retries do not apply the reset twice. Undo, Skip,
and Go actions remain disabled online until their transitions are implemented.

`MatchResult` contains the frozen course name and roster, per-hole scores and
par, ordered standings with ranks and ties, totals, relative-to-par where par
is known, holes in one and best/worst completed holes, accepted-shot and
hazard-choice counts, and elapsed match time. These counts come from accepted
commands and committed authority state. Interrupted results retain the last
committed partial scorecard and a reason, with no winner. A zero score in the
current hole is unfinished; a positive partial score is included in strokes
but is not called a completed hole until the authority marks that ball holed.

Clients using any non-current version receive `UnsupportedProtocol` with
`supportedProtocolVersion: 3`. There is no compatibility shim. Shared
generic-envelope cases are in
[`online-envelope-fixtures.json`](online-envelope-fixtures.json).
