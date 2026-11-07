// SPDX-License-Identifier: GPL-2.0-or-later
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { once } from 'node:events';
import { fileURLToPath } from 'node:url';
import { WebSocket } from 'ws';
import { envelope } from '../protocol/envelope.ts';
import { loadCourseCatalog } from '../service/course-catalog.ts';
import { jsonLogger, logDeliveries, requestType } from '../service/logger.ts';
import { LobbyWebSocketService } from '../service/websocket-service.ts';

const courseRoot = fileURLToPath(new URL('../../courses/', import.meta.url));

async function client(url: string) {
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

test('JSON logger writes one object per line with a timestamp', () => {
  const lines: string[] = [];
  const log = jsonLogger(line => lines.push(line), () => Date.UTC(2026, 10, 6));
  log('lobby.created', { lobbyId: 'lobby_1', skipped: undefined });
  assert.deepEqual(JSON.parse(lines[0]), { time: '2026-11-06T00:00:00.000Z', event: 'lobby.created', lobbyId: 'lobby_1' });
  assert.equal(requestType('{"type":"JoinLobby","payload":{}}'), 'JoinLobby');
  assert.equal(requestType('{"type":"<script>"}'), 'unknown');
});

test('client-written reasons are not logged unless debugging', () => {
  const events: any[] = [];
  const deliveries = [{ connectionId: 'c', message: envelope('MatchResult',
    { result: { status: 'Interrupted', reason: 'Sam rage quit', matchId: 'm', durationMs: 5 } }, { lobbyId: 'l', matchId: 'm' }) }];
  logDeliveries((event, fields) => events.push({ event, ...fields }), deliveries);
  logDeliveries((event, fields) => events.push({ event, ...fields }), deliveries, undefined, true);
  assert.equal(events[0].reason, 'other'); assert.equal(events[1].reason, 'Sam rage quit');
});

test('lobby and rejection events are logged without names, codes, or addresses', async t => {
  const lines: string[] = [];
  const service = new LobbyWebSocketService({ catalog: loadCourseCatalog(courseRoot).slice(0, 1),
    log: jsonLogger(line => lines.push(line)), limits: { lobbyReconnectMs: 100 } });
  await service.ready(); t.after(() => service.close());
  const host = await client(service.endpoint());
  host.socket.send(JSON.stringify(envelope('CreateLobby', { displayName: 'Secret Sam', colorMode: 'auto', courseId: 'classic' }, { requestId: 'c' })));
  await until(() => host.messages.some(message => message.type === 'LobbyCreated'));
  const joinCode = host.messages.find(message => message.type === 'LobbyCreated').payload.state.joinCode as string;
  const guest = await client(service.endpoint());
  guest.socket.send(JSON.stringify(envelope('JoinLobby', { joinCode: 'ZZZZ2222', displayName: 'Hidden Hana', colorMode: 'auto' }, { requestId: 'j' })));
  await until(() => guest.messages.some(message => message.type === 'RequestRejected'));
  host.socket.close();
  await until(() => lines.some(line => line.includes('lobby.closed')));
  const events = lines.map(line => JSON.parse(line));
  assert.deepEqual(events.map(entry => entry.event), ['lobby.created', 'request.rejected', 'member.reconnecting', 'lobby.closed']);
  assert.deepEqual([events[1].type, events[1].code], ['JoinLobby', 'LobbyNotFound']);
  assert.equal(events[3].reason, 'A participant disconnected');
  const text = lines.join('\n');
  for (const secret of ['Secret Sam', 'Hidden Hana', joinCode, 'ZZZZ2222', '127.0.0.1'])
    assert(!text.includes(secret), secret);
  guest.socket.terminate();
});
