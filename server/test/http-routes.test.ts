// SPDX-License-Identifier: GPL-2.0-or-later
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { once } from 'node:events';
import { fileURLToPath } from 'node:url';
import { WebSocket } from 'ws';
import { envelope } from '../protocol/envelope.ts';
import { loadCourseCatalog } from '../service/course-catalog.ts';
import { normalizeJoinCode } from '../service/http-routes.ts';
import { MAX_MOTD_LENGTH, serverInfoFromEnvironment } from '../service/server-info.ts';
import { LobbyWebSocketService } from '../service/websocket-service.ts';

const courseRoot = fileURLToPath(new URL('../../courses/', import.meta.url));
const info = { serverName: 'Test Green', serverVersion: '9.9.9', motd: 'Welcome back', publicUrl: 'https://golf.example' };

async function started(t: { after: (fn: () => unknown) => void }) {
  const service = new LobbyWebSocketService({ catalog: loadCourseCatalog(courseRoot).slice(0, 1), info });
  await service.ready(); t.after(() => service.close());
  return service;
}

async function helloFrom(url: string) {
  const socket = new WebSocket(url);
  const [data] = await once(socket, 'message');
  return { socket, hello: JSON.parse(data.toString()) };
}

test('healthz answers ok and unknown paths are 404', async t => {
  const service = await started(t);
  const health = await fetch(`${service.httpEndpoint()}/healthz`);
  assert.equal(health.status, 200); assert.equal(await health.text(), 'ok\n');
  assert.equal(health.headers.get('cache-control'), 'no-store');
  assert.equal((await fetch(`${service.httpEndpoint()}/nothing`)).status, 404);
  assert.equal((await fetch(`${service.httpEndpoint()}/`)).status, 404);
  const post = await fetch(`${service.httpEndpoint()}/healthz`, { method: 'POST' });
  assert.equal(post.status, 405); assert.equal(post.headers.get('allow'), 'GET, HEAD');
  const head = await fetch(`${service.httpEndpoint()}/healthz`, { method: 'HEAD' });
  assert.equal(head.status, 200); assert.equal(await head.text(), '');
});

test('status reports counts only', async t => {
  const service = await started(t);
  const { socket, hello } = await helloFrom(`${service.endpoint()}/ws`); t.after(() => socket.terminate());
  socket.send(JSON.stringify(envelope('CreateLobby', { displayName: 'Secret Sam', colorMode: 'auto', courseId: 'classic' }, { requestId: 'r1' })));
  const [created] = await once(socket, 'message');
  const joinCode = JSON.parse(created.toString()).payload.state.joinCode as string;
  assert.match(joinCode, /^[A-Z2-9]+$/);
  assert.equal(hello.payload.serverName, 'Test Green');
  const response = await fetch(`${service.httpEndpoint()}/status`);
  assert.equal(response.status, 200);
  assert.match(response.headers.get('content-type') ?? '', /application\/json/);
  const text = await response.text(), status = JSON.parse(text);
  assert.deepEqual(Object.keys(status).sort(), ['connections', 'lobbies', 'name', 'protocolVersion', 'uptime', 'version']);
  assert.equal(status.name, 'Test Green'); assert.equal(status.version, '9.9.9'); assert.equal(status.protocolVersion, 3);
  assert.equal(status.lobbies, 1); assert.equal(status.connections, 1);
  assert(Number.isInteger(status.uptime) && status.uptime >= 0);
  assert(!text.includes('Secret Sam')); assert(!text.includes(joinCode));
});

test('join pages look the same whether or not the lobby exists', async t => {
  const service = await started(t);
  const { socket } = await helloFrom(`${service.endpoint()}/ws`); t.after(() => socket.terminate());
  socket.send(JSON.stringify(envelope('CreateLobby', { displayName: 'Host', colorMode: 'auto', courseId: 'classic' }, { requestId: 'r1' })));
  const [created] = await once(socket, 'message');
  const realCode = JSON.parse(created.toString()).payload.state.joinCode as string;
  const fakeCode = realCode === 'ABCDEFGH' ? 'ABCDEFGJ' : 'ABCDEFGH';
  const real = await fetch(`${service.httpEndpoint()}/join/${realCode.toLowerCase()}`);
  const fake = await fetch(`${service.httpEndpoint()}/join/${fakeCode.slice(0, 4)}-${fakeCode.slice(4)}`);
  assert.equal(real.status, 200); assert.equal(fake.status, 200);
  const realBody = await real.text(), fakeBody = await fake.text();
  assert.equal(realBody.split(realCode).join('CODE').split(`${realCode.slice(0, 4)}-${realCode.slice(4)}`).join('CODE'),
    fakeBody.split(fakeCode).join('CODE').split(`${fakeCode.slice(0, 4)}-${fakeCode.slice(4)}`).join('CODE'));
  assert(realBody.includes(`kolf://join/${realCode}?server=${encodeURIComponent('wss://golf.example/ws')}`));
  assert.match(real.headers.get('content-security-policy') ?? '', /default-src 'none'/);
  assert.equal((await fetch(`${service.httpEndpoint()}/join/${encodeURIComponent('<script>')}`)).status, 404);
  assert.equal((await fetch(`${service.httpEndpoint()}/join/O0O0O0`)).status, 404);
});

test('WebSocket upgrades are accepted on / and /ws only', async t => {
  const service = await started(t);
  for (const path of ['', '/ws']) {
    const { socket, hello } = await helloFrom(`${service.endpoint()}${path}`);
    assert.equal(hello.type, 'ServiceHello');
    assert.equal(hello.payload.serverVersion, '9.9.9'); assert.equal(hello.payload.motd, 'Welcome back');
    assert(!('latestClient' in hello.payload));
    socket.terminate();
  }
  const rejected = new WebSocket(`${service.endpoint()}/elsewhere`);
  rejected.on('error', () => {});
  const [, response] = await once(rejected, 'unexpected-response');
  assert.equal(response.statusCode, 404);
  rejected.terminate();
});

test('server info from the environment is plain text and bounded', () => {
  const bell = String.fromCharCode(7), separator = String.fromCharCode(0x2028), isolate = String.fromCharCode(0x2066);
  const parsed = serverInfoFromEnvironment({
    KOLF_SERVER_NAME: `  kolf${bell}.pro  `, KOLF_MOTD: `Line one${separator}line two ${isolate}x${'y'.repeat(400)}`,
    KOLF_PUBLIC_URL: 'javascript:alert(1)', KOLF_LATEST_CLIENT_VERSION: '26.08.1-cattn.2',
  });
  assert.equal(parsed.serverName, 'kolf .pro');
  assert(parsed.motd!.startsWith('Line one line two x'));
  assert.equal(parsed.motd!.length, MAX_MOTD_LENGTH);
  assert.equal(parsed.publicUrl, undefined);
  assert.equal(parsed.latestClient, undefined);
  const complete = serverInfoFromEnvironment({ KOLF_LATEST_CLIENT_VERSION: '2', KOLF_LATEST_CLIENT_URL: 'https://kolf.pro/download/' });
  assert.deepEqual(complete.latestClient, { version: '2', url: 'https://kolf.pro/download' });
  assert.equal(complete.serverName, 'Kolf server');
  assert.match(complete.serverVersion, /^\d+\.\d+\.\d+/);
});

test('join codes ignore case, dashes, and spaces', () => {
  assert.equal(normalizeJoinCode('k7qf-2mxa'), 'K7QF2MXA');
  assert.equal(normalizeJoinCode('K7QF%202MXA'), 'K7QF2MXA');
  assert.equal(normalizeJoinCode('IIII1111'), undefined);
  assert.equal(normalizeJoinCode('%E0%A4%A'), undefined);
});
