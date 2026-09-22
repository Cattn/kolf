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

Clients using any non-current version receive `UnsupportedProtocol` with
`supportedProtocolVersion: 3`. There is no compatibility shim. Shared
generic-envelope cases are in
[`online-envelope-fixtures.json`](online-envelope-fixtures.json).
