// SPDX-License-Identifier: GPL-2.0-or-later
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { mkdtempSync, readFileSync, writeFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import type { Envelope } from '../protocol/envelope.ts';
import { envelope } from '../protocol/envelope.ts';
import { LobbyProtocolController } from '../service/connection.ts';
import { COURSE_CHUNK_BYTES, loadCourseCatalog } from '../service/course-catalog.ts';

const send = (controller: LobbyProtocolController, connectionId: string, message: Envelope) =>
  controller.receive(connectionId, JSON.stringify(message));

test('members can download a shipped course whose bytes hash to the catalog hash', () => {
  const root = mkdtempSync(join(tmpdir(), 'kolf-shipped-'));
  const original = readFileSync(fileURLToPath(new URL('../../courses/Hard.kolf', import.meta.url)), 'utf8');
  writeFileSync(join(root, 'big.kolf'), original.replaceAll('\r\n', '\n').replaceAll('\n', '\r\n'));
  const [entry] = loadCourseCatalog(root, [{ courseId: 'big', displayName: 'Big', fileName: 'big.kolf' }]);
  assert.equal(entry.source, 'shipped');
  assert(entry.byteSize! > COURSE_CHUNK_BYTES);

  const controller = new LobbyProtocolController([entry]);
  const host = controller.connect(), guest = controller.connect();
  const created = send(controller, host, envelope('CreateLobby', { displayName: 'Host', colorMode: 'auto', courseId: 'big' }, { requestId: 'c' }));
  const state = created[0].message.payload.state as any, lobbyId = state.lobbyId as string;
  send(controller, guest, envelope('JoinLobby', { joinCode: state.joinCode, displayName: 'Guest', colorMode: 'auto' }, { requestId: 'j' }));
  const revision = controller.service.activeLobby(lobbyId).lobbyRevision;
  for (const [client, requestId] of [[host, 'rh'], [guest, 'rg']])
    send(controller, client, envelope('SetReady', { lobbyRevision: revision, ready: true }, { requestId, lobbyId }));
  const start = send(controller, host, envelope('StartMatch', { lobbyRevision: revision }, { requestId: 's', lobbyId }));
  const matchId = (start[0].message.payload.state as any).match.matchId as string;

  const chunks: Buffer[] = [];
  for (let index = 0; index < Math.ceil(entry.byteSize! / COURSE_CHUNK_BYTES); ++index) {
    const reply = send(controller, guest, envelope('GetCourseChunk', { sha256: entry.expectedHash, index },
      { requestId: `g${index}`, lobbyId, matchId }));
    assert.equal(reply[0].message.type, 'CourseChunkData');
    chunks.push(Buffer.from(String(reply[0].message.payload.data), 'base64'));
  }
  const bytes = Buffer.concat(chunks);
  assert.equal(bytes.length, entry.byteSize);
  assert(!bytes.includes('\r'));
  assert.equal(createHash('sha256').update(bytes).digest('hex'), entry.expectedHash);

  const wrong = send(controller, guest, envelope('GetCourseChunk', { sha256: 'c'.repeat(64), index: 0 },
    { requestId: 'bad', lobbyId, matchId }));
  assert.equal(wrong[0].message.payload.code, 'CourseUnavailable');
  const beyond = send(controller, guest, envelope('GetCourseChunk', { sha256: entry.expectedHash, index: Math.ceil(entry.byteSize! / COURSE_CHUNK_BYTES) },
    { requestId: 'far', lobbyId, matchId }));
  assert.equal(beyond[0].message.payload.code, 'CourseUnavailable');
});
