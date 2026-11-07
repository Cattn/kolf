// SPDX-License-Identifier: GPL-2.0-or-later
import { test } from 'node:test';
import assert from 'node:assert/strict';
import type { Envelope } from '../protocol/envelope.ts';
import { envelope } from '../protocol/envelope.ts';
import { LobbyProtocolController } from '../service/connection.ts';

const catalog = [{ courseId: 'classic', displayName: 'Classic', expectedHash: 'a'.repeat(64), par: [3] }];
const send = (controller: LobbyProtocolController, connectionId: string, message: Envelope) =>
  controller.receive(connectionId, JSON.stringify(message));
const buildA = { appVersion: '26.08.1-cattn.2', rulesBuildId: 'a1b2c3d4'.repeat(8) };
const buildB = { appVersion: '26.08.1-cattn.1', rulesBuildId: 'ffee0011'.repeat(8) };

function hostLobby(controller: LobbyProtocolController, clientInfo?: typeof buildA) {
  const host = controller.connect();
  const created = send(controller, host, envelope('CreateLobby', {
    displayName: 'Sam', colorMode: 'auto', courseId: 'classic', ...(clientInfo ? { clientInfo } : {}),
  }, { requestId: 'c' }));
  return (created[0].message.payload.state as any).joinCode as string;
}
const join = (controller: LobbyProtocolController, joinCode: string, clientInfo?: typeof buildA) =>
  send(controller, controller.connect(), envelope('JoinLobby', {
    joinCode, displayName: 'Ana', colorMode: 'auto', ...(clientInfo ? { clientInfo } : {}),
  }, { requestId: 'j' }))[0].message;

test('a guest with the host build joins', () => {
  const controller = new LobbyProtocolController(catalog);
  const code = hostLobby(controller, buildA);
  assert.equal(join(controller, code, { ...buildA }).type, 'LobbyState');
});

test('a guest with another build is told both versions at join time', () => {
  const controller = new LobbyProtocolController(catalog);
  const code = hostLobby(controller, buildA);
  const rejected = join(controller, code, buildB);
  assert.equal(rejected.type, 'RequestRejected');
  assert.equal(rejected.payload.code, 'IncompatibleBuild');
  assert.equal(rejected.payload.hostVersion, '26.08.1-cattn.2');
  assert.equal(rejected.payload.hostBuild, 'a1b2c3d4');
  assert.equal(rejected.payload.yourVersion, '26.08.1-cattn.1');
  assert.equal(rejected.payload.yourBuild, 'ffee0011');
  const unknown = join(controller, code);
  assert.equal(unknown.payload.code, 'IncompatibleBuild');
  assert.equal(unknown.payload.yourVersion, 'unknown');
  assert.equal(controller.service.membershipCount, 1);
});

test('lobbies from clients that send no build are not checked', () => {
  const controller = new LobbyProtocolController(catalog);
  const code = hostLobby(controller);
  assert.equal(join(controller, code, buildB).type, 'LobbyState');
});

test('malformed client info is rejected', () => {
  const controller = new LobbyProtocolController(catalog);
  const code = hostLobby(controller, buildA);
  for (const clientInfo of [{ appVersion: '26', rulesBuildId: 'XYZ' }, { appVersion: 'a b', rulesBuildId: buildA.rulesBuildId },
    { ...buildA, extra: 'x' }])
    assert.equal(join(controller, code, clientInfo as any).payload.code, 'InvalidPayload');
});
