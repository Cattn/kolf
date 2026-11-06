// SPDX-License-Identifier: GPL-2.0-or-later
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { once } from 'node:events';
import type { IncomingMessage } from 'node:http';
import { fileURLToPath } from 'node:url';
import { WebSocket } from 'ws';
import type { Envelope } from '../protocol/envelope.ts';
import { envelope } from '../protocol/envelope.ts';
import { decodeClientMessage } from '../protocol/codecs.ts';
import type { IdFactory } from '../protocol/ids.ts';
import { AttemptThrottle, clientAddress, parseTrustedProxies, TokenBucket } from '../service/abuse-guard.ts';
import { LobbyProtocolController } from '../service/connection.ts';
import { loadCourseCatalog } from '../service/course-catalog.ts';
import { LobbyWebSocketService } from '../service/websocket-service.ts';

const courseRoot = fileURLToPath(new URL('../../courses/', import.meta.url));
const courseHash = 'a'.repeat(64), compatibilityId = 'b'.repeat(64);
const catalog = [{ courseId: 'classic', displayName: 'Classic', expectedHash: courseHash, par: [3] }];

function ids(): IdFactory {
  let connection = 0, member = 0, lobby = 0, player = 0, match = 0;
  return {
    connection: () => `connection_${++connection}`, member: () => `member_${++member}`, lobby: () => `lobby_${++lobby}`,
    player: () => `player_${++player}`, match: () => `match_${++match}`, joinCode: () => 'ABCDEFGH',
  };
}
const send = (controller: LobbyProtocolController, connectionId: string, message: Envelope) =>
  controller.receive(connectionId, JSON.stringify(message));

function fakeRequest(remoteAddress: string, forwardedFor?: string): IncomingMessage {
  return { socket: { remoteAddress }, headers: forwardedFor ? { 'x-forwarded-for': forwardedFor } : {} } as unknown as IncomingMessage;
}

test('client address trusts X-Forwarded-For only from configured proxies', () => {
  const proxies = parseTrustedProxies('127.0.0.1, 10.0.0.0/8, ::1');
  assert.equal(clientAddress(fakeRequest('203.0.113.9', '198.51.100.1'), proxies), '203.0.113.9');
  assert.equal(clientAddress(fakeRequest('::ffff:127.0.0.1', '198.51.100.1'), proxies), '198.51.100.1');
  assert.equal(clientAddress(fakeRequest('127.0.0.1', '6.6.6.6, 198.51.100.1, 10.1.2.3'), proxies), '198.51.100.1');
  assert.equal(clientAddress(fakeRequest('::1'), proxies), '::1');
  assert.equal(clientAddress(fakeRequest('127.0.0.1', 'garbage'), proxies), '127.0.0.1');
  assert.throws(() => parseTrustedProxies('10.0.0.0/33'));
  assert.throws(() => parseTrustedProxies('nginx'));
});

test('token bucket allows a burst, then refills at its rate', () => {
  let now = 0;
  const bucket = new TokenBucket(3, 2, () => now);
  assert.deepEqual([bucket.take(), bucket.take(), bucket.take(), bucket.take()], [true, true, true, false]);
  now = 499; assert.equal(bucket.take(), false);
  now = 500; assert.equal(bucket.take(), true);
  now = 10_000; assert.deepEqual([bucket.take(), bucket.take(), bucket.take(), bucket.take()], [true, true, true, false]);
});

test('attempt throttle forgets attempts after its window', () => {
  let now = 0;
  const throttle = new AttemptThrottle(2, 60_000, () => now);
  throttle.record('a'); throttle.record('a');
  assert.equal(throttle.blocked('a'), true); assert.equal(throttle.blocked('b'), false);
  now = 60_001; assert.equal(throttle.blocked('a'), false);
});

async function connect(url: string) {
  const socket = new WebSocket(url), messages: any[] = [];
  socket.on('message', data => messages.push(JSON.parse(data.toString())));
  socket.on('error', () => {});
  await once(socket, 'open');
  return { socket, messages };
}
async function until(check: () => boolean) {
  for (let attempt = 0; attempt < 200 && !check(); ++attempt) await new Promise(resolve => setTimeout(resolve, 10));
  assert(check());
}

test('connections per address are limited and released on close', async t => {
  const service = new LobbyWebSocketService({ catalog: loadCourseCatalog(courseRoot).slice(0, 1),
    abuse: { maximumConnectionsPerAddress: 2 } });
  await service.ready(); t.after(() => service.close());
  const first = await connect(service.endpoint()), second = await connect(service.endpoint());
  const third = await connect(service.endpoint());
  await until(() => third.messages.length > 0);
  assert.equal(third.messages[0].payload.code, 'TooManyConnections');
  await once(third.socket, 'close');
  first.socket.close(); await once(first.socket, 'close');
  await until(() => service.controller.connectionCount === 1);
  const fourth = await connect(service.endpoint());
  await until(() => fourth.messages.length > 0);
  assert.equal(fourth.messages[0].type, 'ServiceHello');
  second.socket.terminate(); fourth.socket.terminate();
});

test('wrong join codes are throttled per address', async t => {
  const service = new LobbyWebSocketService({ catalog: loadCourseCatalog(courseRoot).slice(0, 1),
    abuse: { joinFailuresPerWindow: 2 } });
  await service.ready(); t.after(() => service.close());
  const host = await connect(service.endpoint());
  host.socket.send(JSON.stringify(envelope('CreateLobby', { displayName: 'Host', colorMode: 'auto', courseId: 'classic' }, { requestId: 'c' })));
  await until(() => host.messages.some(message => message.type === 'LobbyCreated'));
  const joinCode = host.messages.find(message => message.type === 'LobbyCreated').payload.state.joinCode;
  const guest = await connect(service.endpoint());
  const join = (code: string, requestId: string) => guest.socket.send(JSON.stringify(envelope('JoinLobby',
    { joinCode: code, displayName: 'Guest', colorMode: 'auto' }, { requestId })));
  const wrong = joinCode === 'ZZZZZZZZ' ? 'YYYYYYYY' : 'ZZZZZZZZ';
  join(wrong, 'j1'); join(wrong, 'j2'); join(joinCode, 'j3');
  await until(() => guest.messages.filter(message => message.requestId?.startsWith('j')).length === 3);
  const codes = guest.messages.filter(message => message.requestId?.startsWith('j')).map(message => message.payload.code);
  assert.deepEqual(codes, ['LobbyNotFound', 'LobbyNotFound', 'TooManyAttempts']);
  host.socket.terminate(); guest.socket.terminate();
});

test('a client that keeps flooding is disconnected', async t => {
  const service = new LobbyWebSocketService({ catalog: loadCourseCatalog(courseRoot).slice(0, 1),
    abuse: { messageBurst: 5, messagesPerSecond: 1 } });
  await service.ready(); t.after(() => service.close());
  const client = await connect(service.endpoint());
  const closed = once(client.socket, 'close');
  for (let index = 0; index < 10; ++index)
    client.socket.send(JSON.stringify(envelope('LeaveLobby', {}, { requestId: `l${index}` })));
  const [code] = await closed;
  assert.equal(code, 1008);
});

test('an idle open lobby closes after 30 minutes and tells its members', () => {
  let now = 0;
  const controller = new LobbyProtocolController(catalog, ids(), () => now);
  const host = controller.connect(), guest = controller.connect();
  const created = send(controller, host, envelope('CreateLobby', { displayName: 'Host', colorMode: 'auto', courseId: 'classic' }, { requestId: 'c' }));
  const state = created[0].message.payload.state as any;
  send(controller, guest, envelope('JoinLobby', { joinCode: state.joinCode, displayName: 'Guest', colorMode: 'auto' }, { requestId: 'j' }));
  now = 20 * 60 * 1000;
  send(controller, guest, envelope('UpdateMember', { displayName: 'Guest Two' }, { requestId: 'u', lobbyId: state.lobbyId }));
  now = 31 * 60 * 1000;
  assert.deepEqual(controller.tick(), []);
  now = 50 * 60 * 1000 + 1;
  const deliveries = controller.tick();
  assert.deepEqual(deliveries.map(delivery => [delivery.connectionId, delivery.message.type]),
    [[host, 'LobbyClosed'], [guest, 'LobbyClosed']]);
  assert.match(String(deliveries[0].message.payload.reason), /30 minutes/);
  assert.equal(controller.service.activeLobbyCount, 0);
});

test('a match longer than three hours is interrupted with honest results', () => {
  let now = 0;
  const controller = new LobbyProtocolController(catalog, ids(), () => now);
  const alice = controller.connect(), bob = controller.connect();
  const created = send(controller, alice, envelope('CreateLobby', { displayName: 'Alice', colorMode: 'auto', courseId: 'classic' }, { requestId: 'c' }));
  const state = created[0].message.payload.state as any, lobbyId = state.lobbyId as string;
  send(controller, bob, envelope('JoinLobby', { joinCode: state.joinCode, displayName: 'Bob', colorMode: 'auto' }, { requestId: 'j' }));
  const revision = controller.service.activeLobby(lobbyId).lobbyRevision;
  send(controller, alice, envelope('SetReady', { lobbyRevision: revision, ready: true }, { requestId: 'ra', lobbyId }));
  send(controller, bob, envelope('SetReady', { lobbyRevision: revision, ready: true }, { requestId: 'rb', lobbyId }));
  const start = send(controller, alice, envelope('StartMatch', { lobbyRevision: revision }, { requestId: 's', lobbyId }));
  const matchId = (start[0].message.payload.state as any).match.matchId as string;
  for (const [client, requestId] of [[alice, 'ca'], [bob, 'cb']])
    send(controller, client, envelope('CourseReady', { courseHash, compatibilityId }, { requestId, lobbyId, matchId }));
  now = 3 * 60 * 60 * 1000;
  assert(!controller.tick().some(delivery => delivery.message.type === 'MatchResult'));
  now += 1;
  const results = controller.tick().filter(delivery => delivery.message.type === 'MatchResult');
  assert.equal(results.length, 2);
  const result = results[0].message.payload.result as any;
  assert.equal(result.status, 'Interrupted');
  assert.match(result.reason, /3-hour limit/);
  assert.deepEqual(result.winnerPlayerIds, []);
});

test('display names reject invisible and control characters but keep emoji', () => {
  const create = (displayName: string) => () => decodeClientMessage(JSON.stringify(envelope('CreateLobby',
    { displayName, colorMode: 'auto', courseId: 'classic' }, { requestId: 'r' })));
  for (const code of [0x07, 0x85, 0x200b, 0x202e, 0x2066, 0x2028, 0xfeff])
    assert.throws(create(`Sam${String.fromCodePoint(code)}x`), String(code));
  assert.doesNotThrow(create(`Sam ${String.fromCodePoint(0x1f468, 0x200d, 0x1f469)}`));
  assert.doesNotThrow(create('Zoë'));
});
