// SPDX-License-Identifier: GPL-2.0-or-later
import { test } from 'node:test';
import assert from 'node:assert/strict';
import type { IdFactory } from '../protocol/ids.ts';
import { LobbyError } from '../service/errors.ts';
import { LobbyService } from '../service/lobby-service.ts';

const hash = (character: string) => character.repeat(64);
const catalog = [
  { courseId: 'classic', displayName: 'Classic', expectedHash: hash('a'), par: [3, 4] },
  { courseId: 'easy', displayName: 'Easy', expectedHash: hash('b'), par: [2] },
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
  const service = new LobbyService(catalog, deterministicIds(), () => now);
  const created = service.create('connection_a', 'create_1', { displayName: 'Alice', color: '#ff0000ff' }, 'classic');
  const joined = service.join('connection_b', 'join_1', created.state.joinCode, { displayName: 'Bob', color: '#0000ffff' });
  const owner = service.session('connection_a'), guest = service.session('connection_b');
  return { service, lobby: owner.lobby, owner: owner.memberId, guest: guest.memberId, created, joined, advance: (ms: number) => { now += ms; } };
}

function ready(h: ReturnType<typeof setup>) {
  const revision = h.lobby.lobbyRevision;
  h.lobby.setReady(h.owner, `owner_ready_${revision}`, revision, true);
  h.lobby.setReady(h.guest, `guest_ready_${revision}`, revision, true);
  return revision;
}

test('create, join, and full lobby keep stable member/player identities', () => {
  const h = setup();
  assert.equal(h.created.state.members.length, 1);
  assert.equal(h.joined.state.members.length, 2);
  assert.notEqual(h.created.memberId, h.joined.memberId);
  assert.notEqual(h.joined.state.members[0].playerId, h.joined.state.members[1].playerId);
  assert.throws(() => h.service.join('connection_c', 'join_2', h.created.state.joinCode, { displayName: 'Cara', color: '#00ff00ff' }),
    (error: unknown) => error instanceof LobbyError && error.code === 'LobbyFull');
});

test('owner-only settings and self-only profile updates are enforced by the domain API', () => {
  const h = setup();
  assert.throws(() => h.lobby.setCourse(h.guest, 'course_1', 'easy'),
    (error: unknown) => error instanceof LobbyError && error.code === 'NotOwner');
  h.lobby.updateMember(h.guest, 'profile_1', { displayName: 'Robert' });
  assert.equal(h.lobby.member(h.guest).displayName, 'Robert');
  assert.equal(h.lobby.member(h.owner).displayName, 'Alice');
  h.lobby.setCourse(h.owner, 'course_2', 'easy');
  assert.equal(h.lobby.state().selectedCourseId, 'easy');
});

test('match-affecting changes advance revision and clear readiness', () => {
  const h = setup(), revision = ready(h);
  assert(h.lobby.state().members.every(member => member.ready));
  h.lobby.updateMember(h.guest, 'profile_2', { color: '#00ff00ff' });
  assert.equal(h.lobby.lobbyRevision, revision + 1);
  assert(h.lobby.state().members.every(member => !member.ready));
  assert.throws(() => h.lobby.setReady(h.owner, 'stale_ready', revision, true),
    (error: unknown) => error instanceof LobbyError && error.code === 'StaleLobbyRevision');
});

test('start is atomic and duplicate requests allocate one match', () => {
  const h = setup(), revision = ready(h);
  assert.throws(() => h.lobby.start(h.guest, 'guest_start', revision),
    (error: unknown) => error instanceof LobbyError && error.code === 'NotOwner');
  const first = h.lobby.start(h.owner, 'start_1', revision);
  const retry = h.lobby.start(h.owner, 'start_1', revision);
  const duplicate = h.lobby.start(h.owner, 'start_2', revision);
  assert.equal(first.matchId, retry.matchId);
  assert.equal(first.matchId, duplicate.matchId);
  assert.equal(first.roster.length, 2);
  assert.equal(new Set(first.roster.map(player => player.playerId)).size, 2);
});

test('request IDs reject changed retries', () => {
  const h = setup(), revision = h.lobby.lobbyRevision;
  h.lobby.setReady(h.owner, 'same_request', revision, true);
  assert.throws(() => h.lobby.setReady(h.owner, 'same_request', revision, false),
    (error: unknown) => error instanceof LobbyError && error.code === 'RequestConflict');
});

test('preparation succeeds only after matching course and compatibility reports', () => {
  const h = setup(), match = h.lobby.start(h.owner, 'start_1', ready(h));
  assert.equal(h.lobby.courseReady(h.owner, 'course_ready_1', match.matchId, hash('a'), hash('c')).allReady, false);
  assert.equal(h.lobby.courseReady(h.guest, 'course_ready_2', match.matchId, hash('a'), hash('c')).allReady, true);
  h.lobby.completePreparation(match.matchId);
  assert.equal(h.lobby.phase, 'Playing');
});

test('preparation mismatch destroys the partial match and returns to open', () => {
  const h = setup(), match = h.lobby.start(h.owner, 'start_1', ready(h));
  assert.throws(() => h.lobby.courseReady(h.owner, 'course_ready_1', match.matchId, hash('f'), hash('c')),
    (error: unknown) => error instanceof LobbyError && error.code === 'CourseMismatch');
  assert.equal(h.lobby.phase, 'Open');
  assert.equal(h.lobby.currentMatchId, undefined);
  assert(h.lobby.state().members.every(member => !member.ready));
});

test('completion, results, return, and rematch use fresh identity', () => {
  const h = setup();
  const first = h.lobby.start(h.owner, 'start_1', ready(h));
  h.lobby.courseReady(h.owner, 'course_ready_1', first.matchId, hash('a'), hash('c'));
  h.lobby.courseReady(h.guest, 'course_ready_2', first.matchId, hash('a'), hash('c'));
  h.lobby.completePreparation(first.matchId);
  const result = h.lobby.completeMatch(first.matchId, [[2, 4], [3, 4]]);
  assert.equal(result.status, 'Completed');
  assert.deepEqual(result.totals, [6, 7]);
  assert.deepEqual(result.winnerPlayerIds, [first.roster[0].playerId]);
  result.scores[0][0] = 999;
  assert.equal(h.lobby.state().latestResult?.scores[0][0], 2, 'result snapshots do not mutate domain state');
  h.lobby.returnToLobby(h.owner, 'return_1', first.matchId);
  assert.equal(h.lobby.phase, 'Results');
  h.lobby.returnToLobby(h.guest, 'return_2', first.matchId);
  assert.equal(h.lobby.phase, 'Open');
  assert(h.lobby.state().members.every(member => !member.ready));
  const second = h.lobby.start(h.owner, 'start_2', ready(h));
  assert.notEqual(second.matchId, first.matchId);
  assert.throws(() => h.lobby.assertCurrentMatch(first.matchId),
    (error: unknown) => error instanceof LobbyError && error.code === 'StaleMatch');
});

test('interrupted results never fabricate a winner', () => {
  const h = setup(), match = h.lobby.start(h.owner, 'start_1', ready(h));
  h.lobby.courseReady(h.owner, 'course_ready_1', match.matchId, hash('a'), hash('c'));
  h.lobby.courseReady(h.guest, 'course_ready_2', match.matchId, hash('a'), hash('c'));
  h.lobby.completePreparation(match.matchId);
  const result = h.lobby.interruptMatch(match.matchId, 'authority disconnected', [[2], [3]]);
  assert.equal(result.status, 'Interrupted');
  assert.deepEqual(result.winnerPlayerIds, []);
  assert.equal(result.reason, 'authority disconnected');
});
