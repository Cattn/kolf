// SPDX-License-Identifier: GPL-2.0-or-later
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { once } from 'node:events';
import { fileURLToPath } from 'node:url';
import { WebSocket } from 'ws';
import { envelope } from '../protocol/envelope.ts';
import { courseHash, loadCourseCatalog } from '../service/course-catalog.ts';
import { LobbyWebSocketService } from '../service/websocket-service.ts';

const courseRoot = fileURLToPath(new URL('../../courses/', import.meta.url));

async function opened(url: string) {
  const socket = new WebSocket(url);
  await once(socket, 'open');
  return socket;
}
async function nextMessage(socket: WebSocket): Promise<any> {
  const [data] = await once(socket, 'message');
  return JSON.parse(data.toString());
}
async function openedWithHello(url: string) {
  const socket = new WebSocket(url), helloMessage = nextMessage(socket);
  await once(socket, 'open');
  return { socket, hello: await helloMessage };
}

test('shipped course catalog hashes allowlisted files with platform-neutral line endings', () => {
  const catalog = loadCourseCatalog(courseRoot);
  assert.deepEqual(catalog.map(course => course.courseId), ['classic', 'easy', 'practice']);
  assert(catalog.every(course => /^[a-f0-9]{64}$/.test(course.expectedHash)));
  assert.equal(courseHash(Buffer.from("[0-course]\r\nName=Classic\r\n")),
    courseHash(Buffer.from("[0-course]\nName=Classic\n")));
  assert.throws(() => loadCourseCatalog(courseRoot, [{ courseId: 'escape', displayName: 'Escape', fileName: '../intro' }]));
});

test('new connections receive the v3 course catalog and service limits', async t => {
  const service = new LobbyWebSocketService({ catalog: loadCourseCatalog(courseRoot).slice(0, 2),
    limits: { maximumLobbies: 4, maximumPlayersPerLobby: 8 } });
  await service.ready(); t.after(() => service.close());
  const { socket, hello } = await openedWithHello(service.endpoint()); t.after(() => socket.terminate());
  assert.equal(hello.type, 'ServiceHello'); assert.equal(hello.payload.protocolVersion, 3);
  assert.deepEqual(hello.payload.courses.map((course: any) => course.courseId), ['classic', 'easy']);
  assert.equal(hello.payload.limits.maximumLobbies, 4); assert.equal(hello.payload.limits.maximumPlayersPerLobby, 8);
  assert(!('expectedHash' in hello.payload.courses[0]));
});

test('two remote WebSocket clients create and join a v3 lobby', async t => {
  const catalog = loadCourseCatalog(courseRoot).slice(0, 1);
  const service = new LobbyWebSocketService({ catalog });
  await service.ready();
  t.after(() => service.close());
  const alice = await opened(service.endpoint()), bob = await opened(service.endpoint());
  t.after(() => { alice.terminate(); bob.terminate(); });

  const createdMessage = nextMessage(alice);
  alice.send(JSON.stringify(envelope('CreateLobby', {
    displayName: 'Alice', color: '#ff0000ff', courseId: 'classic',
  }, { requestId: 'create_1' })));
  const created = await createdMessage;
  assert.equal(created.type, 'LobbyCreated');
  const state = created.payload.state;

  const aliceJoined = nextMessage(alice), bobJoined = nextMessage(bob);
  bob.send(JSON.stringify(envelope('JoinLobby', {
    joinCode: state.joinCode, displayName: 'Bob', color: '#0000ffff',
  }, { requestId: 'join_1' })));
  const [forAlice, forBob] = await Promise.all([aliceJoined, bobJoined]);
  assert.equal(forAlice.type, 'LobbyState');
  assert.equal(forBob.payload.state.members.length, 2);
  assert.equal(forBob.payload.state.lobbyId, state.lobbyId);
});

test('all-interface bind advertises a hostname clients can open', async t => {
  const service = new LobbyWebSocketService({ host: '0.0.0.0', port: 0, catalog: loadCourseCatalog(courseRoot).slice(0, 1) });
  await service.ready();
  t.after(() => service.close());
  const url = new URL(service.endpoint());
  assert.notEqual(url.hostname, '0.0.0.0');
  const socket = await opened(service.endpoint());
  t.after(() => socket.terminate());
});

test('two rooms accept interleaved roster and start traffic without cross-room leakage', async t => {
  const service = new LobbyWebSocketService({ catalog: loadCourseCatalog(courseRoot).slice(0, 1) });
  await service.ready(); t.after(() => service.close());
  const sockets = await Promise.all(Array.from({ length: 4 }, () => opened(service.endpoint())));
  const [a, b, c, d] = sockets; t.after(() => sockets.forEach(socket => socket.terminate()));

  const createdA = nextMessage(a), createdC = nextMessage(c);
  a.send(JSON.stringify(envelope('CreateLobby', { displayName: 'A', color: '#ff0000ff', courseId: 'classic' }, { requestId: 'create_a' })));
  c.send(JSON.stringify(envelope('CreateLobby', { displayName: 'C', color: '#00ff00ff', courseId: 'classic' }, { requestId: 'create_c' })));
  const [roomA, roomC] = (await Promise.all([createdA, createdC])).map(message => message.payload.state);
  assert.notEqual(roomA.lobbyId, roomC.lobbyId);

  const joinA1 = nextMessage(a), joinA2 = nextMessage(b), joinC1 = nextMessage(c), joinC2 = nextMessage(d);
  b.send(JSON.stringify(envelope('JoinLobby', { joinCode: roomA.joinCode, displayName: 'B', color: '#0000ffff' }, { requestId: 'join_b' })));
  d.send(JSON.stringify(envelope('JoinLobby', { joinCode: roomC.joinCode, displayName: 'D', color: '#ffff00ff' }, { requestId: 'join_d' })));
  const joined = await Promise.all([joinA1, joinA2, joinC1, joinC2]);
  assert(joined.slice(0, 2).every(message => message.payload.state.lobbyId === roomA.lobbyId));
  assert(joined.slice(2).every(message => message.payload.state.lobbyId === roomC.lobbyId));

  const addA1 = nextMessage(a), addA2 = nextMessage(b);
  a.send(JSON.stringify(envelope('AddPlayer', { displayName: 'A2', color: '#ff00ffff' },
    { requestId: 'add_a2', lobbyId: roomA.lobbyId })));
  const added = await Promise.all([addA1, addA2]);
  assert(added.every(message => message.payload.state.players.length === 3));
  assert(added.every(message => message.payload.state.lobbyId === roomA.lobbyId));

  async function setReady(actor: WebSocket, peer: WebSocket, lobbyId: string, revision: number, requestId: string) {
    const first = nextMessage(actor), second = nextMessage(peer);
    actor.send(JSON.stringify(envelope('SetReady', { lobbyRevision: revision, ready: true }, { requestId, lobbyId })));
    return Promise.all([first, second]);
  }
  const revisionA = added[0].payload.state.lobbyRevision;
  await setReady(a, b, roomA.lobbyId, revisionA, 'ready_a');
  const readyAB = await setReady(b, a, roomA.lobbyId, revisionA, 'ready_b');
  const revisionC = joined[2].payload.state.lobbyRevision;
  await setReady(c, d, roomC.lobbyId, revisionC, 'ready_c');
  const readyCD = await setReady(d, c, roomC.lobbyId, revisionC, 'ready_d');

  const startA1 = nextMessage(a), startA2 = nextMessage(b), startC1 = nextMessage(c), startC2 = nextMessage(d);
  a.send(JSON.stringify(envelope('StartMatch', { lobbyRevision: revisionA }, { requestId: 'start_a', lobbyId: roomA.lobbyId })));
  c.send(JSON.stringify(envelope('StartMatch', { lobbyRevision: revisionC }, { requestId: 'start_c', lobbyId: roomC.lobbyId })));
  const started = await Promise.all([startA1, startA2, startC1, startC2]);
  assert.equal(started[0].payload.state.match.roster.length, 3); assert.equal(started[2].payload.state.match.roster.length, 2);
  assert(started.slice(0, 2).every(message => message.lobbyId === roomA.lobbyId));
  assert(started.slice(2).every(message => message.lobbyId === roomC.lobbyId));

  const rejected = nextMessage(a);
  a.send(JSON.stringify(envelope('SetReady', { lobbyRevision: readyCD[0].payload.state.lobbyRevision, ready: false },
    { requestId: 'cross_room', lobbyId: roomC.lobbyId })));
  const response = await rejected;
  assert.equal(response.type, 'RequestRejected'); assert.equal(response.payload.code, 'WrongLobby');
  assert(!JSON.stringify(response).includes(roomC.joinCode));
  assert(readyAB.every(message => message.payload.state.lobbyId === roomA.lobbyId));
});

test('non-current WebSocket client receives UnsupportedProtocol before close', async t => {
  const service = new LobbyWebSocketService({ catalog: loadCourseCatalog(courseRoot).slice(0, 1) });
  await service.ready();
  t.after(() => service.close());
  const socket = await opened(service.endpoint());
  const responseMessage = nextMessage(socket), closed = once(socket, 'close');
  socket.send(JSON.stringify({ protocolVersion: 1, type: 'Hello', payload: {} }));
  const response = await responseMessage;
  assert.equal(response.type, 'UnsupportedProtocol');
  assert.equal(response.payload.supportedProtocolVersion, 3);
  await closed;
});
