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

test('prepared v2 clients cross the scene and initial-state barriers', () => {
  const controller = new LobbyProtocolController(catalog, ids());
  const alice = controller.connect(), bob = controller.connect();
  const created = send(controller, alice, envelope('CreateLobby', {
    displayName: 'Alice', color: '#ff0000ff', courseId: 'classic',
  }, { requestId: 'create_scene' }));
  const createdState = stateOf(created), lobbyId = createdState.lobbyId as string;
  send(controller, bob, envelope('JoinLobby', {
    joinCode: createdState.joinCode, displayName: 'Bob', color: '#0000ffff',
  }, { requestId: 'join_scene' }));
  const revision = controller.service.activeLobby(lobbyId).lobbyRevision;
  send(controller, alice, envelope('SetReady', { lobbyRevision: revision, ready: true }, { requestId: 'ready_scene_a', lobbyId }));
  send(controller, bob, envelope('SetReady', { lobbyRevision: revision, ready: true }, { requestId: 'ready_scene_b', lobbyId }));
  const start = send(controller, alice, envelope('StartMatch', { lobbyRevision: revision }, { requestId: 'start_scene', lobbyId }));
  const state = stateOf(start), matchId = state.match.matchId as string;
  const playerIds = state.match.roster.map((player: any) => player.playerId);
  send(controller, alice, envelope('CourseReady', { courseHash, compatibilityId }, {
    requestId: 'course_scene_a', lobbyId, matchId,
  }));
  const prepared = send(controller, bob, envelope('CourseReady', { courseHash, compatibilityId }, {
    requestId: 'course_scene_b', lobbyId, matchId,
  }));
  assert.deepEqual(prepared.map(delivery => delivery.message.type),
    ['PreparationReady', 'PreparationReady', 'Welcome', 'Welcome', 'LoadCourse', 'LoadCourse']);
  send(controller, alice, envelope('SceneReady', { manifestHash: compatibilityId }, {
    requestId: 'scene_a', lobbyId, matchId,
  }));
  const begin = send(controller, bob, envelope('SceneReady', { manifestHash: compatibilityId }, {
    requestId: 'scene_b', lobbyId, matchId,
  }));
  assert.equal(begin[0].connectionId, alice);
  assert.equal(begin[0].message.type, 'StartMatch');
  const initialState = {
    stateRevision: 1, holeGeneration: 1, turnId: 1, activeSlot: 0, hole: 1, par: 3,
    phase: 'AwaitingShot', manifestHash: compatibilityId, courseHash,
    balls: [0, 1].map(i => ({ id: `ball/${i}`, kind: 'sprite', x: 100, y: 200, z: 1,
      visible: true, rotation: 0, opacity: 1, sprite: 'ball', frame: -1, state: 1 })),
    objects: [], scores: [[0], [0]],
  };
  const committed = send(controller, alice, envelope('InitialState', { state: initialState }, {
    requestId: 'initial_scene', lobbyId, matchId,
  }));
  assert(committed.every(delivery => delivery.message.type === 'TransitionCommitted'));
  const syncId = committed[0].message.payload.syncId as number;
  send(controller, alice, envelope('StateApplied', {
    stateRevision: 1, syncId, manifestHash: compatibilityId,
  }, { requestId: 'applied_scene_a', lobbyId, matchId }));
  const opened = send(controller, bob, envelope('StateApplied', {
    stateRevision: 1, syncId, manifestHash: compatibilityId,
  }, { requestId: 'applied_scene_b', lobbyId, matchId }));
  assert.equal(opened[0].message.type, 'MatchStarted');
  assert.equal((opened[0].message.payload.state as any).phase, 'Playing');
  assert.deepEqual(opened.slice(2).map(delivery => delivery.message.type), ['InputReady', 'InputReady']);
  assert.deepEqual(new Set(playerIds), new Set((opened[0].message.payload.state as any).match.roster.map((p: any) => p.playerId)));
});

test('disconnect during a live match does not crash tick', () => {
  const controller = new LobbyProtocolController(catalog, ids());
  const alice = controller.connect(), bob = controller.connect();
  const created = send(controller, alice, envelope('CreateLobby', {
    displayName: 'Alice', color: '#ff0000ff', courseId: 'classic',
  }, { requestId: 'create_live' }));
  const createdState = stateOf(created), lobbyId = createdState.lobbyId as string;
  send(controller, bob, envelope('JoinLobby', {
    joinCode: createdState.joinCode, displayName: 'Bob', color: '#0000ffff',
  }, { requestId: 'join_live' }));
  const revision = controller.service.activeLobby(lobbyId).lobbyRevision;
  send(controller, alice, envelope('SetReady', { lobbyRevision: revision, ready: true }, { requestId: 'ready_live_a', lobbyId }));
  send(controller, bob, envelope('SetReady', { lobbyRevision: revision, ready: true }, { requestId: 'ready_live_b', lobbyId }));
  const start = send(controller, alice, envelope('StartMatch', { lobbyRevision: revision }, { requestId: 'start_live', lobbyId }));
  const matchId = stateOf(start).match.matchId as string;
  send(controller, alice, envelope('CourseReady', { courseHash, compatibilityId }, {
    requestId: 'course_live_a', lobbyId, matchId,
  }));
  send(controller, bob, envelope('CourseReady', { courseHash, compatibilityId }, {
    requestId: 'course_live_b', lobbyId, matchId,
  }));
  const closed = controller.disconnect(alice);
  assert.equal(closed[0].message.type, 'LobbyClosed');
  assert.doesNotThrow(() => controller.tick());
  controller.disconnect(bob);
  assert.doesNotThrow(() => controller.tick());
});
