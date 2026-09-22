// SPDX-License-Identifier: GPL-2.0-or-later
import { test } from 'node:test';
import assert from 'node:assert/strict';
import type { IdFactory } from '../protocol/ids.ts';
import { LobbyError } from '../service/errors.ts';
import { LobbyRegistry } from '../service/lobby-registry.ts';
import { AUTO_COLORS } from '../service/lobby.ts';

const hash = (character: string) => character.repeat(64);
const catalog = [
  { courseId: 'classic', displayName: 'Classic', resourceName: 'Classic.kolf', expectedHash: hash('a'), par: [3, 4] },
  { courseId: 'easy', displayName: 'Easy', resourceName: 'Easy.kolf', expectedHash: hash('b'), par: [2] },
];

function deterministicIds(): IdFactory {
  let connection = 0, member = 0, lobby = 0, player = 0, match = 0, join = 0;
  return {
    connection: () => `connection_${++connection}`,
    member: () => `member_${++member}`,
    lobby: () => `lobby_${++lobby}`,
    player: () => `player_${++player}`,
    match: () => `match_${++match}`,
    joinCode: () => `ABCDEF${++join}`,
  };
}

function setup() {
  let now = 1000;
  const service = new LobbyRegistry(catalog, deterministicIds(), () => now);
  const created = service.create('connection_a', 'create_1', { displayName: 'Alice' },
    { displayName: 'Alice', colorMode: 'custom', customColor: '#ff0000ff' }, 'classic');
  const joined = service.join('connection_b', 'join_1', created.state.joinCode, { displayName: 'Bob' },
    { displayName: 'Bob', colorMode: 'custom', customColor: '#0000ffff' });
  const owner = service.session('connection_a'), guest = service.session('connection_b');
  return { service, lobby: owner.lobby, owner: owner.memberId, guest: guest.memberId, created, joined,
    advance: (ms: number) => { now += ms; } };
}

function ready(h: ReturnType<typeof setup>) {
  const revision = h.lobby.lobbyRevision;
  h.lobby.setReady(h.owner, `owner_ready_${revision}`, revision, true);
  h.lobby.setReady(h.guest, `guest_ready_${revision}`, revision, true);
  return revision;
}

test('members and owned players are separate stable identities', () => {
  const h = setup();
  assert.equal(h.created.state.members.length, 1); assert.equal(h.created.state.players.length, 1);
  assert.equal(h.joined.state.members.length, 2); assert.equal(h.joined.state.players.length, 2);
  assert.notEqual(h.created.memberId, h.joined.memberId);
  assert.deepEqual(h.joined.state.players.map(player => player.ownerMemberId), [h.owner, h.guest]);
  assert(h.joined.state.members.every(member => !('playerId' in member)));
});

test('owners manage only their own slots and every member keeps one', () => {
  const h = setup();
  const added = h.lobby.addPlayer(h.owner, 'add_1', { displayName: 'Alice 2', colorMode: 'custom', customColor: '#00ff00ff' });
  assert.equal(added.state.players.length, 3);
  assert.equal(added.state.players.filter(player => player.ownerMemberId === h.owner).length, 2);
  assert.throws(() => h.lobby.updatePlayer(h.guest, 'steal_1', added.playerId, { displayName: 'Stolen' }),
    (error: unknown) => error instanceof LobbyError && error.code === 'NotPlayerOwner');
  const guestPlayer = h.lobby.state().players.find(player => player.ownerMemberId === h.guest)!;
  assert.throws(() => h.lobby.removePlayer(h.guest, 'remove_last', guestPlayer.playerId),
    (error: unknown) => error instanceof LobbyError && error.code === 'LastPlayer');
  h.lobby.removePlayer(h.owner, 'remove_2', added.playerId);
  assert.equal(h.lobby.playerCount, 2);
});

test('roster mutations clear readiness and stale revisions fail', () => {
  const h = setup(), revision = ready(h);
  assert(h.lobby.state().members.every(member => member.ready));
  const ownerPlayer = h.lobby.state().players.find(player => player.ownerMemberId === h.owner)!;
  h.lobby.updatePlayer(h.owner, 'profile_2', ownerPlayer.playerId, { colorMode: 'custom', customColor: '#00ff00ff' });
  assert.equal(h.lobby.lobbyRevision, revision + 1);
  assert(h.lobby.state().members.every(member => !member.ready));
  assert.throws(() => h.lobby.setReady(h.owner, 'stale_ready', revision, true),
    (error: unknown) => error instanceof LobbyError && error.code === 'StaleLobbyRevision');
});

test('start freezes a deterministic multi-slot roster and request retries are idempotent', () => {
  const h = setup();
  h.lobby.addPlayer(h.owner, 'add_1', { displayName: 'Alice 2', colorMode: 'custom', customColor: '#00ff00ff' });
  h.lobby.addPlayer(h.guest, 'add_2', { displayName: 'Bob 2', colorMode: 'custom', customColor: '#ff00ffff' });
  const revision = ready(h);
  const first = h.lobby.start(h.owner, 'start_1', revision);
  const retry = h.lobby.start(h.owner, 'start_1', revision);
  assert.equal(first.matchId, retry.matchId); assert.equal(first.roster.length, 4);
  assert.deepEqual(first.roster.map(player => player.engineIndex), [0, 1, 2, 3]);
  assert.equal(new Set(first.roster.map(player => player.playerId)).size, 4);
});

test('preparation, completion, results, and rematch use every member and player', () => {
  const h = setup();
  h.lobby.addPlayer(h.owner, 'add_1', { displayName: 'Alice 2', colorMode: 'custom', customColor: '#00ff00ff' });
  const first = h.lobby.start(h.owner, 'start_1', ready(h));
  assert.equal(h.lobby.courseReady(h.owner, 'course_ready_1', first.matchId, hash('a'), hash('c')).allReady, false);
  assert.equal(h.lobby.courseReady(h.guest, 'course_ready_2', first.matchId, hash('a'), hash('c')).allReady, true);
  h.lobby.completePreparation(first.matchId);
  const result = h.lobby.completeMatch(first.matchId, [[2, 4], [3, 4], [2, 3]]);
  assert.deepEqual(result.totals, [6, 7, 5]);
  assert.deepEqual(result.winnerPlayerIds, [first.roster[2].playerId]);
  h.lobby.returnToLobby(h.owner, 'return_1', first.matchId);
  h.lobby.returnToLobby(h.guest, 'return_2', first.matchId);
  const second = h.lobby.start(h.owner, 'start_2', ready(h));
  assert.notEqual(second.matchId, first.matchId);
  assert.throws(() => h.lobby.assertCurrentMatch(first.matchId),
    (error: unknown) => error instanceof LobbyError && error.code === 'StaleMatch');
});

test('registry hosts isolated rooms, enforces capacity, and releases indexes on owner departure', () => {
  const ids = deterministicIds();
  const service = new LobbyRegistry(catalog, ids, () => 1, { maximumLobbies: 2 });
  const first = service.create('connection_a', 'create_a', { displayName: 'A' }, { displayName: 'A', colorMode: 'custom', customColor: '#ff0000ff' }, 'classic');
  const second = service.create('connection_b', 'create_b', { displayName: 'B' }, { displayName: 'B', colorMode: 'custom', customColor: '#00ff00ff' }, 'easy');
  assert.notEqual(first.state.lobbyId, second.state.lobbyId); assert.notEqual(first.state.joinCode, second.state.joinCode);
  assert.equal(service.activeLobbyCount, 2);
  assert.throws(() => service.create('connection_c', 'create_c', { displayName: 'C' }, { displayName: 'C', colorMode: 'custom', customColor: '#0000ffff' }, 'classic'),
    (error: unknown) => error instanceof LobbyError && error.code === 'ServiceFull');
  const departure = service.disconnect('connection_a');
  assert.equal(departure?.closed, true); assert.equal(service.activeLobbyCount, 1); assert.equal(service.membershipCount, 1);
  const replacement = service.create('connection_c', 'create_c2', { displayName: 'C' }, { displayName: 'C', colorMode: 'custom', customColor: '#0000ffff' }, 'classic');
  assert.equal(service.activeLobbyCount, 2); assert.notEqual(replacement.state.joinCode, second.state.joinCode);
});

test('non-owner departure removes only that member and its slots', () => {
  const h = setup();
  h.lobby.addPlayer(h.guest, 'add_guest', { displayName: 'Bob 2', colorMode: 'custom', customColor: '#00ffffff' });
  const departure = h.service.disconnect('connection_b')!;
  assert.equal(departure.closed, false); assert.equal(departure.state?.members.length, 1);
  assert.equal(departure.state?.players.length, 1);
  assert.equal(h.service.activeLobbyCount, 1);
});

test('auto colors avoid manual choices and frozen match colors retain exact RGBA', () => {
  const h = setup();
  const first = h.lobby.state().players[0];
  h.lobby.updatePlayer(h.owner, 'auto_owner', first.playerId, { colorMode: 'auto' });
  const guest = h.lobby.state().players[1];
  h.lobby.updatePlayer(h.guest, 'manual_guest', guest.playerId,
    { colorMode: 'custom', customColor: AUTO_COLORS[0].toUpperCase().replace('FF', '80') });
  const added = h.lobby.addPlayer(h.owner, 'auto_second', { displayName: 'A2', colorMode: 'auto' });
  const colors = added.state.players.map(player => player.resolvedColor);
  assert.equal(added.state.players[1].resolvedColor, '#0072b280');
  assert.equal(new Set(colors).size, 3);
  const match = h.lobby.start(h.owner, 'start_color', ready(h));
  assert.deepEqual(match.roster.map(player => player.resolvedColor), colors);
  assert.equal(match.roster[1].customColor, '#0072B280');
});

test('member display-name changes preserve readiness because the match roster is unchanged', () => {
  const h = setup();
  ready(h);
  h.lobby.updateMember(h.owner, 'rename_member', { displayName: 'Alice renamed' });
  assert(h.lobby.state().members.every(member => member.ready));
});

test('equivalent color spelling does not clear readiness', () => {
  const h = setup();
  const revision = ready(h);
  const owner = h.lobby.state().players[0];
  h.lobby.updatePlayer(h.owner, 'same_color_case', owner.playerId,
    { colorMode: 'custom', customColor: '#FF0000FF' });
  assert.equal(h.lobby.lobbyRevision, revision);
  assert(h.lobby.state().members.every(member => member.ready));
});
