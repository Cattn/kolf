// SPDX-License-Identifier: GPL-2.0-or-later
import { test } from 'node:test';
import assert from 'node:assert/strict';
import type { Envelope } from '../protocol/envelope.ts';
import { envelope } from '../protocol/envelope.ts';
import type { IdFactory } from '../protocol/ids.ts';
import { LobbyProtocolController } from '../service/connection.ts';

const courseHash = 'a'.repeat(64), compatibilityId = 'b'.repeat(64);
const catalog = [{ courseId: 'classic', displayName: 'Classic', expectedHash: courseHash, par: [3] }];

function ids(): IdFactory {
  let connection = 0, member = 0, lobby = 0, player = 0, match = 0;
  return {
    connection: () => `connection_${++connection}`,
    member: () => `member_${++member}`,
    lobby: () => `lobby_${++lobby}`,
    player: () => `player_${++player}`,
    match: () => `match_${++match}`,
    joinCode: () => 'ABCDEFGH',
  };
}

const send = (controller: LobbyProtocolController, connectionId: string, message: Envelope) =>
  controller.receive(connectionId, JSON.stringify(message));
const stateOf = (deliveries: ReturnType<typeof send>) => deliveries[0].message.payload.state as any;

test('two protocol clients can complete a lobby shell and start a fresh rematch', () => {
  const controller = new LobbyProtocolController(catalog, ids());
  const alice = controller.connect(), bob = controller.connect();
  const created = send(controller, alice, envelope('CreateLobby', {
    displayName: 'Alice', color: '#ff0000ff', courseId: 'classic',
  }, { requestId: 'create_1' }));
  const firstState = created[0].message.payload.state as any;
  const lobbyId = firstState.lobbyId as string, joinCode = firstState.joinCode as string;
  const joined = send(controller, bob, envelope('JoinLobby', {
    joinCode, displayName: 'Bob', color: '#0000ffff',
  }, { requestId: 'join_1' }));
  assert.equal(joined.length, 2);
  assert.equal(stateOf(joined).members.length, 2);
  assert(!('connectionId' in stateOf(joined).members[0]), 'transport identity is not exposed in lobby state');
  let revision = stateOf(joined).lobbyRevision as number;
  send(controller, alice, envelope('SetReady', { lobbyRevision: revision, ready: true }, { requestId: 'ready_a1', lobbyId }));
  const bothReady = send(controller, bob, envelope('SetReady', { lobbyRevision: revision, ready: true }, {
    requestId: 'ready_b1', lobbyId,
  }));
  assert(stateOf(bothReady).members.every((member: any) => member.ready));
  const preparing = send(controller, alice, envelope('StartMatch', { lobbyRevision: revision }, { requestId: 'start_1', lobbyId }));
  const firstMatchId = stateOf(preparing).match.matchId as string;
  send(controller, alice, envelope('CourseReady', { courseHash, compatibilityId }, {
    requestId: 'course_a1', lobbyId, matchId: firstMatchId,
  }));
  const prepared = send(controller, bob, envelope('CourseReady', { courseHash, compatibilityId }, {
    requestId: 'course_b1', lobbyId, matchId: firstMatchId,
  }));
  assert.equal(prepared[0].message.type, 'PreparationReady');
  assert.equal(controller.beginPlaying(lobbyId, firstMatchId)[0].message.type, 'MatchStarted');
  const results = controller.finish(lobbyId, firstMatchId, [[2], [3]]);
  assert.equal(results[0].message.type, 'MatchResult');
  send(controller, alice, envelope('ReturnToLobby', {}, { requestId: 'return_a1', lobbyId, matchId: firstMatchId }));
  const open = send(controller, bob, envelope('ReturnToLobby', {}, { requestId: 'return_b1', lobbyId, matchId: firstMatchId }));
  assert.equal(stateOf(open).phase, 'Open');
  revision = stateOf(open).lobbyRevision;
  send(controller, alice, envelope('SetReady', { lobbyRevision: revision, ready: true }, { requestId: 'ready_a2', lobbyId }));
  send(controller, bob, envelope('SetReady', { lobbyRevision: revision, ready: true }, { requestId: 'ready_b2', lobbyId }));
  const rematch = send(controller, alice, envelope('StartMatch', { lobbyRevision: revision }, { requestId: 'start_2', lobbyId }));
  const secondMatchId = stateOf(rematch).match.matchId as string;
  assert.notEqual(secondMatchId, firstMatchId);
  assert.deepEqual(new Set(rematch.map(delivery => delivery.connectionId)), new Set([alice, bob]), 'connections persist across matches');
  const stale = send(controller, bob, envelope('CourseReady', { courseHash, compatibilityId }, {
    requestId: 'stale_1', lobbyId, matchId: firstMatchId,
  }));
  assert.equal(stale[0].message.payload.code, 'StaleMatch');
  const reusedMatchRequest = send(controller, alice, envelope('CourseReady', { courseHash, compatibilityId }, {
    requestId: 'course_a1', lobbyId, matchId: secondMatchId,
  }));
  assert.equal(reusedMatchRequest[0].message.type, 'LobbyState', 'a rematch has a fresh request namespace');
});

test('v1 gets a clear protocol mismatch response', () => {
  const controller = new LobbyProtocolController(catalog, ids());
  const connection = controller.connect();
  const response = controller.receive(connection, JSON.stringify({ protocolVersion: 1, type: 'Hello', payload: {} }));
  assert.equal(response[0].message.type, 'UnsupportedProtocol');
  assert.equal(response[0].message.payload.supportedProtocolVersion, 2);
});
