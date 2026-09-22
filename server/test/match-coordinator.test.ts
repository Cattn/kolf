// SPDX-License-Identifier: GPL-2.0-or-later
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { envelope } from '../protocol/envelope.ts';
import type { LobbyState } from '../service/lobby.ts';
import { MatchCoordinator } from '../service/match-session.ts';

const courseHash = 'a'.repeat(64), compatibilityId = 'b'.repeat(64);

function lobbyState(owners: string[]): LobbyState {
  const members = [...new Set(owners)].map((memberId, index) => ({
    memberId, connectionId: `connection_${index}`, displayName: memberId, ready: true as const, connected: true as const,
  }));
  const roster = owners.map((ownerMemberId, engineIndex) => ({ playerId: `player_${engineIndex}`, ownerMemberId, engineIndex,
    displayName: `Player ${engineIndex}`, color: `#${String(engineIndex + 1).padStart(6, '0')}ff` }));
  return {
    lobbyId: 'lobby_1', joinCode: 'ABCDEFGH', ownerMemberId: owners[0], lobbyRevision: 4,
    phase: 'Preparing', selectedCourseId: 'classic', members,
    players: roster.map(player => ({ ...player, order: player.engineIndex })),
    match: { matchId: 'match_1', authorityMemberId: owners[0], compatibilityId,
      course: { courseId: 'classic', displayName: 'Classic', expectedHash: courseHash }, roster },
  };
}

function state(rosterSize: number, activeSlot = 0) {
  return {
    stateRevision: 1, holeGeneration: 1, turnId: 1, activeSlot, hole: 1, par: 3,
    phase: 'AwaitingShot', manifestHash: compatibilityId, courseHash,
    balls: Array.from({ length: rosterSize }, (_, index) => ({ id: `ball/${index}`, kind: 'sprite', x: 100 + index,
      y: 200, z: 1, visible: true, rotation: 0, opacity: 1, sprite: 'ball', frame: -1, state: 1 })),
    objects: [], scores: Array.from({ length: rosterSize }, () => [0]),
  };
}

function scoped(type: string, payload: Record<string, unknown>) {
  return envelope(type, payload, { requestId: `request_${type}`, lobbyId: 'lobby_1', matchId: 'match_1' });
}

function open(coordinator: MatchCoordinator, members: string[], snapshot: ReturnType<typeof state>) {
  coordinator.start();
  for (const member of members) coordinator.receive(member, scoped('SceneReady', { manifestHash: compatibilityId }));
  const committed = coordinator.receive(members[0], scoped('InitialState', { state: snapshot }));
  const syncId = committed.deliveries[0].message.payload.syncId as number;
  let progress = committed;
  for (const member of members) progress = coordinator.receive(member, scoped('StateApplied', {
    stateRevision: 1, syncId, manifestHash: compatibilityId,
  }));
  assert.equal(progress.becamePlaying, true);
  return syncId;
}

test('three members coordinate four players with multi-slot ownership and frame fan-out', () => {
  const owners = ['member_a', 'member_b', 'member_a', 'member_c'];
  const coordinator = new MatchCoordinator(lobbyState(owners));
  const started = coordinator.start();
  assert.equal(started.deliveries.filter(delivery => delivery.message.type === 'Welcome').length, 3);
  assert.deepEqual(started.deliveries.find(delivery => delivery.memberId === 'member_a' && delivery.message.type === 'Welcome')
    ?.message.payload.playerIds, ['player_0', 'player_2']);
  for (const member of ['member_a', 'member_b']) {
    const progress = coordinator.receive(member, scoped('SceneReady', { manifestHash: compatibilityId }));
    assert.equal(progress.deliveries.length, 0);
  }
  const begin = coordinator.receive('member_c', scoped('SceneReady', { manifestHash: compatibilityId }));
  assert.deepEqual(begin.deliveries.map(delivery => [delivery.memberId, delivery.message.type]), [['member_a', 'StartMatch']]);
  const committed = coordinator.receive('member_a', scoped('InitialState', { state: state(4, 1) }));
  const syncId = committed.deliveries[0].message.payload.syncId as number;
  coordinator.receive('member_a', scoped('StateApplied', { stateRevision: 1, syncId, manifestHash: compatibilityId }));
  coordinator.receive('member_b', scoped('StateApplied', { stateRevision: 1, syncId, manifestHash: compatibilityId }));
  const opened = coordinator.receive('member_c', scoped('StateApplied', { stateRevision: 1, syncId, manifestHash: compatibilityId }));
  assert.equal(opened.becamePlaying, true); assert.equal(opened.deliveries.length, 3);

  const wrong = coordinator.receive('member_c', scoped('SubmitShot', { commandId: 'shot_wrong', holeGeneration: 1, turnId: 1,
    playerId: 'player_1', puttingMode: 'normal', directionRadians: 0, launchMagnitude: 1 }));
  assert.equal(wrong.deliveries[0].message.type, 'CommandRejected');
  const admitted = coordinator.receive('member_b', scoped('SubmitShot', { commandId: 'shot_right', holeGeneration: 1, turnId: 1,
    playerId: 'player_1', puttingMode: 'normal', directionRadians: 0, launchMagnitude: 1 }));
  assert.deepEqual(admitted.deliveries.map(delivery => [delivery.memberId, delivery.message.type]),
    [['member_b', 'ShotPending'], ['member_a', 'AdmitShot']]);

  const frame = coordinator.receive('member_a', scoped('StateFrame', { state: state(4, 1), syncId, frameSeq: 1, hostMs: 10 }));
  assert.deepEqual(frame.deliveries.map(delivery => delivery.memberId), ['member_b', 'member_c']);
  assert(frame.deliveries.every(delivery => delivery.visual));
});

test('eight-player boundary accepts exact state and rejects a smaller snapshot', () => {
  const owners = ['member_a', 'member_b', 'member_a', 'member_b', 'member_a', 'member_b', 'member_a', 'member_b'];
  const coordinator = new MatchCoordinator(lobbyState(owners));
  const syncId = open(coordinator, ['member_a', 'member_b'], state(8));
  assert.equal(syncId, 1);

  const invalid = new MatchCoordinator(lobbyState(owners)); invalid.start();
  invalid.receive('member_a', scoped('SceneReady', { manifestHash: compatibilityId }));
  invalid.receive('member_b', scoped('SceneReady', { manifestHash: compatibilityId }));
  const progress = invalid.receive('member_a', scoped('InitialState', { state: state(7) }));
  assert.equal(progress.interruptedReason, 'The match protocol was interrupted.');
});

test('a later hole uses its own client-verified scene manifest', () => {
  const coordinator = new MatchCoordinator(lobbyState(['member_a', 'member_b']));
  open(coordinator, ['member_a', 'member_b'], state(2));
  coordinator.receive('member_a', scoped('SubmitShot', { commandId: 'shot_hole', holeGeneration: 1, turnId: 1,
    playerId: 'player_0', puttingMode: 'normal', directionRadians: 0, launchMagnitude: 1 }));
  coordinator.receive('member_a', scoped('ShotAccepted', { commandId: 'shot_hole' }));
  const moving = { ...state(2), stateRevision: 2, phase: 'Simulating' };
  const movingProgress = coordinator.receive('member_a', scoped('CommitTransition', { state: moving }));
  const movingSync = movingProgress.deliveries[0].message.payload.syncId as number;
  for (const member of ['member_a', 'member_b']) coordinator.receive(member, scoped('StateApplied', {
    stateRevision: 2, syncId: movingSync, manifestHash: compatibilityId,
  }));

  const nextManifest = 'c'.repeat(64);
  const nextHole = { ...state(2), stateRevision: 3, holeGeneration: 2, turnId: 2, hole: 2,
    manifestHash: nextManifest, scores: [[1, 0], [0, 0]] };
  const committed = coordinator.receive('member_a', scoped('CommitTransition', { state: nextHole }));
  assert(committed.deliveries.every(delivery => delivery.message.type === 'TransitionCommitted'));
  const syncId = committed.deliveries[0].message.payload.syncId as number;
  coordinator.receive('member_a', scoped('StateApplied', { stateRevision: 3, syncId, manifestHash: nextManifest }));
  const applied = coordinator.receive('member_b', scoped('StateApplied', { stateRevision: 3, syncId, manifestHash: nextManifest }));
  assert.equal(applied.interruptedReason, undefined);
  assert(applied.deliveries.every(delivery => delivery.message.type === 'InputReady'));
});
