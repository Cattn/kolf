// SPDX-License-Identifier: GPL-2.0-or-later
import { test } from 'node:test';
import assert from 'node:assert/strict';
import type { Envelope } from '../protocol/envelope.ts';
import { envelope } from '../protocol/envelope.ts';
import { LobbyProtocolController } from '../service/connection.ts';

const catalog = [{ courseId: 'classic', displayName: 'Classic', expectedHash: 'a'.repeat(64), par: [3] }];
const send = (controller: LobbyProtocolController, connectionId: string, message: Envelope) =>
  controller.receive(connectionId, JSON.stringify(message));

function lobbyWithGuest() {
  let now = 1_000;
  const controller = new LobbyProtocolController(catalog, undefined, () => now);
  const host = controller.connect(), guest = controller.connect();
  const created = send(controller, host, envelope('CreateLobby', { displayName: 'Sam', colorMode: 'auto', courseId: 'classic' }, { requestId: 'c' }));
  const state = created[0].message.payload.state as any;
  const joined = send(controller, guest, envelope('JoinLobby', { joinCode: state.joinCode, displayName: 'Ana', colorMode: 'auto' }, { requestId: 'j' }));
  const forGuest = joined.find(delivery => delivery.connectionId === guest)!.message.payload as any;
  const forHost = joined.find(delivery => delivery.connectionId === host)!.message.payload as any;
  return {
    controller, host, guest, lobbyId: state.lobbyId as string,
    hostToken: created[0].message.payload.resumeToken as string, guestToken: forGuest.resumeToken as string,
    guestMemberId: forGuest.joinedMemberId as string, hostSawToken: 'resumeToken' in forHost,
    advance: (ms: number) => { now += ms; },
  };
}

test('each member gets its own resume token and nobody else sees it', () => {
  const h = lobbyWithGuest();
  assert.match(h.hostToken, /^[A-Za-z0-9_-]{22}$/);
  assert.match(h.guestToken, /^[A-Za-z0-9_-]{22}$/);
  assert.notEqual(h.hostToken, h.guestToken);
  assert.equal(h.hostSawToken, false);
});

test('a dropped guest is held for 120 s and resumes on a new connection', () => {
  const h = lobbyWithGuest();
  const dropped = h.controller.disconnect(h.guest);
  assert.deepEqual(dropped.map(delivery => [delivery.connectionId, delivery.message.type]), [[h.host, 'LobbyState']]);
  const member = (dropped[0].message.payload.state as any).members.find((m: any) => m.memberId === h.guestMemberId);
  assert.equal(member.connected, false);
  assert.equal(member.reconnectDeadline, 1_000 + 120_000);

  h.advance(30_000);
  const next = h.controller.connect();
  const resumed = send(h.controller, next, envelope('ResumeSession', { resumeToken: h.guestToken }, { requestId: 'r' }));
  assert.deepEqual(resumed.map(delivery => [delivery.connectionId, delivery.message.type]),
    [[next, 'SessionResumed'], [h.host, 'LobbyState']]);
  assert.equal(resumed[0].message.payload.memberId, h.guestMemberId);
  const back = (resumed[1].message.payload.state as any).members.find((m: any) => m.memberId === h.guestMemberId);
  assert.equal(back.connected, true);
  assert(!('reconnectDeadline' in back));
  const renamed = send(h.controller, next, envelope('UpdateMember', { displayName: 'Ana B' }, { requestId: 'u', lobbyId: h.lobbyId }));
  assert.equal(renamed[0].message.type, 'LobbyState');
  h.advance(200_000);
  assert(!h.controller.tick().some(delivery => delivery.message.type === 'LobbyClosed'));
  assert.equal(h.controller.service.membershipCount, 2);
});

test('resuming over a live connection replaces it, and resuming twice is harmless', () => {
  const h = lobbyWithGuest();
  const second = h.controller.connect(), third = h.controller.connect();
  assert.equal(send(h.controller, second, envelope('ResumeSession', { resumeToken: h.guestToken }, { requestId: 'r1' }))[0].message.type, 'SessionResumed');
  assert.equal(send(h.controller, second, envelope('ResumeSession', { resumeToken: h.guestToken }, { requestId: 'r2' }))[0].message.type, 'SessionResumed');
  assert.equal(send(h.controller, third, envelope('ResumeSession', { resumeToken: h.guestToken }, { requestId: 'r3' }))[0].message.type, 'SessionResumed');
  const stale = send(h.controller, h.guest, envelope('UpdateMember', { displayName: 'Old' }, { requestId: 'o', lobbyId: h.lobbyId }));
  assert.equal(stale[0].message.payload.code, 'NotMember');
  assert.deepEqual(h.controller.disconnect(h.guest), []);
  assert.equal(h.controller.service.membershipCount, 2);
});

test('wrong, malformed, and expired tokens are refused', () => {
  const h = lobbyWithGuest();
  const stranger = h.controller.connect();
  const wrong = send(h.controller, stranger, envelope('ResumeSession', { resumeToken: 'A'.repeat(22) }, { requestId: 'w' }));
  assert.equal(wrong[0].message.payload.code, 'ResumeRejected');
  const malformed = send(h.controller, stranger, envelope('ResumeSession', { resumeToken: 'short' }, { requestId: 'm' }));
  assert.equal(malformed[0].message.payload.code, 'InvalidPayload');

  h.controller.disconnect(h.guest);
  h.advance(120_001);
  const expired = h.controller.tick();
  assert.deepEqual(expired.map(delivery => [delivery.connectionId, delivery.message.type]), [[h.host, 'LobbyState']]);
  assert.equal((expired[0].message.payload.state as any).members.length, 1);
  const late = send(h.controller, stranger, envelope('ResumeSession', { resumeToken: h.guestToken }, { requestId: 'l' }));
  assert.equal(late[0].message.payload.code, 'ResumeRejected');
});

test('a dropped host keeps the lobby open, and nobody can start until everyone is back', () => {
  const h = lobbyWithGuest();
  const revision = h.controller.service.activeLobby(h.lobbyId).lobbyRevision;
  send(h.controller, h.host, envelope('SetReady', { lobbyRevision: revision, ready: true }, { requestId: 'rh', lobbyId: h.lobbyId }));
  send(h.controller, h.guest, envelope('SetReady', { lobbyRevision: revision, ready: true }, { requestId: 'rg', lobbyId: h.lobbyId }));
  const dropped = h.controller.disconnect(h.host);
  assert.deepEqual(dropped.map(delivery => delivery.message.type), ['LobbyState']);
  assert.equal(h.controller.service.activeLobbyCount, 1);

  const back = h.controller.connect();
  send(h.controller, back, envelope('ResumeSession', { resumeToken: h.hostToken }, { requestId: 'r' }));
  h.controller.disconnect(h.guest);
  const blocked = send(h.controller, back, envelope('StartMatch', { lobbyRevision: revision }, { requestId: 's', lobbyId: h.lobbyId }));
  assert.equal(blocked[0].message.payload.code, 'MemberReconnecting');

  h.advance(120_001);
  const closed = h.controller.tick();
  assert.deepEqual(closed.map(delivery => [delivery.connectionId, delivery.message.type]), [[back, 'LobbyState']]);
});

test('an expired host closes the lobby for everyone', () => {
  const h = lobbyWithGuest();
  h.controller.disconnect(h.host);
  h.advance(120_001);
  const closed = h.controller.tick();
  assert.deepEqual(closed.map(delivery => [delivery.connectionId, delivery.message.type]), [[h.guest, 'LobbyClosed']]);
  assert.equal(h.controller.service.activeLobbyCount, 0);
});
