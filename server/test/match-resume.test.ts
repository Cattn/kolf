// SPDX-License-Identifier: GPL-2.0-or-later
import { test } from 'node:test';
import assert from 'node:assert/strict';
import type { Envelope } from '../protocol/envelope.ts';
import { envelope } from '../protocol/envelope.ts';
import { LobbyProtocolController } from '../service/connection.ts';

const courseHash = 'a'.repeat(64), compatibilityId = 'b'.repeat(64);
const catalog = [{ courseId: 'classic', displayName: 'Classic', expectedHash: courseHash, par: [3] }];
const send = (controller: LobbyProtocolController, connectionId: string, message: Envelope) =>
  controller.receive(connectionId, JSON.stringify(message));

function playingMatch() {
  let now = 1_000;
  const controller = new LobbyProtocolController(catalog, undefined, () => now);
  const host = controller.connect(), guest = controller.connect();
  const created = send(controller, host, envelope('CreateLobby', { displayName: 'Sam', colorMode: 'auto', courseId: 'classic' }, { requestId: 'c' }));
  const lobbyState = created[0].message.payload.state as any, lobbyId = lobbyState.lobbyId as string;
  const hostToken = created[0].message.payload.resumeToken as string;
  const joined = send(controller, guest, envelope('JoinLobby', { joinCode: lobbyState.joinCode, displayName: 'Ana', colorMode: 'auto' }, { requestId: 'j' }));
  const guestToken = (joined.find(delivery => delivery.connectionId === guest)!.message.payload as any).resumeToken as string;
  const revision = controller.service.activeLobby(lobbyId).lobbyRevision;
  for (const [client, requestId] of [[host, 'rh'], [guest, 'rg']])
    send(controller, client, envelope('SetReady', { lobbyRevision: revision, ready: true }, { requestId, lobbyId }));
  const start = send(controller, host, envelope('StartMatch', { lobbyRevision: revision }, { requestId: 's', lobbyId }));
  const match = (start[0].message.payload.state as any).match, matchId = match.matchId as string;
  for (const [client, requestId] of [[host, 'ch'], [guest, 'cg']])
    send(controller, client, envelope('CourseReady', { courseHash, compatibilityId }, { requestId, lobbyId, matchId }));
  for (const [client, requestId] of [[host, 'sh'], [guest, 'sg']])
    send(controller, client, envelope('SceneReady', { manifestHash: compatibilityId }, { requestId, lobbyId, matchId }));
  const initialState = {
    stateRevision: 1, holeGeneration: 1, turnId: 1, activeSlot: 0, hole: 1, par: 3,
    phase: 'AwaitingShot', manifestHash: compatibilityId, courseHash,
    balls: [0, 1].map(i => ({ id: `ball/${i}`, kind: 'sprite', x: 100, y: 200, z: 1,
      visible: true, rotation: 0, opacity: 1, sprite: 'ball', frame: -1, state: 1 })),
    objects: [], scores: [[0], [0]],
  };
  const committed = send(controller, host, envelope('InitialState', { state: initialState }, { requestId: 'i', lobbyId, matchId }));
  const syncId = committed[0].message.payload.syncId as number;
  send(controller, host, envelope('StateApplied', { stateRevision: 1, syncId, manifestHash: compatibilityId }, { requestId: 'ah', lobbyId, matchId }));
  const opened = send(controller, guest, envelope('StateApplied', { stateRevision: 1, syncId, manifestHash: compatibilityId }, { requestId: 'ag', lobbyId, matchId }));
  assert.equal((opened[0].message.payload.state as any).phase, 'Playing');
  const shot = (requestId: string) => send(controller, host, envelope('SubmitShot', {
    commandId: `shot-${requestId}`, holeGeneration: 1, turnId: 1, playerId: match.roster[0].playerId,
    puttingMode: 'normal', directionRadians: 0.5, launchMagnitude: 3,
  }, { requestId, lobbyId, matchId }));
  return { controller, host, guest, lobbyId, matchId, guestToken, hostToken, shot, advance: (ms: number) => { now += ms; } };
}

test('a guest who drops mid-match pauses it instead of ending it', () => {
  const m = playingMatch();
  const dropped = m.controller.disconnect(m.guest);
  assert.deepEqual(dropped.map(delivery => [delivery.connectionId, delivery.message.type]), [[m.host, 'LobbyState']]);
  const ana = (dropped[0].message.payload.state as any).members.find((member: any) => member.displayName === 'Ana');
  assert.equal(ana.connected, false);
  assert.equal(ana.reconnectDeadline, 1_000 + 60_000);
  const refused = m.shot('shot1');
  assert.equal(refused[0].message.type, 'CommandRejected');
  assert.equal(refused[0].message.payload.reason, 'waiting for a player to reconnect');
  m.advance(45_000);
  assert(!m.controller.tick().some(delivery => delivery.message.type === 'MatchResult'));
});

test('a resumed guest rejoins the running match and play continues', () => {
  const m = playingMatch();
  m.controller.disconnect(m.guest);
  m.advance(20_000);
  const back = m.controller.connect();
  const resumed = send(m.controller, back, envelope('ResumeSession', { resumeToken: m.guestToken }, { requestId: 'r' }));
  assert.equal(resumed[0].message.type, 'SessionResumed');
  assert.equal((resumed[0].message.payload.state as any).phase, 'Playing');
  const resync = send(m.controller, back, envelope('RequestResync', {}, { requestId: 'rs', lobbyId: m.lobbyId, matchId: m.matchId }));
  assert(resync.some(delivery => delivery.connectionId === m.host && delivery.message.type === 'RequestResync'));
  assert(!m.controller.tick().some(delivery => delivery.message.type === 'MatchResult'));
});

test('a guest who stays away for 60 s ends the match as before', () => {
  const m = playingMatch();
  m.controller.disconnect(m.guest);
  m.advance(60_001);
  const results = m.controller.tick().filter(delivery => delivery.message.type === 'MatchResult');
  assert.equal(results.length, 1);
  assert.equal((results[0].message.payload.result as any).status, 'Interrupted');
});

test('a dropped authority pauses the match and resumes with its scene intact', () => {
  const m = playingMatch();
  const dropped = m.controller.disconnect(m.host);
  assert.deepEqual(dropped.map(delivery => [delivery.connectionId, delivery.message.type]), [[m.guest, 'LobbyState']]);
  m.advance(30_000);
  assert(!m.controller.tick().some(delivery => delivery.message.type === 'MatchResult'));
  const back = m.controller.connect();
  const resumed = send(m.controller, back, envelope('ResumeSession', { resumeToken: m.hostToken }, { requestId: 'r' }));
  assert.equal(resumed[0].message.type, 'SessionResumed');
  const resync = send(m.controller, back, envelope('RequestResync', {}, { requestId: 'rs', lobbyId: m.lobbyId, matchId: m.matchId }));
  assert(resync.some(delivery => delivery.connectionId === back && delivery.message.type === 'RequestResync'));
  const accepted = m.shot('after');
  assert.notEqual(accepted[0]?.message.payload.reason, 'waiting for a player to reconnect');
});

test('an authority that stays away ends the match and the lobby', () => {
  const m = playingMatch();
  m.controller.disconnect(m.host);
  m.advance(60_001);
  const types = m.controller.tick().map(delivery => delivery.message.type);
  assert(types.includes('MatchResult'));
  assert(types.includes('LobbyClosed'));
});
