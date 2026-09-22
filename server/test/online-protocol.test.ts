// SPDX-License-Identifier: GPL-2.0-or-later
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { decodeClientMessage } from '../protocol/codecs.ts';
import { decodeEnvelope, envelope, MAX_MESSAGE_BYTES, ProtocolError } from '../protocol/envelope.ts';
import { compatibilityIdentity } from '../service/compatibility.ts';
import { LobbyError } from '../service/errors.ts';
import { RequestCache } from '../service/request-cache.ts';

test('shared online envelope fixtures', () => {
  const fixture = JSON.parse(readFileSync(new URL('../../protocol/online-envelope-fixtures.json', import.meta.url), 'utf8'));
  for (const entry of fixture.cases) {
    if (entry.valid) assert.equal(decodeEnvelope(JSON.stringify(entry.message)).type, entry.message.type, entry.name);
    else assert.throws(() => decodeEnvelope(JSON.stringify(entry.message)),
      (error: unknown) => error instanceof ProtocolError && error.code === entry.error, entry.name);
  }
});

test('online client codecs validate scope and bounded profile fields', () => {
  const create = envelope('CreateLobby', { displayName: 'Alice', colorMode: 'custom', customColor: '#ff0000ff', courseId: 'classic' }, { requestId: 'request_1' });
  assert.equal(decodeClientMessage(JSON.stringify(create)).type, 'CreateLobby');
  assert.equal(decodeClientMessage(JSON.stringify(envelope('JoinLobby', {
    joinCode: 'ABCDEFGH', displayName: 'Bob', colorMode: 'auto',
  }, { requestId: 'request_auto' }))).type, 'JoinLobby');
  assert.throws(() => decodeClientMessage(JSON.stringify({ ...create,
    payload: { ...create.payload, colorMode: 'auto', customColor: '#ff0000ff' } })), ProtocolError);
  assert.throws(() => decodeClientMessage(JSON.stringify({ ...create, protocolVersion: 2 })),
    (error: unknown) => error instanceof ProtocolError && error.code === 'UnsupportedProtocol');
  assert.throws(() => decodeClientMessage(JSON.stringify({ ...create, lobbyId: 'not_allowed' })), ProtocolError);
  assert.throws(() => decodeClientMessage(JSON.stringify({ ...create, payload: { ...create.payload, displayName: 'x'.repeat(33) } })), ProtocolError);
  assert.throws(() => decodeClientMessage(' '.repeat(MAX_MESSAGE_BYTES + 1)),
    (error: unknown) => error instanceof ProtocolError && error.code === 'MessageTooLarge');
});

test('compatibility identity ignores ordering and text line endings only', () => {
  const first = compatibilityIdentity([
    { path: 'game/b.cpp', content: 'two\r\nlines\r\n' },
    { path: 'game/a.cpp', content: 'same\rcontent\n' },
  ]);
  const second = compatibilityIdentity([
    { path: '.\\game\\a.cpp', content: 'same\ncontent\n' },
    { path: 'game/b.cpp', content: 'two\nlines\n' },
  ]);
  assert.equal(first, second);
  assert.notEqual(first, compatibilityIdentity([{ path: 'game/a.cpp', content: 'changed\n' }]));
  assert.throws(() => compatibilityIdentity([{ path: '../outside.cpp', content: '' }]));
});

test('request cache replays identical work, rejects conflicts, and remains bounded', () => {
  let now = 0, calls = 0;
  const cache = new RequestCache(() => now, 2, 10);
  assert.deepEqual(cache.run('member', 'request_1', 'SetReady', { ready: true }, () => ({ value: ++calls })), { value: 1 });
  assert.deepEqual(cache.run('member', 'request_1', 'SetReady', { ready: true }, () => ({ value: ++calls })), { value: 1 });
  assert.throws(() => cache.run('member', 'request_1', 'SetReady', { ready: false }, () => null),
    (error: unknown) => error instanceof LobbyError && error.code === 'RequestConflict');
  cache.run('member', 'request_2', 'SetReady', {}, () => 2);
  cache.run('member', 'request_3', 'SetReady', {}, () => 3);
  assert.equal(cache.size, 2);
  cache.clearScope('member');
  assert.equal(cache.size, 0);
  cache.run('other', 'request_4', 'SetReady', {}, () => 4);
  now = 11;
  assert.equal(cache.size, 0);
});
