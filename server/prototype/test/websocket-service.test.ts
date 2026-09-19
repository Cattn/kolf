// SPDX-License-Identifier: GPL-2.0-or-later
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { once } from 'node:events';
import { fileURLToPath } from 'node:url';
import { WebSocket } from 'ws';
import { envelope } from '../protocol/envelope.ts';
import { loadCourseCatalog } from '../service/course-catalog.ts';
import { LobbyWebSocketService } from '../service/websocket-service.ts';

const courseRoot = fileURLToPath(new URL('../../../courses/', import.meta.url));

async function opened(url: string) {
  const socket = new WebSocket(url);
  await once(socket, 'open');
  return socket;
}
async function nextMessage(socket: WebSocket): Promise<any> {
  const [data] = await once(socket, 'message');
  return JSON.parse(data.toString());
}

test('shipped course catalog hashes raw allowlisted files', () => {
  const catalog = loadCourseCatalog(courseRoot);
  assert.deepEqual(catalog.map(course => course.courseId), ['classic', 'easy', 'practice']);
  assert(catalog.every(course => /^[a-f0-9]{64}$/.test(course.expectedHash)));
  assert.throws(() => loadCourseCatalog(courseRoot, [{ courseId: 'escape', displayName: 'Escape', fileName: '../intro' }]));
});

test('two remote WebSocket clients create and join the v2 lobby', async t => {
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

test('v1 WebSocket client receives UnsupportedProtocol before close', async t => {
  const service = new LobbyWebSocketService({ catalog: loadCourseCatalog(courseRoot).slice(0, 1) });
  await service.ready();
  t.after(() => service.close());
  const socket = await opened(service.endpoint());
  const responseMessage = nextMessage(socket), closed = once(socket, 'close');
  socket.send(JSON.stringify({ protocolVersion: 1, type: 'Hello', payload: {} }));
  const response = await responseMessage;
  assert.equal(response.type, 'UnsupportedProtocol');
  assert.equal(response.payload.supportedProtocolVersion, 2);
  await closed;
});
