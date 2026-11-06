// SPDX-License-Identifier: GPL-2.0-or-later
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { once } from 'node:events';
import { fileURLToPath } from 'node:url';
import { WebSocket } from 'ws';
import type { Envelope } from '../protocol/envelope.ts';
import { envelope } from '../protocol/envelope.ts';
import { LobbyProtocolController } from '../service/connection.ts';
import { loadCourseCatalog } from '../service/course-catalog.ts';
import { LobbyWebSocketService } from '../service/websocket-service.ts';

const courseRoot = fileURLToPath(new URL('../../courses/', import.meta.url));
const catalog = [{ courseId: 'classic', displayName: 'Classic', expectedHash: 'a'.repeat(64), par: [3] }];
const send = (controller: LobbyProtocolController, connectionId: string, message: Envelope) =>
  controller.receive(connectionId, JSON.stringify(message));

test('a draining server tells everyone and refuses new lobbies and matches', () => {
  const controller = new LobbyProtocolController(catalog, undefined, () => 1000,
    undefined, { serverName: 'kolf.pro', serverVersion: '1' });
  const host = controller.connect(), guest = controller.connect(), idle = controller.connect();
  const created = send(controller, host, envelope('CreateLobby', { displayName: 'Host', colorMode: 'auto', courseId: 'classic' }, { requestId: 'c' }));
  const state = created[0].message.payload.state as any, lobbyId = state.lobbyId as string;
  send(controller, guest, envelope('JoinLobby', { joinCode: state.joinCode, displayName: 'Guest', colorMode: 'auto' }, { requestId: 'j' }));

  const notices = controller.beginDrain(601_000);
  assert.deepEqual(notices.map(delivery => delivery.connectionId).sort(), [host, guest, idle].sort());
  assert.deepEqual(notices[0].message.payload, { kind: 'restarting', deadline: 601_000, message: 'kolf.pro is restarting for an update.' });

  const refused = send(controller, idle, envelope('CreateLobby', { displayName: 'Late', colorMode: 'auto', courseId: 'classic' }, { requestId: 'late' }));
  assert.equal(refused[0].message.payload.code, 'ServiceRestarting');
  const revision = controller.service.activeLobby(lobbyId).lobbyRevision;
  for (const [client, requestId] of [[host, 'rh'], [guest, 'rg']])
    send(controller, client, envelope('SetReady', { lobbyRevision: revision, ready: true }, { requestId, lobbyId }));
  const start = send(controller, host, envelope('StartMatch', { lobbyRevision: revision }, { requestId: 's', lobbyId }));
  assert.equal(start[0].message.payload.code, 'ServiceRestarting');

  const late = controller.connect();
  assert.deepEqual(controller.welcome(late).map(delivery => delivery.message.type), ['ServiceHello', 'ServiceNotice']);
});

test('drain closes the service once no match is running', async t => {
  const service = new LobbyWebSocketService({ catalog: loadCourseCatalog(courseRoot).slice(0, 1) });
  await service.ready();
  t.after(() => service.close());
  const socket = new WebSocket(service.endpoint()), messages: any[] = [];
  socket.on('message', data => messages.push(JSON.parse(data.toString())));
  await once(socket, 'open');
  const closed = once(socket, 'close');
  const drained = service.drain(60_000);
  const [code] = await closed;
  await drained;
  assert.equal(code, 1001);
  assert.deepEqual(messages.map(message => message.type), ['ServiceHello', 'ServiceNotice']);
  assert.equal(messages[1].payload.kind, 'restarting');
});
